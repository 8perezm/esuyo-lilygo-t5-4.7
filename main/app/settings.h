#pragma once

#include <cstdint>

struct CityRef {
  char name[28];
  float lat;
  float lon;
};

// Everything the web UI can change. Persisted as one versioned blob in NVS so
// struct changes are detectable instead of silently misread.
struct Settings {
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

  int refresh_min;        // data refresh period while awake
  int sleep_after_s;      // idle time before deep sleep
  int sleep_refresh_min;  // deep-sleep timer: minutes between wake-refresh-sleep cycles
  bool fahrenheit;

  // Which screens the carousel may show. The panel still reports every screen
  // over /api/screens; a disabled one is skipped by the buttons and marked as
  // disabled in the web UI.
  bool show_weather;
  bool show_markets;
  bool show_status;
  bool show_quote;
  bool show_joke;
};

namespace settings {

void init();
Settings& get();
void save();
void defaults();

// Screen toggles by id ("weather", "markets", "quote", "joke", "status").
// Unknown ids read as enabled so a newer web UI cannot hide a screen this
// firmware does not know about.
bool screen_on(const char* id);
void set_screen_on(const char* id, bool on);
bool any_screen_on();

// Helpers for the fixed-size string fields.
void set_str(char* dst, int cap, const char* src);
bool add_symbol(char (*list)[12], int* count, int cap, const char* sym);
bool remove_symbol(char (*list)[12], int* count, const char* sym);

}  // namespace settings
