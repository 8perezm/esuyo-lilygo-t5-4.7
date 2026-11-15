#pragma once

#include <cstddef>

#include "assets/fonts.h"
#include "assets/icons.h"
#include "lvgl.h"

namespace screens {

constexpr uint32_t kInk = 0x171717;
// E-paper is low contrast: light greys read as ghosts from arm's length.
constexpr uint32_t kGrey = 0x1F1F1F;
constexpr uint32_t kFaint = 0x2B2B2B;
// Rules and outlines need to survive the panel's low contrast at arm's length:
// the old 0x7A756C and 1-2 px drew as a faint grey ghost.
constexpr uint32_t kHairline = 0x1F1F1F;
constexpr int kRuleHeight = 3;
constexpr int kDividerHeight = 2;
constexpr uint32_t kCard = 0xEBE8E1;

// Page chrome shared by every dashboard. Returns the title label.
lv_obj_t* header(lv_obj_t* root, const char* title, const char* subtitle, lv_obj_t** clock,
                 lv_obj_t** date);
void rule(lv_obj_t* root, int y);
void footer(lv_obj_t* root, const char* hint, lv_obj_t** updated);

void centered(lv_obj_t* parent, const lv_font_t* font, uint32_t colour, const char* text, int x,
              int w, int y);
void right_text(lv_obj_t* parent, const lv_font_t* font, uint32_t colour, const char* text,
                int x, int w, int y);

// Big wrapped copy for the quote/joke screens: created at a fixed position and
// width, then shrunk from 48 px until it fits above `max_bottom` (the author
// line sits just under the returned y).
lv_obj_t* body_text(lv_obj_t* root, const char* text);
int fit_body(lv_obj_t* label, int max_bottom);

// Shrink a header title until it fits `max_width`. A geocoded city name such as
// "Santa Cruz de Tenerife · Canary Islands" would otherwise run under the clock
// and off the glass.
void fit_title(lv_obj_t* label, int max_width);

void format_price(char* out, size_t cap, float value, int decimals);
void format_change(char* out, size_t cap, float pct);

}  // namespace screens

