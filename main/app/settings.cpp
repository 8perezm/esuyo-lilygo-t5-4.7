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
constexpr uint32_t kVersion = 3;

struct Blob {
  uint32_t magic;
  uint32_t version;
  Settings s;
};

// Layout written by firmware 1.x. Kept verbatim so an upgrade migrates the
// stored settings (Wi-Fi credentials above all) instead of falling back to the
// defaults. Do not edit: it must keep matching what that firmware wrote.
struct SettingsV1 {
  char ssid[33];
  char pass[65];

  CityRef cities[8];
  int city_count;
  int city_index;

  char coins[8][12];
  int coin_count;

  char indices[8][12];
  int index_count;

  char commodities[8][12];
  int commodity_count;

  int refresh_min;
  int sleep_after_s;
  bool fahrenheit;
};

struct BlobV1 {
  uint32_t magic;
  uint32_t version;
  SettingsV1 s;
};

// Layout written by the sleep-refresh firmware (v2). Same reasoning as above:
// copy the bytes it wrote rather than guess at the new tail.
struct SettingsV2 {
  char ssid[33];
  char pass[65];

  CityRef cities[8];
  int city_count;
  int city_index;

  char coins[8][12];
  int coin_count;

  char indices[8][12];
  int index_count;

  char commodities[8][12];
  int commodity_count;

  int refresh_min;
  int sleep_after_s;
  int sleep_refresh_min;
  bool fahrenheit;
};

struct BlobV2 {
  uint32_t magic;
  uint32_t version;
  SettingsV2 s;
};

Settings g_settings;

void copy(char* dst, size_t cap, const char* src) {
  if (src == nullptr) src = "";
  strlcpy(dst, src, cap);
}

// strlcpy cuts on a byte boundary, so a long name can end halfway through a
// UTF-8 sequence ("Santa Cruz de Tenerife · Canary" cut inside the ·). The
// fragment is not a character any font can draw, so drop it and the trailing
// space it leaves behind.
void trim_utf8(char* s) {
  size_t len = strlen(s);
  size_t cut = len;
  for (size_t i = 0; i < len;) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = 1;
    if (c < 0x80) {
      n = 1;
    } else if ((c & 0xE0) == 0xC0) {
      n = 2;
    } else if ((c & 0xF0) == 0xE0) {
      n = 3;
    } else if ((c & 0xF8) == 0xF0) {
      n = 4;
    }
    if (i + n > len) {
      cut = i;
      break;
    }
    i += n;
  }
  while (cut > 0 && s[cut - 1] == ' ') cut--;
  s[cut] = '\0';
}

// Names stored by an older build may already end in a half character; trim
// them in memory so the next save persists the repaired value.
void repair_city_names() {
  for (int i = 0; i < g_settings.city_count && i < 8; i++) {
    trim_utf8(g_settings.cities[i].name);
  }
}

bool* toggle_for(const char* id) {
  if (id == nullptr) return nullptr;
  if (strcmp(id, "weather") == 0) return &g_settings.show_weather;
  if (strcmp(id, "markets") == 0) return &g_settings.show_markets;
  if (strcmp(id, "status") == 0) return &g_settings.show_status;
  if (strcmp(id, "quote") == 0) return &g_settings.show_quote;
  if (strcmp(id, "joke") == 0) return &g_settings.show_joke;
  return nullptr;
}

}  // namespace

