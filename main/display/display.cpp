#include "display/display.h"

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

namespace {

constexpr const char* kTag = "display";

EpdiyHighlevelState g_hl;
uint8_t* g_fb = nullptr;
int g_width = 0;
int g_height = 0;

}  // namespace

namespace display {

bool init() {
  // The 1K LUT has no GC16/i2s lookup table on the ESP32, so use the 64K one
  // and give Wi-Fi fewer internal buffers instead.
  epd_init(&epd_board_lilygo_t5_47, &ED047TC1, EPD_LUT_64K);
  g_hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
  g_fb = epd_hl_get_framebuffer(&g_hl);
  g_width = epd_width();
  g_height = epd_height();
  if (g_fb == nullptr) {
    ESP_LOGE(kTag, "no framebuffer");
    return false;
  }
  // epdiy stores 0 = black, 255 (nibble 15) = white.
  memset(g_fb, 0xFF, static_cast<size_t>(g_width) * g_height / 2);
  ESP_LOGI(kTag, "panel %dx%d, framebuffer %u bytes", g_width, g_height,
           static_cast<unsigned>(g_width * g_height / 2));
  return true;
}

void power_on() { epd_poweron(); }
void power_off() { epd_poweroff(); }

uint8_t* fb() { return g_fb; }
int width() { return g_width; }
int height() { return g_height; }

void clear() {
  memset(g_fb, 0xFF, static_cast<size_t>(g_width) * g_height / 2);
}

void wipe() {
  int64_t start = esp_timer_get_time();
  epd_poweron();
  epd_clear();
  epd_poweroff();
  const size_t bytes = static_cast<size_t>(g_width) * g_height / 2;
  memset(g_hl.front_fb, 0xFF, bytes);
  memset(g_hl.back_fb, 0xFF, bytes);
  ESP_LOGI(kTag, "panel wiped in %d ms",
           static_cast<int>((esp_timer_get_time() - start) / 1000));
}

void repair() {
  int64_t start = esp_timer_get_time();
  epd_poweron();
  epd_clear_area_cycles(epd_full_screen(), 3, 300);
  epd_poweroff();
  const size_t bytes = static_cast<size_t>(g_width) * g_height / 2;
  memset(g_hl.front_fb, 0xFF, bytes);
  memset(g_hl.back_fb, 0xFF, bytes);
  ESP_LOGW(kTag, "panel repair in %d ms",
           static_cast<int>((esp_timer_get_time() - start) / 1000));
}

void refresh(enum EpdDrawMode mode) {
  int temp = epd_ambient_temperature();
  if (temp <= 0 || temp > 60) temp = 22;

  epd_poweron();
  int64_t start = esp_timer_get_time();
  enum EpdDrawError err = epd_hl_update_screen(&g_hl, mode, temp);
  epd_poweroff();

  if (err != EPD_DRAW_SUCCESS) {
    ESP_LOGE(kTag, "draw error 0x%X", err);
  } else {
    ESP_LOGI(kTag, "refresh %d ms", static_cast<int>((esp_timer_get_time() - start) / 1000));
  }
}

}  // namespace display
