#include "net/http_client.h"

#include <cstdio>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace {

constexpr const char* kTag = "http";
constexpr const char* kUserAgent = "Mozilla/5.0 (X11; Linux x86_64) T5-Dashboard/1.0";

// Weather, markets and geocoding all fetch HTTPS from different tasks. The
// certificate bundle is initialised on the first attach and is not reentrant,
// so serialise the whole request. This also keeps two TLS sessions from
// competing for the little internal RAM epdiy leaves behind.
SemaphoreHandle_t g_fetch_lock = xSemaphoreCreateMutex();

struct FetchLock {
  FetchLock() {
    if (g_fetch_lock != nullptr) xSemaphoreTake(g_fetch_lock, portMAX_DELAY);
  }
  ~FetchLock() {
    if (g_fetch_lock != nullptr) xSemaphoreGive(g_fetch_lock);
  }
};

// Split "https://host[:port]/path". Returns false for anything unexpected.
bool split_url(const std::string& url, std::string& host, std::string& path) {
  const char* scheme = strstr(url.c_str(), "://");
  if (scheme == nullptr) return false;
  const char* start = scheme + 3;
  size_t host_len = strcspn(start, ":/");
  if (host_len == 0 || host_len >= 128) return false;
  host.assign(start, host_len);

  const char* after = start + host_len;
  if (*after == ':') {
    after += strspn(after, ":0123456789");
    if (*after != '/') after = "/";
  }
  path = *after != '\0' ? after : "/";
  return true;
}

// ZenQuotes answers 200 with neither Content-Length nor Transfer-Encoding: the
// body ends when the server closes the socket. ESP-IDF treats an unknown length
// as chunked and parses zero bytes, so read the same URL over a bare TLS socket
// and strip the header block by hand.
bool tls_get_close(const std::string& url, std::string& out, int timeout_ms) {
  std::string host;
  std::string path;
  if (!split_url(url, host, path)) return false;

  esp_tls_cfg_t cfg{};
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = timeout_ms;
  cfg.non_block = false;

  esp_tls_t* tls = esp_tls_init();
  if (tls == nullptr) return false;
  if (esp_tls_conn_http_new_sync(url.c_str(), &cfg, tls) != 1) {
    ESP_LOGW(kTag, "raw TLS connect failed for %s", host.c_str());
    esp_tls_conn_destroy(tls);
    return false;
  }

  char request[640];
  int request_len = snprintf(request, sizeof(request),
                             "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n"
                             "Accept: */*\r\nConnection: close\r\n\r\n",
                             path.c_str(), host.c_str(), kUserAgent);
  size_t sent = 0;
  while (sent < static_cast<size_t>(request_len)) {
    ssize_t n = esp_tls_conn_write(tls, request + sent, request_len - sent);
    if (n <= 0) {
      ESP_LOGW(kTag, "raw TLS write failed");
      esp_tls_conn_destroy(tls);
      return false;
    }
    sent += static_cast<size_t>(n);
  }

  std::string raw;
  char buf[1024];
  for (;;) {
    ssize_t n = esp_tls_conn_read(tls, buf, sizeof(buf));
    if (n <= 0) break;
    raw.append(buf, static_cast<size_t>(n));
    if (raw.size() > 64 * 1024) break;
  }
  esp_tls_conn_destroy(tls);

  const size_t sep = raw.find("\r\n\r\n");
  if (sep == std::string::npos) {
    ESP_LOGW(kTag, "raw TLS: no header terminator (%u bytes)", static_cast<unsigned>(raw.size()));
    return false;
  }
  const std::string head = raw.substr(0, sep);
  if (head.compare(0, 12, "HTTP/1.1 200") != 0 && head.compare(0, 12, "HTTP/1.0 200") != 0) {
    ESP_LOGW(kTag, "raw TLS: bad status %s", head.substr(0, head.find("\r\n")).c_str());
    return false;
  }
  if (head.find("Transfer-Encoding: chunked") != std::string::npos) {
    // Would need a chunk decoder; not what this fallback exists for.
    ESP_LOGW(kTag, "raw TLS: unexpected chunked body");
    return false;
  }
  out = raw.substr(sep + 4);
  return !out.empty();
}

}  // namespace

namespace net {

bool http_get(const std::string& url, std::string& out, int timeout_ms) {
  out.clear();
  FetchLock lock;

  // Resolve up front so a DNS stall is visible in the log instead of looking
  // like a generic TLS timeout. The result also warms the lwIP DNS cache.
  {
    const char* host = strstr(url.c_str(), "://");
    if (host != nullptr) {
      host += 3;
      size_t host_len = strcspn(host, ":/");
      char name[128] = {};
      if (host_len < sizeof(name)) {
        memcpy(name, host, host_len);
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        int64_t start = esp_timer_get_time();
        int rc = getaddrinfo(name, nullptr, &hints, &res);
        ESP_LOGI(kTag, "dns %s rc=%d in %d ms", name, rc,
                 static_cast<int>((esp_timer_get_time() - start) / 1000));
        if (res != nullptr) freeaddrinfo(res);
      }
    }
  }

  esp_http_client_config_t cfg{};
  cfg.url = url.c_str();
  cfg.timeout_ms = timeout_ms;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  // IPv6 is enabled in this build but the LAN has no global IPv6, so a AAAA
  // answer makes the connection hang until it times out. Force IPv4.
  cfg.addr_type = HTTP_ADDR_TYPE_INET;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1024;
  cfg.user_agent = kUserAgent;
  cfg.disable_auto_redirect = false;
  cfg.max_redirection_count = 3;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) return false;

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  int content_len = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGW(kTag, "HTTP %d for %s", status, url.c_str());
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  char buf[2048];
  int total = 0;
  for (;;) {
    int n = esp_http_client_read(client, buf, sizeof(buf));
    if (n <= 0) break;
    total += n;
    if (total > 512 * 1024) break;  // sanity cap
    out.append(buf, static_cast<size_t>(n));
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (content_len > 0 && total != content_len) {
    ESP_LOGW(kTag, "short read %d/%d", total, content_len);
  }

  // Closed-delimited body (no Content-Length, no chunking): esp_http_client
  // assumes chunked, finds no chunk header and returns nothing. Re-fetch over a
  // raw TLS socket instead.
  if (total == 0 && content_len <= 0) {
    ESP_LOGW(kTag, "close-delimited body, retrying over raw TLS");
    return tls_get_close(url, out, timeout_ms);
  }
  return !out.empty();
}

std::string url_encode(const char* text) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); p != nullptr && *p;
       ++p) {
    unsigned char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '.' || c == '_' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

std::string url_decode(const char* text) {
  auto hex_value = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };

  std::string out;
  for (const char* p = text; p != nullptr && *p; ++p) {
    if (*p == '+') {
      out.push_back(' ');
      continue;
    }
    if (*p == '%' && p[1] != '\0' && p[2] != '\0') {
      const int high = hex_value(p[1]);
      const int low = hex_value(p[2]);
      if (high >= 0 && low >= 0) {
        out.push_back(static_cast<char>((high << 4) | low));
        p += 2;
        continue;
      }
    }
    out.push_back(*p);
  }
  return out;
}

}  // namespace net
