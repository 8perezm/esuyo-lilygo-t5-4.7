#include "net/http_server.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "app/app.h"
#include "app/power.h"
#include "app/settings.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "net/http_client.h"
#include "net/wifi.h"

namespace {

constexpr const char* kTag = "http";
constexpr size_t kMaxBody = 32768;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");

esp_timer_handle_t g_restart_timer = nullptr;

void restart_cb(void*) { esp_restart(); }

void restart_soon() {
  if (g_restart_timer == nullptr) {
    esp_timer_create_args_t args{};
    args.callback = restart_cb;
    args.name = "restart";
    esp_timer_create(&args, &g_restart_timer);
  }
  esp_timer_start_once(g_restart_timer, 400 * 1000);
}

esp_err_t send_raw(httpd_req_t* req, const char* type, const char* body, size_t len) {
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body, len);
}

esp_err_t send_json(httpd_req_t* req, cJSON* root) {
  char* text = cJSON_PrintUnformatted(root);
  esp_err_t err = send_raw(req, "application/json", text != nullptr ? text : "{}",
                           text != nullptr ? strlen(text) : 2);
  cJSON_free(text);
  return err;
}

esp_err_t send_ok(httpd_req_t* req) {
  return send_raw(req, "application/json", "{\"ok\":true}", 11);
}

std::string read_body(httpd_req_t* req) {
  std::string body;
  if (req->content_len > kMaxBody) return body;
  char buf[1024];
  int remaining = req->content_len;
  while (remaining > 0) {
    int n = httpd_req_recv(req, buf, remaining < static_cast<int>(sizeof(buf)) ? remaining
                                                                               : sizeof(buf));
    if (n <= 0) return std::string();
    body.append(buf, static_cast<size_t>(n));
    remaining -= n;
  }
  return body;
}

esp_err_t handle_index(httpd_req_t* req) {
  return send_raw(req, "text/html", reinterpret_cast<const char*>(index_html_start),
                  strlen(reinterpret_cast<const char*>(index_html_start)));
}

esp_err_t handle_css(httpd_req_t* req) {
  return send_raw(req, "text/css", reinterpret_cast<const char*>(style_css_start),
                  strlen(reinterpret_cast<const char*>(style_css_start)));
}

esp_err_t handle_js(httpd_req_t* req) {
  return send_raw(req, "application/javascript", reinterpret_cast<const char*>(app_js_start),
                  strlen(reinterpret_cast<const char*>(app_js_start)));
}

esp_err_t handle_status(httpd_req_t* req) {
  cJSON* root = cJSON_CreateObject();
  const esp_app_desc_t* desc = esp_app_get_description();

  cJSON_AddStringToObject(root, "fw", desc != nullptr ? desc->version : "?");
  cJSON_AddNumberToObject(root, "heap", esp_get_free_heap_size());
  cJSON_AddNumberToObject(root, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000);
  cJSON_AddNumberToObject(root, "rssi", net::wifi_rssi());
  cJSON_AddStringToObject(root, "ip", net::wifi_ip());
  cJSON_AddStringToObject(root, "ssid", net::wifi_ssid());
  cJSON_AddStringToObject(root, "screen", app::current_screen_id());
  cJSON_AddNumberToObject(root, "battery_mv", power::battery_mv());
  cJSON_AddStringToObject(root, "wifi_mode", net::ap_active() ? "ap+sta" : "sta");

  esp_err_t err = send_json(req, root);
  cJSON_Delete(root);
  return err;
}

esp_err_t handle_settings_get(httpd_req_t* req) {
  char* buf = static_cast<char*>(malloc(8192));
  if (buf == nullptr) return httpd_resp_send_500(req);
  app::settings_to_json(buf, 8192);
  esp_err_t err = send_raw(req, "application/json", buf, strlen(buf));
  free(buf);
  return err;
}

esp_err_t handle_settings_put(httpd_req_t* req) {
  std::string body = read_body(req);
  if (body.empty()) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
  if (!app::apply_settings_json(body.c_str(), body.size())) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
  }
  return send_ok(req);
}

esp_err_t handle_screens(httpd_req_t* req) {
  cJSON* root = cJSON_CreateObject();
  cJSON* list = cJSON_AddArrayToObject(root, "screens");
  for (int i = 0; i < app::screen_count(); i++) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", app::screen_id(i));
    cJSON_AddStringToObject(item, "title", app::screen_title(i));
    cJSON_AddItemToArray(list, item);
  }
  esp_err_t err = send_json(req, root);
  cJSON_Delete(root);
  return err;
}

