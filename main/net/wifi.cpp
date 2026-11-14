#include "net/wifi.h"

#include <cstring>
#include <ctime>

#include "app/settings.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace {

constexpr const char* kTag = "wifi";
constexpr int kConnectedBit = BIT0;
constexpr int kFailedBit = BIT1;
constexpr const char* kApSsid = "T5-Dashboard";
constexpr const char* kApPass = "t5dashboard";

EventGroupHandle_t g_events = nullptr;
esp_netif_t* g_sta_netif = nullptr;
esp_netif_t* g_ap_netif = nullptr;
volatile bool g_ap_active = false;
char g_ip[20] = "0.0.0.0";

void on_wifi_event(void*, esp_event_base_t, int32_t id, void* data) {
  if (id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(g_events, kConnectedBit);
    auto* info = static_cast<wifi_event_sta_disconnected_t*>(data);
    ESP_LOGW(kTag, "disconnected (reason %d)", info != nullptr ? info->reason : -1);
    xEventGroupSetBits(g_events, kFailedBit);
  } else if (id == WIFI_EVENT_AP_START) {
    g_ap_active = true;
  } else if (id == WIFI_EVENT_AP_STOP) {
    g_ap_active = false;
  }
}

void on_ip_event(void*, esp_event_base_t, int32_t id, void* data) {
  if (id != IP_EVENT_STA_GOT_IP) return;
  auto* ev = static_cast<ip_event_got_ip_t*>(data);
  snprintf(g_ip, sizeof(g_ip), IPSTR, IP2STR(&ev->ip_info.ip));
  ESP_LOGI(kTag, "got IP %s", g_ip);
  xEventGroupSetBits(g_events, kConnectedBit);
}

// Minimal captive-portal DNS: answer every A query with the AP address.
void dns_task(void*) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) return;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(sock);
    return;
  }
  uint8_t buf[512];
  for (;;) {
    sockaddr_in from{};
    socklen_t len = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &len);
    if (n < 12) continue;
    uint8_t reply[512];
    memcpy(reply, buf, n);
    reply[2] |= 0x80;  // response
    reply[7] = 1;      // one answer
    // Keep the question, then append an A record for 192.168.4.1.
    int qlen = n;
    reply[qlen++] = 0xC0;
    reply[qlen++] = 0x0C;
    reply[qlen++] = 0x00;
    reply[qlen++] = 0x01;
    reply[qlen++] = 0x00;
    reply[qlen++] = 0x01;
    reply[qlen++] = 0x00;
    reply[qlen++] = 0x00;
    reply[qlen++] = 0x00;
    reply[qlen++] = 0x3C;
    reply[qlen++] = 0x00;
    reply[qlen++] = 0x04;
    reply[qlen++] = 192;
    reply[qlen++] = 168;
    reply[qlen++] = 4;
    reply[qlen++] = 1;
    sendto(sock, reply, qlen, 0, reinterpret_cast<sockaddr*>(&from), len);
  }
}

void start_ap() {
  if (g_ap_active) return;
  esp_wifi_set_mode(WIFI_MODE_APSTA);

  wifi_config_t ap{};
  strlcpy(reinterpret_cast<char*>(ap.ap.ssid), kApSsid, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = strlen(kApSsid);
  strlcpy(reinterpret_cast<char*>(ap.ap.password), kApPass, sizeof(ap.ap.password));
  ap.ap.channel = 1;
  ap.ap.max_connection = 4;
  ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
  // The driver may already be running from the failed STA attempt; starting it
  // twice is not an error we care about.
  esp_err_t err = esp_wifi_start();
  if (err != ESP_OK) ESP_LOGW(kTag, "ap start: %s", esp_err_to_name(err));

  ESP_LOGW(kTag, "setup AP '%s' (%s) at 192.168.4.1", kApSsid, kApPass);
  xTaskCreate(dns_task, "dns53", 3072, nullptr, 3, nullptr);
}

}  // namespace

namespace net {

bool wifi_start() {
  g_events = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  g_sta_netif = esp_netif_create_default_wifi_sta();
  g_ap_netif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    // Not fatal: the panel can still render, just without data.
    ESP_LOGE(kTag, "esp_wifi_init: %s", esp_err_to_name(err));
    return false;
  }
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, nullptr));

  Settings& s = settings::get();
  if (s.ssid[0] != '\0') {
    ESP_LOGI(kTag, "connecting to '%s'", s.ssid);
    if (wifi_connect(s.ssid, s.pass, 15000)) return true;
    ESP_LOGW(kTag, "connection failed, starting setup AP");
  }
  start_ap();
  return false;
}

bool wifi_connect(const char* ssid, const char* pass, int timeout_ms) {
  if (ssid == nullptr || *ssid == '\0') return false;

  wifi_config_t sta{};
  strlcpy(reinterpret_cast<char*>(sta.sta.ssid), ssid, sizeof(sta.sta.ssid));
  strlcpy(reinterpret_cast<char*>(sta.sta.password), pass != nullptr ? pass : "",
          sizeof(sta.sta.password));
  sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
  sta.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  esp_wifi_set_mode(WIFI_MODE_STA);
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));

  xEventGroupClearBits(g_events, kConnectedBit | kFailedBit);
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_wifi_connect();

  EventBits_t bits = xEventGroupWaitBits(g_events, kConnectedBit | kFailedBit, pdFALSE, pdFALSE,
                                         pdMS_TO_TICKS(timeout_ms));
  return (bits & kConnectedBit) != 0;
}

bool wifi_save_credentials(const char* ssid, const char* pass) {
  Settings& s = settings::get();
  settings::set_str(s.ssid, sizeof(s.ssid), ssid);
  settings::set_str(s.pass, sizeof(s.pass), pass);
  settings::save();
  return true;
}

bool wifi_connected() {
  return g_events != nullptr && (xEventGroupGetBits(g_events) & kConnectedBit) != 0;
}

bool ap_active() { return g_ap_active; }

int wifi_rssi() {
  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
  return -127;
}

const char* wifi_ip() { return g_ip; }

const char* wifi_ssid() { return settings::get().ssid; }

const char* ap_ssid() { return kApSsid; }

int wifi_scan(wifi_ap_record_t* out, int max) {
  wifi_scan_config_t cfg{};
  cfg.show_hidden = false;
  uint16_t count = static_cast<uint16_t>(max);
  if (esp_wifi_scan_start(&cfg, true) != ESP_OK) return 0;
  if (esp_wifi_scan_get_ap_records(&count, out) != ESP_OK) return 0;
  return count;
}

void time_sync_start() {
  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
  tzset();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.cloudflare.com");
  esp_sntp_init();
}

bool time_valid() { return time(nullptr) > 1600000000; }

void format_clock(char* out, size_t cap) {
  time_t now = time(nullptr);
  struct tm tm_now{};
  localtime_r(&now, &tm_now);
  strftime(out, cap, "%H:%M", &tm_now);
}

void format_date(char* out, size_t cap) {
  time_t now = time(nullptr);
  struct tm tm_now{};
  localtime_r(&now, &tm_now);
  strftime(out, cap, "%a %d %b", &tm_now);
}

}  // namespace net
