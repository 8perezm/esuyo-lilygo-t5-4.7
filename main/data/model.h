#pragma once

#include <cstdint>

// Plain data shared between providers and screens. No LVGL, no HTTP here.
namespace data {

struct WeatherDay {
  char label[6] = {};  // "MON", "TUE", or "TODAY"
  int16_t code = 0;
  int8_t tmax = 0;
  int8_t tmin = 0;
  int8_t rain = 0;     // precipitation probability, %
};

struct Weather {
  bool valid = false;
  bool stale = false;
  char city[32] = {};
  int16_t temp = 0;    // degrees C, rounded
  int16_t feels = 0;
  int16_t high = 0;
  int16_t low = 0;
  int8_t humidity = 0;
  int16_t wind = 0;    // km/h
  int8_t rain_prob = 0;
  int8_t precip_tenths = 0;  // mm * 10
  int16_t code = 0;          // WMO weather code
  char updated[6] = {};      // "14:05"
  WeatherDay days[5];
  int day_count = 0;
};

struct Quote {
  char symbol[16] = {};
  char name[24] = {};
  float price = 0;
  float change_pct = 0;
  float spark[5] = {};
  int spark_count = 0;
  int decimals = 2;
  bool valid = false;
};

struct QuoteSet {
  bool valid = false;
  bool stale = false;
  char updated[6] = {};
  char currency[8] = "USD";
  int count = 0;
  Quote items[8];
};

}  // namespace data