esp_err_t handle_display(httpd_req_t* req) {
  std::string body = read_body(req);
  cJSON* root = cJSON_Parse(body.c_str());
  const cJSON* screen = cJSON_GetObjectItemCaseSensitive(root, "screen");
  if (cJSON_IsString(screen)) app::request_screen(screen->valuestring);
  cJSON_Delete(root);
  return send_ok(req);
}

esp_err_t handle_refresh(httpd_req_t* req) {
  std::string body = read_body(req);
  cJSON* root = cJSON_Parse(body.c_str());
  const cJSON* screen = cJSON_GetObjectItemCaseSensitive(root, "screen");
  if (cJSON_IsString(screen)) app::request_screen(screen->valuestring);
  app::request_refresh();
  cJSON_Delete(root);
  return send_ok(req);
}

esp_err_t handle_wifi_scan(httpd_req_t* req) {
  auto* records = static_cast<wifi_ap_record_t*>(calloc(20, sizeof(wifi_ap_record_t)));
  if (records == nullptr) return httpd_resp_send_500(req);
  int n = net::wifi_scan(records, 20);

  cJSON* root = cJSON_CreateObject();
  cJSON* list = cJSON_AddArrayToObject(root, "networks");
  for (int i = 0; i < n; i++) {
    if (records[i].ssid[0] == '\0') continue;
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "ssid", reinterpret_cast<const char*>(records[i].ssid));
    cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
    cJSON_AddNumberToObject(item, "auth", records[i].authmode);
    cJSON_AddItemToArray(list, item);
  }
  free(records);

  esp_err_t err = send_json(req, root);
  cJSON_Delete(root);
  return err;
}

esp_err_t handle_wifi_post(httpd_req_t* req) {
  std::string body = read_body(req);
  cJSON* root = cJSON_Parse(body.c_str());
  const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
  const cJSON* pass = cJSON_GetObjectItemCaseSensitive(root, "password");
  if (!cJSON_IsString(ssid)) {
    cJSON_Delete(root);
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
  }
  net::wifi_save_credentials(ssid->valuestring,
                             cJSON_IsString(pass) ? pass->valuestring : "");
  cJSON_Delete(root);

  esp_err_t err = send_ok(req);
  restart_soon();
  return err;
}

esp_err_t handle_geocode(httpd_req_t* req) {
  char query[256] = {};
  char value[128] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "q", value, sizeof(value)) != ESP_OK) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "q required");
  }

  std::string url = "https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&format=json&name=";
  url += net::url_encode(value);

  std::string body;
  if (!net::http_get(url, body)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "lookup failed");

  cJSON* parsed = cJSON_Parse(body.c_str());
  cJSON* root = cJSON_CreateObject();
  cJSON* results = cJSON_AddArrayToObject(root, "results");
  const cJSON* list = cJSON_GetObjectItemCaseSensitive(parsed, "results");
  const cJSON* item = nullptr;
  cJSON_ArrayForEach(item, list) {
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
    const cJSON* lat = cJSON_GetObjectItemCaseSensitive(item, "latitude");
    const cJSON* lon = cJSON_GetObjectItemCaseSensitive(item, "longitude");
    if (!cJSON_IsString(name) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) continue;
    cJSON* entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "name", name->valuestring);
    const cJSON* country = cJSON_GetObjectItemCaseSensitive(item, "country");
    const cJSON* admin = cJSON_GetObjectItemCaseSensitive(item, "admin1");
    if (cJSON_IsString(country)) cJSON_AddStringToObject(entry, "country", country->valuestring);
    if (cJSON_IsString(admin)) cJSON_AddStringToObject(entry, "admin1", admin->valuestring);
    cJSON_AddNumberToObject(entry, "lat", lat->valuedouble);
    cJSON_AddNumberToObject(entry, "lon", lon->valuedouble);
    cJSON_AddItemToArray(results, entry);
  }
  cJSON_Delete(parsed);

  esp_err_t err = send_json(req, root);
  cJSON_Delete(root);
  return err;
}

