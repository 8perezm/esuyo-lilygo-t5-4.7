#include "app/ui_state.h"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"

namespace {

constexpr const char* kTag = "ui";
constexpr const char* kNamespace = "ui";
constexpr const char* kKeyScreen = "screen";
constexpr const char* kKeyMarket = "market";

char g_screen[24] = "weather";
int g_market_kind = 0;

void write() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, kKeyScreen, g_screen);
  nvs_set_i32(h, kKeyMarket, g_market_kind);
  nvs_commit(h);
  nvs_close(h);
}

}  // namespace

namespace ui_state {

void init() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) {
    ESP_LOGI(kTag, "no saved screen, starting on weather");
    return;
  }

  size_t len = sizeof(g_screen);
  if (nvs_get_str(h, kKeyScreen, g_screen, &len) != ESP_OK) {
    strlcpy(g_screen, "weather", sizeof(g_screen));
  }

  int32_t kind = 0;
  if (nvs_get_i32(h, kKeyMarket, &kind) == ESP_OK && kind >= 0 && kind < 3) {
    g_market_kind = kind;
  }
  nvs_close(h);

  ESP_LOGI(kTag, "restored screen '%s', market %d", g_screen, g_market_kind);
}

const char* screen_id() { return g_screen; }

int market_kind() { return g_market_kind; }

void set_screen_id(const char* id) {
  if (id == nullptr || *id == '\0' || strcmp(id, g_screen) == 0) return;
  strlcpy(g_screen, id, sizeof(g_screen));
  write();
  ESP_LOGI(kTag, "saved screen '%s'", g_screen);
}

void set_market_kind(int kind) {
  if (kind == g_market_kind) return;
  g_market_kind = kind;
  write();
  ESP_LOGI(kTag, "saved market %d", g_market_kind);
}

}  // namespace ui_state
