#include "net/http_client.h"

#include <cstdio>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace {

constexpr const char* kTag = "http";

}  // namespace

namespace net {

bool http_get(const std::string& url, std::string& out, int timeout_ms) {
  out.clear();

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
  cfg.user_agent = "Mozilla/5.0 (X11; Linux x86_64) T5-Dashboard/1.0";
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

}  // namespace net
