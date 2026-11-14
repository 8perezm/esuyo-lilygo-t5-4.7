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

  int refresh_min;     // data refresh period while awake
  int sleep_after_s;   // idle time before deep sleep
  bool fahrenheit;
};

namespace settings {

void init();
Settings& get();
void save();
void defaults();

// Helpers for the fixed-size string fields.
void set_str(char* dst, int cap, const char* src);
bool add_symbol(char (*list)[12], int* count, int cap, const char* sym);
bool remove_symbol(char (*list)[12], int* count, const char* sym);

}  // namespace settings
