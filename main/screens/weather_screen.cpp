#include "screens/weather_screen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/settings.h"
#include "data/text.h"
#include "data/weather.h"
#include "display/ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/wifi.h"
#include "screens/common.h"

namespace {

constexpr const char* kTag = "weather-ui";

const char* condition_text(int code) {
  if (code == 0) return "CLEAR SKY";
  if (code == 1) return "MAINLY CLEAR";
  if (code == 2) return "PARTLY CLOUDY";
  if (code == 3) return "OVERCAST";
  if (code == 45 || code == 48) return "FOG";
  if (code >= 51 && code <= 57) return "DRIZZLE";
  if (code >= 61 && code <= 67) return "RAIN";
  if (code >= 71 && code <= 77) return "SNOW";
  if (code >= 80 && code <= 82) return "SHOWERS";
  if (code >= 95) return "THUNDERSTORM";
  return "CLOUDY";
}

}  // namespace

void WeatherScreen::build(lv_obj_t* root) {
  char subtitle[24] = "5-DAY FORECAST";
  lv_obj_t* clock = nullptr;
  lv_obj_t* date = nullptr;
  const Settings& s = settings::get();
  char city[sizeof(s.cities[0].name)] = "";
  data::clean_text(s.cities[s.city_index].name, city, sizeof(city));
  title_ = screens::header(root, city, subtitle, &clock, &date);
  screens::fit_title(title_, 520);
  screens::rule(root, 100);

  // Today, emphasised: a light card on the left third.
  ui::panel(root, 28, 112, 396, 372, screens::kCard);
  ui::label(root, &font_roboto_bold_28, screens::kGrey, "TODAY", 52, 116);
  icon_ = ui::image(root, icon_get("sun", 48), 52, 156);
  temp_ = ui::label(root, &font_roboto_bold_96, screens::kInk, "--", 118, 144);
  unit_ = ui::label(root, &font_roboto_medium_28, screens::kGrey, "C", 300, 208);
  cond_ = ui::label(root, &font_roboto_medium_28, screens::kInk, "--", 52, 262);
  feels_ = ui::label(root, &font_roboto_medium_28, screens::kGrey, "FEELS --", 52, 296);

  // One stat per row: at 24 px three side-by-side columns no longer fit.
  static const char* labels[3] = {"WIND", "RAIN", "HUMIDITY"};
  for (int i = 0; i < 3; i++) {
    int y = 340 + i * 40;
    ui::label(root, &font_roboto_bold_28, screens::kGrey, labels[i], 52, y);
    lv_obj_t* value = ui::label(root, &font_roboto_medium_28, screens::kInk, "--", 200, y);
    lv_obj_set_width(value, 200);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    stats_[i] = value;
  }

  // Four following days. Stop at x=856: the top-right of the glass is
  // covered by the connector tab, so text must not run under it.
  for (int i = 0; i < 4; i++) {
    int x = 436 + i * 126;
    ui::panel(root, x, 112, 118, 372, 0xFFFFFF);
    lv_obj_t* border = lv_obj_create(root);
    lv_obj_remove_style_all(border);
    lv_obj_set_pos(border, x, 112);
    lv_obj_set_size(border, 118, 372);
    lv_obj_set_style_border_width(border, 2, 0);
    lv_obj_set_style_border_color(border, lv_color_hex(screens::kHairline), 0);
    lv_obj_clear_flag(border, LV_OBJ_FLAG_SCROLLABLE);

    day_label_[i] = ui::label(root, &font_roboto_bold_28, screens::kInk, "--", x, 122);
    lv_obj_set_width(day_label_[i], 118);
    lv_obj_set_style_text_align(day_label_[i], LV_TEXT_ALIGN_CENTER, 0);

    day_icon_[i] = ui::image(root, nullptr, x + 35, 168);

    day_max_[i] = ui::label(root, &font_roboto_bold_48, screens::kInk, "--", x, 232);
    lv_obj_set_width(day_max_[i], 118);
    lv_obj_set_style_text_align(day_max_[i], LV_TEXT_ALIGN_CENTER, 0);

    day_min_[i] = ui::label(root, &font_roboto_medium_28, screens::kGrey, "--", x, 288);
    lv_obj_set_width(day_min_[i], 118);
    lv_obj_set_style_text_align(day_min_[i], LV_TEXT_ALIGN_CENTER, 0);

    day_rain_[i] = ui::label(root, &font_roboto_medium_28, screens::kGrey, "--", x, 334);
    lv_obj_set_width(day_rain_[i], 118);
    lv_obj_set_style_text_align(day_rain_[i], LV_TEXT_ALIGN_CENTER, 0);
  }

  screens::footer(root, "< SCREEN   CITY >   HOLD > SLEEP", &updated_);
}