namespace settings {

void set_str(char* dst, int cap, const char* src) {
  copy(dst, static_cast<size_t>(cap), src);
  trim_utf8(dst);
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
  g_settings.sleep_refresh_min = 30;
  g_settings.fahrenheit = false;

  g_settings.show_weather = true;
  g_settings.show_markets = true;
  g_settings.show_status = true;
  g_settings.show_quote = true;
  g_settings.show_joke = true;
}

bool screen_on(const char* id) {
  const bool* flag = toggle_for(id);
  return flag == nullptr ? true : *flag;
}

void set_screen_on(const char* id, bool on) {
  bool* flag = toggle_for(id);
  if (flag != nullptr) *flag = on;
}

bool any_screen_on() {
  return g_settings.show_weather || g_settings.show_markets || g_settings.show_status ||
         g_settings.show_quote || g_settings.show_joke;
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

  if (err == ESP_OK && len == sizeof(Blob) && blob.magic == kMagic &&
      blob.version == kVersion && blob.s.city_count >= 0 && blob.s.city_count <= 8) {
    g_settings = blob.s;
    repair_city_names();
    ESP_LOGI(kTag, "loaded settings (%d cities, %d coins)", g_settings.city_count,
             g_settings.coin_count);
    return;
  }

  // Sleep-refresh firmware (v2): everything is still valid, only the per-screen
  // toggles are new, so they keep their defaults (all on).
  if (err == ESP_OK && len == sizeof(BlobV2) && blob.magic == kMagic && blob.version == 2) {
    BlobV2 old{};
    memcpy(&old, &blob, sizeof(old));
    if (old.s.city_count < 0 || old.s.city_count > 8) {
      ESP_LOGW(kTag, "v2 settings are corrupt, using defaults");
      return;
    }
    Settings& s = g_settings;  // defaults, so the new toggles start enabled
    memcpy(s.ssid, old.s.ssid, sizeof(s.ssid));
    memcpy(s.pass, old.s.pass, sizeof(s.pass));
    memcpy(s.cities, old.s.cities, sizeof(s.cities));
    s.city_count = old.s.city_count;
    s.city_index = old.s.city_index;
    memcpy(s.coins, old.s.coins, sizeof(s.coins));
    s.coin_count = old.s.coin_count;
    memcpy(s.indices, old.s.indices, sizeof(s.indices));
    s.index_count = old.s.index_count;
    memcpy(s.commodities, old.s.commodities, sizeof(s.commodities));
    s.commodity_count = old.s.commodity_count;
    s.refresh_min = old.s.refresh_min;
    s.sleep_after_s = old.s.sleep_after_s;
    s.sleep_refresh_min = old.s.sleep_refresh_min;
    s.fahrenheit = old.s.fahrenheit;
    repair_city_names();
    ESP_LOGW(kTag, "migrated v2 settings (ssid '%s')", s.ssid);
    save();
    return;
  }

  // Firmware 1.x blob: everything it holds is still valid, only the deep-sleep
  // refresh interval is new. Copy it across and persist the upgraded layout.
  if (err == ESP_OK && len == sizeof(BlobV1) && blob.magic == kMagic && blob.version == 1) {
    BlobV1 old{};
    memcpy(&old, &blob, sizeof(old));
    if (old.s.city_count < 0 || old.s.city_count > 8) {
      ESP_LOGW(kTag, "v1 settings are corrupt, using defaults");
      return;
    }
    Settings& s = g_settings;  // defaults, so the new field has its default
    memcpy(s.ssid, old.s.ssid, sizeof(s.ssid));
    memcpy(s.pass, old.s.pass, sizeof(s.pass));
    memcpy(s.cities, old.s.cities, sizeof(s.cities));
    s.city_count = old.s.city_count;
    s.city_index = old.s.city_index;
    memcpy(s.coins, old.s.coins, sizeof(s.coins));
    s.coin_count = old.s.coin_count;
    memcpy(s.indices, old.s.indices, sizeof(s.indices));
    s.index_count = old.s.index_count;
    memcpy(s.commodities, old.s.commodities, sizeof(s.commodities));
    s.commodity_count = old.s.commodity_count;
    s.refresh_min = old.s.refresh_min;
    s.sleep_after_s = old.s.sleep_after_s;
    s.fahrenheit = old.s.fahrenheit;
    repair_city_names();
    ESP_LOGW(kTag, "migrated v1 settings (ssid '%s', sleep refresh %d min)", s.ssid,
             s.sleep_refresh_min);
    save();
    return;
  }

  ESP_LOGW(kTag, "no stored settings, using defaults");
}

Settings& get() { return g_settings; }

void save() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_open: %s", esp_err_to_name(err));
    return;
  }
  Blob blob{kMagic, kVersion, g_settings};
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
