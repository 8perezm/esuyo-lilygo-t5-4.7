#include "app/app.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/power.h"
#include "app/settings.h"
#include "cJSON.h"
#include "display/display.h"
#include "display/ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input/buttons.h"
#include "input/gpio_probe.h"
#include "net/http_server.h"
#include "net/serial_cli.h"
#include "net/wifi.h"
#include "nvs_flash.h"
#include "screens/market_screen.h"
#include "screens/screen.h"
#include "screens/status_screen.h"
#include "screens/weather_screen.h"

namespace {

constexpr const char* kTag = "app";

WeatherScreen g_weather;
MarketScreen g_crypto(MarketScreen::Kind::Crypto, "crypto", "CRYPTO");
MarketScreen g_indices(MarketScreen::Kind::Indices, "indices", "INDICES");
MarketScreen g_commodities(MarketScreen::Kind::Commodities, "commodities", "COMMODITIES");
StatusScreen g_status;

Screen* g_screens[] = {&g_weather, &g_crypto, &g_indices, &g_commodities, &g_status};
constexpr int kScreenCount = sizeof(g_screens) / sizeof(g_screens[0]);

int g_current = 0;
int64_t g_last_activity = 0;
int64_t g_last_refresh = 0;

volatile bool g_pending_screen = false;
volatile bool g_pending_refresh = false;
volatile bool g_pending_repair = false;
char g_pending_id[24] = {};
bool g_wifi_changed = false;

int find_screen(const char* id) {
  for (int i = 0; i < kScreenCount; i++) {
    if (strcmp(g_screens[i]->id(), id) == 0) return i;
  }
  return -1;
}

void draw_current(bool reload, bool wipe_first) {
  // A page turn starts from a known-white panel: a diff against the previous
  // screen would otherwise leave every pixel the new screen does not touch.
  if (wipe_first) display::wipe();
  ui::clear();
  Screen* s = g_screens[g_current];
  s->build(ui::root());
  if (reload) s->load();
  ui::present();
  g_last_refresh = esp_timer_get_time();
  g_last_activity = g_last_refresh;
}

void show(int index, bool reload) {
  if (index < 0) index = kScreenCount - 1;
  if (index >= kScreenCount) index = 0;
  g_current = index;
  ESP_LOGI(kTag, "screen %s", g_screens[g_current]->id());
  draw_current(reload, true);
}

void refresh_active() {
  Screen* s = g_screens[g_current];
  if (s->load()) {
    ui::present();
  }
  g_last_refresh = esp_timer_get_time();
}

void report_heap(const char* when) {
  ESP_LOGI(kTag, "heap %s: internal free %u, largest %u, psram free %u", when,
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

int sleep_minutes() {
  int m = settings::get().refresh_min;
  if (m < 30) m = 30;
  return m;
}

}  // namespace

namespace app {

void run() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  settings::init();
  ESP_ERROR_CHECK(display::init() ? ESP_OK : ESP_FAIL);
  report_heap("after display");
  if (!ui::begin()) {
    ESP_LOGE(kTag, "UI init failed");
  }
  buttons::begin();
  power::init();
  probe::start();
  net::serial_cli_start();

  const bool online = net::wifi_start();
  if (online) {
    net::time_sync_start();
    // HTTPS certificate verification fails while the clock is still at 1970
    // ("certificate is not yet valid"), so give SNTP a moment before the first
    // fetch. Typical sync is well under a second.
    int waited = 0;
    while (!net::time_valid() && waited < 8000) {
      vTaskDelay(pdMS_TO_TICKS(200));
      waited += 200;
    }
    ESP_LOGI(kTag, "clock %s after %d ms", net::time_valid() ? "synced" : "NOT synced",
             waited);
  }
  server::start();
  report_heap("after server");

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  ESP_LOGW(kTag, "boot: wake cause %d, wifi %s", cause, online ? "up" : "setup AP");

  // The panel keeps its last image through sleep, so only the data is stale.
  show(g_current, true);

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    ESP_LOGI(kTag, "timer wake: refresh done, back to sleep");
    power::sleep_now(sleep_minutes());
  }

  const int idle_s = settings::get().sleep_after_s;
  const int refresh_s = (settings::get().refresh_min > 0 ? settings::get().refresh_min : 15) * 60;

  for (;;) {
    if (g_pending_repair) {
      g_pending_repair = false;
      display::repair();
      draw_current(true, false);
      g_last_activity = esp_timer_get_time();
    }

    if (g_pending_screen) {
      g_pending_screen = false;
      int idx = find_screen(g_pending_id);
      if (idx >= 0) show(idx, true);
      g_last_activity = esp_timer_get_time();
    }

    if (g_pending_refresh) {
      g_pending_refresh = false;
      refresh_active();
      g_last_activity = esp_timer_get_time();
    }

    BtnEvent ev{};
    Btn btn{};
    if (buttons::wait(&ev, &btn, 500)) {
      g_last_activity = esp_timer_get_time();
      ESP_LOGI(kTag, "button %d event %d", static_cast<int>(btn), static_cast<int>(ev));

      if (ev == BtnEvent::Long) {
        if (btn == Btn::Right) power::sleep_now(sleep_minutes());
        refresh_active();
        continue;
      }

      if (btn == Btn::Left) {
        show(g_current - 1, true);
      } else if (btn == Btn::Right) {
        show(g_current + 1, true);
      } else {
        g_screens[g_current]->on_action(1);
        refresh_active();
      }
      continue;
    }

    const int64_t now = esp_timer_get_time();
    if (idle_s > 0 && (now - g_last_activity) > static_cast<int64_t>(idle_s) * 1000000) {
      power::sleep_now(sleep_minutes());
    }
    if (refresh_s > 0 && (now - g_last_refresh) > static_cast<int64_t>(refresh_s) * 1000000) {
      refresh_active();
    }
  }
}

const char* current_screen_id() { return g_screens[g_current]->id(); }

int screen_count() { return kScreenCount; }

const char* screen_id(int index) {
  return (index >= 0 && index < kScreenCount) ? g_screens[index]->id() : "";
}

const char* screen_title(int index) {
  return (index >= 0 && index < kScreenCount) ? g_screens[index]->title() : "";
}

void request_screen(const char* id) {
  if (id == nullptr) return;
  strlcpy(g_pending_id, id, sizeof(g_pending_id));
  g_pending_screen = true;
}

void request_refresh() { g_pending_refresh = true; }

void request_repair() { g_pending_repair = true; }

void settings_to_json(char* out, size_t cap) {
  Settings& s = settings::get();
  cJSON* root = cJSON_CreateObject();

  cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
  cJSON_AddStringToObject(wifi, "ssid", s.ssid);
  cJSON_AddBoolToObject(wifi, "configured", s.ssid[0] != '\0');

  cJSON* weather = cJSON_AddObjectToObject(root, "weather");
  cJSON* cities = cJSON_AddArrayToObject(weather, "cities");
  for (int i = 0; i < s.city_count; i++) {
    cJSON* c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "name", s.cities[i].name);
    cJSON_AddNumberToObject(c, "lat", s.cities[i].lat);
    cJSON_AddNumberToObject(c, "lon", s.cities[i].lon);
    cJSON_AddItemToArray(cities, c);
  }
  cJSON_AddNumberToObject(weather, "active", s.city_index);
  cJSON_AddStringToObject(weather, "units", s.fahrenheit ? "f" : "c");