bool WeatherScreen::load() {
  Settings& s = settings::get();
  if (s.city_count == 0) return false;
  if (s.city_index < 0 || s.city_index >= s.city_count) s.city_index = 0;

  // Without a station connection there is nothing to reach: skip the TLS
  // attempt (it only burns ~1.5 s and internal RAM) and keep what we have.
  if (!net::wifi_connected()) {
    apply();
    return false;
  }

  data::Weather fresh = w_;
  bool ok = weather::fetch(s.cities[s.city_index], s.fahrenheit, fresh);
  if (!ok) {
    // One transient TLS failure should not replace a good screen with NO DATA.
    ESP_LOGW(kTag, "fetch failed, retrying once");
    vTaskDelay(pdMS_TO_TICKS(1500));
    fresh = w_;
    ok = weather::fetch(s.cities[s.city_index], s.fahrenheit, fresh);
  }
  if (!ok) {
    ESP_LOGW(kTag, "fetch failed twice, keeping previous");
    if (w_.valid) w_.stale = true;
    apply();
    return false;
  }
  w_ = fresh;
  apply();
  return true;
}

void WeatherScreen::apply() {
  if (temp_ == nullptr) return;
  Settings& s = settings::get();
  const char* unit = s.fahrenheit ? "F" : "C";

  char buf[48];
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", w_.valid ? w_.temp : 0);
  ui::set_text(temp_, w_.valid ? buf : "--");
  ui::set_text(unit_, unit);
  // Keep the unit against the number whatever its width ("8" vs "-12").
  lv_obj_update_layout(temp_);
  ui::place(unit_, lv_obj_get_x(temp_) + lv_obj_get_width(temp_) + 8, 208);

  const bool online = net::wifi_connected();
  ui::set_text(cond_, w_.valid ? condition_text(w_.code) : (online ? "NO DATA" : "NO WI-FI"));
  snprintf(buf, sizeof(buf), "FEELS %d\xC2\xB0", w_.feels);
  ui::set_text(feels_, w_.valid ? buf : (online ? "FEELS --" : "SETUP HOTSPOT"));

  const lv_image_dsc_t* icon = icon_get(weather::icon_for_code(w_.code), 48);
  if (icon_ != nullptr && icon != nullptr) lv_image_set_src(icon_, icon);

  snprintf(buf, sizeof(buf), "%d KM/H", w_.wind);
  ui::set_text(stats_[0], w_.valid ? buf : "--");
  snprintf(buf, sizeof(buf), "%d%%", w_.rain_prob);
  ui::set_text(stats_[1], w_.valid ? buf : "--");
  snprintf(buf, sizeof(buf), "%d%%", w_.humidity);
  ui::set_text(stats_[2], w_.valid ? buf : "--");

  for (int i = 0; i < 4; i++) {
    int idx = i + 1;
    if (w_.valid && idx < w_.day_count) {
      const data::WeatherDay& d = w_.days[idx];
      ui::set_text(day_label_[i], d.label);
      const lv_image_dsc_t* di = icon_get(weather::icon_for_code(d.code), 48);
      if (day_icon_[i] != nullptr && di != nullptr) lv_image_set_src(day_icon_[i], di);
      snprintf(buf, sizeof(buf), "%d\xC2\xB0", d.tmax);
      ui::set_text(day_max_[i], buf);
      snprintf(buf, sizeof(buf), "%d\xC2\xB0", d.tmin);
      ui::set_text(day_min_[i], buf);
      snprintf(buf, sizeof(buf), "%d%%", d.rain);
      ui::set_text(day_rain_[i], buf);
    } else {
      ui::set_text(day_label_[i], "--");
      ui::set_text(day_max_[i], "--");
      ui::set_text(day_min_[i], "");
      ui::set_text(day_rain_[i], "");
    }
  }

  if (updated_ != nullptr) {
    if (w_.valid) {
      snprintf(buf, sizeof(buf), "%s%s", w_.stale ? "STALE " : "UPDATED ", w_.updated);
      ui::set_text(updated_, buf);
    } else if (online) {
      ui::set_text(updated_, "");
    } else {
      snprintf(buf, sizeof(buf), "JOIN \"%s\" AT 192.168.4.1", net::ap_ssid());
      ui::set_text(updated_, buf);
    }
  }
  if (title_ != nullptr) {
    // Only the characters the panel fonts can draw (a city added from the web
    // panel may carry an accent), sized to stay clear of the clock.
    char name[sizeof(s.cities[0].name)] = "";
    data::clean_text(s.cities[s.city_index].name, name, sizeof(name));
    ui::set_text(title_, name);
    screens::fit_title(title_, 520);
  }
}

void WeatherScreen::cycle_city(int dir) {
  Settings& s = settings::get();
  if (s.city_count <= 0) return;
  s.city_index = (s.city_index + dir + s.city_count) % s.city_count;
  settings::save();
  apply();
}

void WeatherScreen::on_action(int dir) {
  if (dir == 0) return;
  cycle_city(dir);
}
