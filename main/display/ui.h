#pragma once

#include "lvgl.h"

// A thin layer over LVGL 9 that renders into an 8-bit luminance canvas and
// converts it to epdiy's 4 bpp framebuffer. Screens only touch this API, so
// the panel plumbing stays in one place.
namespace ui {

bool begin();

// Container that fills the panel. Screens build into it; switching screens
// deletes it and builds a fresh one.
lv_obj_t* root();
void clear();

// Render everything and push it to the panel (blocking, ~2.5 s in GC16).
void present();

// Convenience constructors with absolute coordinates.
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t colour = 0xFFFFFF);
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t colour,
                const char* text, int x, int y);
lv_obj_t* image(lv_obj_t* parent, const lv_image_dsc_t* img, int x, int y);

void place(lv_obj_t* obj, int x, int y);
void resize(lv_obj_t* obj, int w, int h);
void set_text(lv_obj_t* label, const char* text);

}  // namespace ui
