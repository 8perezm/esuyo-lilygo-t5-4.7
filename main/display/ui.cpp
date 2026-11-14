#include "display/ui.h"

#include <cstring>

#include "board.h"
#include "display/display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {

constexpr const char* kTag = "ui";

uint8_t* g_canvas = nullptr;  // 8-bit luminance, one byte per pixel
lv_display_t* g_display = nullptr;
lv_obj_t* g_root = nullptr;

uint32_t tick_cb() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// LVGL hands us a full-screen L8 buffer; epdiy wants 4 bpp with the even pixel
// in the high nibble.
void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  (void)area;
  uint8_t* dst = display::fb();
  const int n = board::kPanelWidth * board::kPanelHeight;
  if (dst != nullptr && px_map != nullptr) {
    for (int i = 0; i < n; i += 2) {
      uint8_t hi = px_map[i] >> 4;
      uint8_t lo = px_map[i + 1] >> 4;
      dst[i >> 1] = static_cast<uint8_t>((hi << 4) | lo);
    }
  }
  lv_display_flush_ready(disp);
}

}  // namespace

namespace ui {

bool begin() {
  const size_t pixels = static_cast<size_t>(board::kPanelWidth) * board::kPanelHeight;
  g_canvas = static_cast<uint8_t*>(heap_caps_malloc(pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_canvas == nullptr) {
    ESP_LOGE(kTag, "no PSRAM for the LVGL canvas (%u bytes)", static_cast<unsigned>(pixels));
    return false;
  }
  memset(g_canvas, 0xFF, pixels);

  lv_init();
  lv_tick_set_cb(tick_cb);

  g_display = lv_display_create(board::kPanelWidth, board::kPanelHeight);
  lv_display_set_color_format(g_display, LV_COLOR_FORMAT_L8);
  lv_display_set_buffers(g_display, g_canvas, nullptr, pixels, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(g_display, flush_cb);

  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  g_root = lv_obj_create(screen);
  lv_obj_remove_style_all(g_root);
  lv_obj_set_pos(g_root, 0, 0);
  lv_obj_set_size(g_root, board::kPanelWidth, board::kPanelHeight);
  lv_obj_set_style_bg_color(g_root, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
  return true;
}

lv_obj_t* root() { return g_root; }

void clear() {
  if (g_root != nullptr) lv_obj_clean(g_root);
}

void present() {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(g_display);
  display::refresh(MODE_GC16);
}

lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t colour) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_hex(colour), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t colour,
                const char* text, int x, int y) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(colour), 0);
  lv_obj_set_style_text_letter_space(obj, 0, 0);
  lv_label_set_text(obj, text != nullptr ? text : "");
  lv_obj_set_pos(obj, x, y);
  return obj;
}

lv_obj_t* image(lv_obj_t* parent, const lv_image_dsc_t* img, int x, int y) {
  if (img == nullptr) return nullptr;
  lv_obj_t* obj = lv_image_create(parent);
  lv_image_set_src(obj, img);
  lv_obj_set_pos(obj, x, y);
  return obj;
}

void place(lv_obj_t* obj, int x, int y) {
  if (obj != nullptr) lv_obj_set_pos(obj, x, y);
}

void resize(lv_obj_t* obj, int w, int h) {
  if (obj != nullptr) lv_obj_set_size(obj, w, h);
}

void set_text(lv_obj_t* obj, const char* text) {
  if (obj != nullptr) lv_label_set_text(obj, text != nullptr ? text : "");
}

}  // namespace ui
