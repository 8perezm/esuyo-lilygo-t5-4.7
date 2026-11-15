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
    ui::label(root, &font_roboto_medium_28, kGrey, subtitle, 30, 64);
  }
  if (clock != nullptr) {
    char buf[8] = "--:--";
    net::format_clock(buf, sizeof(buf));
    // Keep clear of the connector tab at the top right of the glass.
    *clock = ui::label(root, &font_roboto_medium_28, kInk, buf, 560, 16);
    lv_obj_set_width(*clock, 372);
    lv_obj_set_style_text_align(*clock, LV_TEXT_ALIGN_RIGHT, 0);
  }
  if (date != nullptr) {
    char buf[16] = "";
    net::format_date(buf, sizeof(buf));
    *date = ui::label(root, &font_roboto_medium_28, kGrey, buf, 560, 48);
    lv_obj_set_width(*date, 372);
    lv_obj_set_style_text_align(*date, LV_TEXT_ALIGN_RIGHT, 0);
  }
  return title_label;
}

void rule(lv_obj_t* root, int y) { ui::panel(root, 28, y, 904, kRuleHeight, kHairline); }

void footer(lv_obj_t* root, const char* hint, lv_obj_t** updated) {
  ui::label(root, &font_roboto_medium_28, kFaint, hint, 28, 500);
  if (updated != nullptr) {
    *updated = ui::label(root, &font_roboto_medium_28, kFaint, "", 560, 500);
    lv_obj_set_width(*updated, 372);
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

lv_obj_t* body_text(lv_obj_t* root, const char* text) {
  lv_obj_t* obj = ui::label(root, &font_roboto_bold_48, kInk, text, 48, 132);
  lv_obj_set_width(obj, 864);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(obj, 10, 0);
  return obj;
}

int fit_body(lv_obj_t* label, int max_bottom) {
  if (label == nullptr) return 132;
  // Largest first: short quotes land on 48 px, the common 100-200 character
  // ones on 36 px, and only a very long joke drops to 28/24.
  static const lv_font_t* kSizes[] = {&font_roboto_bold_48, &font_roboto_bold_36,
                                      &font_roboto_bold_28, &font_roboto_medium_24};
  for (const lv_font_t* font : kSizes) {
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_update_layout(label);
    if (lv_obj_get_y(label) + lv_obj_get_height(label) <= max_bottom) break;
  }
  return lv_obj_get_y(label) + lv_obj_get_height(label);
}

void refresh_chrome(lv_obj_t* clock, lv_obj_t* date) {
  if (clock != nullptr) {
    char buf[8] = "--:--";
    net::format_clock(buf, sizeof(buf));
    ui::set_text(clock, buf);
  }
  if (date != nullptr) {
    char buf[16] = "";
    net::format_date(buf, sizeof(buf));
    ui::set_text(date, buf);
  }
}

void fit_title(lv_obj_t* label, int max_width) {  if (label == nullptr) return;
  static const lv_font_t* kSizes[] = {&font_roboto_bold_48, &font_roboto_bold_36,
                                      &font_roboto_bold_28, &font_roboto_medium_24};
  const char* text = lv_label_get_text(label);
  if (text == nullptr || *text == '\0') return;
  for (const lv_font_t* font : kSizes) {
    lv_obj_set_style_text_font(label, font, 0);
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= max_width) break;
  }
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
