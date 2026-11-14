#include "screens/common.h"

#include <cmath>
#include <cstdio>

#include "display/ui.h"
#include "net/wifi.h"

namespace screens {

lv_obj_t* header(lv_obj_t* root, const char* title, const char* subtitle, lv_obj_t** clock,
                 lv_obj_t** date) {
  lv_obj_t* title_label = ui::label(root, &font_roboto_bold_48, kInk, title, 28, 10);
  if (subtitle != nullptr && *subtitle != '\0') {
    ui::label(root, &font_roboto_16, kGrey, subtitle, 30, 70);
  }
  if (clock != nullptr) {
    char buf[8] = "--:--";
    net::format_clock(buf, sizeof(buf));
    *clock = ui::label(root, &font_roboto_medium_20, kInk, buf, 700, 24);
    lv_obj_set_width(*clock, 232);
    lv_obj_set_style_text_align(*clock, LV_TEXT_ALIGN_RIGHT, 0);
  }
  if (date != nullptr) {
    char buf[16] = "";
    net::format_date(buf, sizeof(buf));
    *date = ui::label(root, &font_roboto_16, kGrey, buf, 700, 54);
    lv_obj_set_width(*date, 232);
    lv_obj_set_style_text_align(*date, LV_TEXT_ALIGN_RIGHT, 0);
  }
  return title_label;
}

void rule(lv_obj_t* root, int y) { ui::panel(root, 28, y, 904, 2, kHairline); }

void footer(lv_obj_t* root, const char* hint, lv_obj_t** updated) {
  ui::label(root, &font_roboto_14, kFaint, hint, 28, 504);
  if (updated != nullptr) {
    *updated = ui::label(root, &font_roboto_14, kFaint, "", 640, 504);
    lv_obj_set_width(*updated, 292);
    lv_obj_set_style_text_align(*updated, LV_TEXT_ALIGN_RIGHT, 0);
  }
}

void centered(lv_obj_t* parent, const lv_font_t* font, uint32_t colour, const char* text, int x,
              int w, int y) {
  lv_obj_t* obj = ui::label(parent, font, colour, text, x, y);
  lv_obj_set_width(obj, w);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
}

void right_text(lv_obj_t* parent, const lv_font_t* font, uint32_t colour, const char* text,
                int x, int w, int y) {
  lv_obj_t* obj = ui::label(parent, font, colour, text, x, y);
  lv_obj_set_width(obj, w);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, 0);
}

void format_price(char* out, size_t cap, float value, int decimals) {
  if (value >= 10000) {
    snprintf(out, cap, "%.0f", value);
  } else if (value >= 100) {
    snprintf(out, cap, "%.1f", value);
  } else {
    snprintf(out, cap, "%.*f", decimals, value);
  }
}

void format_change(char* out, size_t cap, float pct) {
  if (std::isnan(pct)) {
    snprintf(out, cap, "--");
  } else {
    snprintf(out, cap, "%+.2f%%", pct);
  }
}

}  // namespace screens