esp_err_t handle_ota(httpd_req_t* req) {
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no slot");

  esp_ota_handle_t ota = 0;
  esp_err_t err = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &ota);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "ota_begin: %s", esp_err_to_name(err));
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin");
  }

  char buf[4096];
  int remaining = req->content_len;
  int written = 0;
  while (remaining > 0) {
    int n = httpd_req_recv(req, buf, remaining < static_cast<int>(sizeof(buf)) ? remaining
                                                                                 : sizeof(buf));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) {
      esp_ota_abort(ota);
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "receive failed");
    }
    err = esp_ota_write(ota, buf, n);
    if (err != ESP_OK) {
      esp_ota_abort(ota);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write");
    }
    written += n;
    remaining -= n;
  }

  err = esp_ota_end(ota);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "ota_end: %s", esp_err_to_name(err));
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "image rejected");
  }
  if (esp_ota_set_boot_partition(partition) != ESP_OK) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot");
  }

  ESP_LOGW(kTag, "OTA wrote %d bytes, restarting", written);
  esp_err_t out = send_ok(req);
  restart_soon();
  return out;
}

esp_err_t handle_reboot(httpd_req_t* req) {
  esp_err_t err = send_ok(req);
  restart_soon();
  return err;
}

// Button discovery: sample every free candidate pin for five seconds while the
// user presses the physical buttons, and report which pins changed. This is how
// a board revision with extra buttons gets mapped without guessing.
esp_err_t handle_gpio_scan(httpd_req_t* req) {
  static const int pins[] = {12, 13, 14, 34, 35, 36, 39};
  constexpr int count = sizeof(pins) / sizeof(pins[0]);

  gpio_config_t cfg{};
  for (int pin : pins) cfg.pin_bit_mask |= (1ULL << pin);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);

  bool changed[count] = {};
  int last[count];
  for (int i = 0; i < count; i++) {
    last[i] = gpio_get_level(static_cast<gpio_num_t>(pins[i]));
  }

  for (int tick = 0; tick < 250; tick++) {  // 250 x 20 ms
    for (int i = 0; i < count; i++) {
      int level = gpio_get_level(static_cast<gpio_num_t>(pins[i]));
      if (level != last[i]) {
        changed[i] = true;
        last[i] = level;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  cJSON* root = cJSON_CreateObject();
  cJSON* list = cJSON_AddArrayToObject(root, "pins");
  for (int i = 0; i < count; i++) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "gpio", pins[i]);
    cJSON_AddBoolToObject(item, "changed", changed[i]);
    cJSON_AddNumberToObject(item, "level", last[i]);
    cJSON_AddItemToArray(list, item);
  }
  esp_err_t err = send_json(req, root);
  cJSON_Delete(root);
  return err;
}

esp_err_t handle_catch_all(httpd_req_t* req) {
  // Captive-portal behaviour: any unknown path shows the control panel.
  return handle_index(req);
}

}  // namespace

namespace server {

void start() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 24;
  cfg.stack_size = 6144;   // internal RAM is tight next to epdiy + Wi-Fi
  cfg.lru_purge_enable = true;
  cfg.max_open_sockets = 3;
  cfg.uri_match_fn = httpd_uri_match_wildcard;

  httpd_handle_t server = nullptr;
  esp_err_t err = httpd_start(&server, &cfg);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "httpd_start: %s", esp_err_to_name(err));
    return;
  }

  const httpd_uri_t routes[] = {
      {"/", HTTP_GET, handle_index, nullptr},
      {"/index.html", HTTP_GET, handle_index, nullptr},
      {"/style.css", HTTP_GET, handle_css, nullptr},
      {"/app.js", HTTP_GET, handle_js, nullptr},
      {"/api/status", HTTP_GET, handle_status, nullptr},
      {"/api/settings", HTTP_GET, handle_settings_get, nullptr},
      {"/api/settings", HTTP_PUT, handle_settings_put, nullptr},
      {"/api/screens", HTTP_GET, handle_screens, nullptr},
      {"/api/display", HTTP_POST, handle_display, nullptr},
      {"/api/refresh", HTTP_POST, handle_refresh, nullptr},
      {"/api/wifi/scan", HTTP_GET, handle_wifi_scan, nullptr},
      {"/api/wifi", HTTP_POST, handle_wifi_post, nullptr},
      {"/api/geocode", HTTP_GET, handle_geocode, nullptr},
      {"/api/ota", HTTP_POST, handle_ota, nullptr},
      {"/api/reboot", HTTP_POST, handle_reboot, nullptr},
      {"/api/gpio/scan", HTTP_GET, handle_gpio_scan, nullptr},
      {"/*", HTTP_GET, handle_catch_all, nullptr},
  };
  for (const auto& route : routes) httpd_register_uri_handler(server, &route);

  ESP_LOGI(kTag, "control panel on http://%s/", net::wifi_ip());
}

}  // namespace server