  cJSON* crypto = cJSON_AddObjectToObject(root, "crypto");
  cJSON* ids = cJSON_AddArrayToObject(crypto, "ids");
  for (int i = 0; i < s.coin_count; i++) cJSON_AddItemToArray(ids, cJSON_CreateString(s.coins[i]));

  cJSON* indices = cJSON_AddObjectToObject(root, "indices");
  cJSON* idx = cJSON_AddArrayToObject(indices, "symbols");
  for (int i = 0; i < s.index_count; i++) cJSON_AddItemToArray(idx, cJSON_CreateString(s.indices[i]));

  cJSON* commodities = cJSON_AddObjectToObject(root, "commodities");
  cJSON* com = cJSON_AddArrayToObject(commodities, "symbols");
  for (int i = 0; i < s.commodity_count; i++)
    cJSON_AddItemToArray(com, cJSON_CreateString(s.commodities[i]));

  cJSON_AddNumberToObject(root, "refresh_min", s.refresh_min);
  cJSON_AddNumberToObject(root, "sleep_after_s", s.sleep_after_s);

  char* text = cJSON_PrintUnformatted(root);
  if (text != nullptr) {
    strlcpy(out, text, cap);
    cJSON_free(text);
  } else {
    strlcpy(out, "{}", cap);
  }
  cJSON_Delete(root);
}

