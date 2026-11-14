#include "data/weather.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "esp_log.h"
#include "net/http_client.h"
#include "net/wifi.h"

namespace {

constexpr const char* kTag = "weather";

int weekday_from_iso(const char* iso) {
  // Zeller's congruence, Gregorian. 0 = Sunday.
  int y = 0, m = 0, d = 0;
  if (iso == nullptr || sscanf(iso, "%4d-%2d-%2d", &y, &m, &d) != 3) return -1;
  if (m < 3) {
    m += 12;
    y -= 1;
  }
  int k = y % 100;
  int j = y / 100;
  int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
  // Zeller: 0 = Saturday. Convert to 0 = Sunday.
  return (h + 1) % 7;
}

double num(const cJSON* obj, const char* key, double fallback = 0) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

double arr(const cJSON* obj, const char* key, int index, double fallback = 0) {
  const cJSON* item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(obj, key), index);
  return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

}  // namespace

namespace weather {

const char* icon_for_code(int code) {
  if (code == 0) return "sun";
  if (code == 1 || code == 2) return "cloud-sun";
  if (code == 3) return "cloud";
  if (code == 45 || code == 48) return "cloud-fog";
  if (code >= 51 && code <= 57) return "cloud-rain";
  if (code >= 61 && code <= 67) return "cloud-rain";
  if (code >= 71 && code <= 77) return "cloud-snow";
  if (code >= 80 && code <= 82) return "cloud-rain";
  if (code >= 85 && code <= 86) return "cloud-snow";
  if (code >= 95) return "cloud-lightning";
  return "cloud";
}

bool fetch(const CityRef& city, bool fahrenheit, data::Weather& out) {
  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast"
           "?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,"
           "weather_code,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "precipitation_probability_max,time"
           "&timezone=auto&forecast_days=5&temperature_unit=%s",
           city.lat, city.lon, fahrenheit ? "fahrenheit" : "celsius");

  std::string body;
  if (!net::http_get(url, body)) {
    out.stale = true;
    return false;
  }

  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr) {
    ESP_LOGW(kTag, "bad JSON (%u bytes)", static_cast<unsigned>(body.size()));
    out.stale = true;
    return false;
  }

  const cJSON* cur = cJSON_GetObjectItemCaseSensitive(root, "current");
  const cJSON* daily = cJSON_GetObjectItemCaseSensitive(root, "daily");

  data::Weather w{};
  w.valid = true;
  w.stale = false;
  strlcpy(w.city, city.name, sizeof(w.city));
  w.temp = static_cast<int16_t>(lround(num(cur, "temperature_2m")));
  w.feels = static_cast<int16_t>(lround(num(cur, "apparent_temperature")));
  w.humidity = static_cast<int8_t>(lround(num(cur, "relative_humidity_2m")));
  w.wind = static_cast<int16_t>(lround(num(cur, "wind_speed_10m")));
  w.precip_tenths = static_cast<int8_t>(lround(num(cur, "precipitation") * 10));
  w.code = static_cast<int16_t>(lround(num(cur, "weather_code")));

  w.day_count = 0;
  for (int i = 0; i < 5 && i < 5; i++) {
    const cJSON* t = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "time"), i);
    if (!cJSON_IsString(t)) break;
    data::WeatherDay& day = w.days[w.day_count];
    if (i == 0) {
      strlcpy(day.label, "TODAY", sizeof(day.label));
    } else {
      static const char* names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
      int wd = weekday_from_iso(t->valuestring);
      strlcpy(day.label, (wd >= 0 && wd < 7) ? names[wd] : "---", sizeof(day.label));
    }
    day.tmax = static_cast<int8_t>(lround(arr(daily, "temperature_2m_max", i)));
    day.tmin = static_cast<int8_t>(lround(arr(daily, "temperature_2m_min", i)));
    day.rain = static_cast<int8_t>(lround(arr(daily, "precipitation_probability_max", i)));
    day.code = static_cast<int16_t>(lround(arr(daily, "weather_code", i)));
    w.day_count++;
  }
  if (w.day_count > 0) {
    w.high = w.days[0].tmax;
    w.low = w.days[0].tmin;
    w.rain_prob = w.days[0].rain;
  }

  net::format_clock(w.updated, sizeof(w.updated));
  cJSON_Delete(root);
  out = w;
  ESP_LOGI(kTag, "%s: %d deg, %s, %d days", w.city, w.temp, icon_for_code(w.code), w.day_count);
  return true;
}

}  // namespace weather
