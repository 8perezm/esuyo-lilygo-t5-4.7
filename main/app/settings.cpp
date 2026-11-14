#include "app/settings.h"

#include <cstring>
#include <strings.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {

constexpr const char* kTag = "settings";
constexpr const char* kNamespace = "cfg";
constexpr const char* kKey = "dashboard";
constexpr uint32_t kMagic = 0x54354701;  // 'T5G' + layout version 1

struct Blob {
  uint32_t magic;
  uint32_t version;
  Settings s;
};

Settings g_settings;

void copy(char* dst, size_t cap, const char* src) {
  if (src == nullptr) src = "";
  strlcpy(dst, src, cap);
}

}  // namespace

namespace settings {

void set_str(char* dst, int cap, const char* src) {
  copy(dst, static_cast<size_t>(cap), src);
}

void defaults() {
  memset(&g_settings, 0, sizeof(g_settings));

  g_settings.cities[0] = {"Basingstoke", 51.2667f, -1.0833f};
  g_settings.cities[1] = {"London", 51.5074f, -0.1278f};
  g_settings.cities[2] = {"New York", 40.7128f, -74.0060f};
  g_settings.cities[3] = {"Tokyo", 35.6762f, 139.6503f};
  g_settings.cities[4] = {"Sydney", -33.8688f, 151.2093f};
  g_settings.city_count = 5;
  g_settings.city_index = 0;

  const char* coins[] = {"bitcoin", "ethereum", "solana", "ripple", "cardano"};
  for (auto c : coins) add_symbol(g_settings.coins, &g_settings.coin_count, 8, c);

  const char* indices[] = {"^GSPC", "^IXIC", "^DJI", "^FTSE", "^GDAXI"};
  for (auto s : indices) add_symbol(g_settings.indices, &g_settings.index_count, 8, s);

  const char* commodities[] = {"GC=F", "SI=F", "NG=F", "CL=F"};
  for (auto s : commodities) add_symbol(g_settings.commodities, &g_settings.commodity_count, 8, s);

  g_settings.refresh_min = 15;
  g_settings.sleep_after_s = 120;
  g_settings.fahrenheit = false;
}

bool add_symbol(char (*list)[12], int* count, int cap, const char* sym) {
  if (sym == nullptr || *sym == '\0' || *count >= cap) return false;
  for (int i = 0; i < *count; i++) {
    if (strcasecmp(list[i], sym) == 0) return true;
  }
  copy(list[*count], 12, sym);
  (*count)++;
  return true;
}

bool remove_symbol(char (*list)[12], int* count, const char* sym) {
  for (int i = 0; i < *count; i++) {
    if (strcasecmp(list[i], sym) == 0) {
      for (int j = i; j < *count - 1; j++) memcpy(list[j], list[j + 1], 12);
      (*count)--;
      list[*count][0] = '\0';
      return true;
    }
  }
  return false;
}

void init() {
  defaults();

  nvs_handle_t h;
  esp_err_t open_err = nvs_open(kNamespace, NVS_READONLY, &h);
  if (open_err != ESP_OK) {
    ESP_LOGW(kTag, "no settings namespace yet (%s), using defaults",
             esp_err_to_name(open_err));
    return;
  }

  Blob blob{};
  size_t len = sizeof(blob);
  esp_err_t err = nvs_get_blob(h, kKey, &blob, &len);
  nvs_close(h);

  if (err == ESP_OK && len == sizeof(blob) && blob.magic == kMagic &&
      blob.version == 1 && blob.s.city_count >= 0 && blob.s.city_count <= 8) {
    g_settings = blob.s;
    ESP_LOGI(kTag, "loaded settings (%d cities, %d coins)", g_settings.city_count,
             g_settings.coin_count);
  } else {
    ESP_LOGW(kTag, "no stored settings, using defaults");
  }
}

Settings& get() { return g_settings; }

void save() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_open: %s", esp_err_to_name(err));
    return;
  }
  Blob blob{kMagic, 1, g_settings};
  err = nvs_set_blob(h, kKey, &blob, sizeof(blob));
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "save (%u bytes): %s", static_cast<unsigned>(sizeof(blob)),
             esp_err_to_name(err));
  } else {
    ESP_LOGI(kTag, "saved (%u bytes, ssid '%s')", static_cast<unsigned>(sizeof(blob)),
             g_settings.ssid);
  }
}

}  // namespace settings