bool apply_settings_json(const char* json, size_t len) {
  (void)len;
  cJSON* root = cJSON_Parse(json);
  if (root == nullptr) return false;

  Settings& s = settings::get();

  const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
  const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
  if (cJSON_IsString(ssid)) settings::set_str(s.ssid, sizeof(s.ssid), ssid->valuestring);
  const cJSON* pass = cJSON_GetObjectItemCaseSensitive(wifi, "password");
  if (cJSON_IsString(pass) && pass->valuestring[0] != '\0') {
    settings::set_str(s.pass, sizeof(s.pass), pass->valuestring);
    g_wifi_changed = true;
  }

  const cJSON* weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
  if (cJSON_IsObject(weather)) {
    const cJSON* cities = cJSON_GetObjectItemCaseSensitive(weather, "cities");
    if (cJSON_IsArray(cities)) {
      int n = 0;
      const cJSON* c = nullptr;
      cJSON_ArrayForEach(c, cities) {
        if (n >= 8) break;
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(c, "name");
        const cJSON* lat = cJSON_GetObjectItemCaseSensitive(c, "lat");
        const cJSON* lon = cJSON_GetObjectItemCaseSensitive(c, "lon");
        if (!cJSON_IsString(name) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) continue;
        settings::set_str(s.cities[n].name, sizeof(s.cities[n].name), name->valuestring);
        s.cities[n].lat = static_cast<float>(lat->valuedouble);
        s.cities[n].lon = static_cast<float>(lon->valuedouble);
        n++;
      }
      s.city_count = n;
      if (s.city_index >= n) s.city_index = 0;
    }
    const cJSON* active = cJSON_GetObjectItemCaseSensitive(weather, "active");
    if (cJSON_IsNumber(active)) {
      int a = active->valueint;
      if (a >= 0 && a < s.city_count) s.city_index = a;
    }
    const cJSON* units = cJSON_GetObjectItemCaseSensitive(weather, "units");
    if (cJSON_IsString(units)) s.fahrenheit = strcmp(units->valuestring, "f") == 0;
  }

  auto load_list = [](const cJSON* parent, const char* key, char (*dst)[12], int* count) {
    const cJSON* arr = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!cJSON_IsArray(arr)) return;
    int n = 0;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, arr) {
      if (n >= 8 || !cJSON_IsString(item)) break;
      settings::set_str(dst[n], 12, item->valuestring);
      n++;
    }
    *count = n;
  };

  const cJSON* crypto = cJSON_GetObjectItemCaseSensitive(root, "crypto");
  load_list(crypto, "ids", s.coins, &s.coin_count);
  const cJSON* indices = cJSON_GetObjectItemCaseSensitive(root, "indices");
  load_list(indices, "symbols", s.indices, &s.index_count);
  const cJSON* commodities = cJSON_GetObjectItemCaseSensitive(root, "commodities");
  load_list(commodities, "symbols", s.commodities, &s.commodity_count);

  const cJSON* refresh = cJSON_GetObjectItemCaseSensitive(root, "refresh_min");
  if (cJSON_IsNumber(refresh)) {
    int v = refresh->valueint;
    if (v < 1) v = 1;
    if (v > 1440) v = 1440;
    s.refresh_min = v;
  }
  const cJSON* sleep_after = cJSON_GetObjectItemCaseSensitive(root, "sleep_after_s");
  if (cJSON_IsNumber(sleep_after)) {
    int v = sleep_after->valueint;
    if (v < 0) v = 0;
    if (v > 3600) v = 3600;
    s.sleep_after_s = v;
  }

  settings::save();
  cJSON_Delete(root);
  ESP_LOGI(kTag, "settings updated");
  return true;
}

void reboot() { esp_restart(); }

bool take_wifi_changed() {
  bool changed = g_wifi_changed;
  g_wifi_changed = false;
  return changed;
}

}  // namespace app

