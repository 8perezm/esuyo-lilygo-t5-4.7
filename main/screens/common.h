#pragma once

#include <cstddef>

#include "assets/fonts.h"
#include "assets/icons.h"
#include "lvgl.h"

namespace screens {

constexpr uint32_t kInk = 0x171717;
// E-paper is low contrast: anything lighter than this reads as "ghost".
constexpr uint32_t kGrey = 0x333333;
constexpr uint32_t kFaint = 0x4A4741;
constexpr uint32_t kHairline = 0x8C877D;
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

void format_price(char* out, size_t cap, float value, int decimals);
void format_change(char* out, size_t cap, float pct);

}  // namespace screens

