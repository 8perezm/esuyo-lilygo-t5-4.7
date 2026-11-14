#include "display/display.h"

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

namespace {

constexpr const char* kTag = "display";

EpdiyHighlevelState g_hl;
bool g_powered = false;
uint8_t* g_fb = nullptr;
int g_width = 0;
int g_height = 0;

}  // namespace

namespace display {

bool init() {
  // The 1K LUT has no GC16/i2s lookup table on the ESP32, so use the 64K one.
  // The default 32-line feed queue reserves 2 x 32 x 960 bytes of internal RAM;
  // the 8-line queue needs a quarter of that. Without it the Wi-Fi AP cannot
  // allocate its coexistence timer when a client associates, and the chip
  // aborts with ESP_ERR_NO_MEM (observed as a reboot loop on connect).
  epd_init(&epd_board_lilygo_t5_47, &ED047TC1,
           static_cast<EpdInitOptions>(EPD_LUT_64K | EPD_FEED_QUEUE_8));
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

void power_on() {
  if (!g_powered) {
    epd_poweron();
    g_powered = true;
  }
}

void power_off() {
  if (g_powered) {
    epd_poweroff();
    g_powered = false;
  }
}

uint8_t* fb() { return g_fb; }
int width() { return g_width; }
int height() { return g_height; }

void clear() {
  memset(g_fb, 0xFF, static_cast<size_t>(g_width) * g_height / 2);
}

void wipe() {
  int64_t start = esp_timer_get_time();
  power_on();
  // Longer cycles than epd_clear()'s 3 x 12 ms: enough to lift the retention
  // left by a previous firmware image, without costing 20 s on every boot.
  epd_clear_area_cycles(epd_full_screen(), 3, 40);
  const size_t bytes = static_cast<size_t>(g_width) * g_height / 2;
  memset(g_hl.front_fb, 0xFF, bytes);
  memset(g_hl.back_fb, 0xFF, bytes);
  ESP_LOGI(kTag, "panel wiped in %d ms",
           static_cast<int>((esp_timer_get_time() - start) / 1000));
}

void repair() {
  int64_t start = esp_timer_get_time();
  power_on();
  epd_clear_area_cycles(epd_full_screen(), 3, 300);
  const size_t bytes = static_cast<size_t>(g_width) * g_height / 2;
  memset(g_hl.front_fb, 0xFF, bytes);
  memset(g_hl.back_fb, 0xFF, bytes);
  ESP_LOGW(kTag, "panel repair in %d ms",
           static_cast<int>((esp_timer_get_time() - start) / 1000));
}

void refresh(enum EpdDrawMode mode) {
  int temp = epd_ambient_temperature();
  if (temp <= 0 || temp > 60) temp = 22;

  power_on();
  int64_t start = esp_timer_get_time();
  enum EpdDrawError err = epd_hl_update_screen(&g_hl, mode, temp);

  if (err != EPD_DRAW_SUCCESS) {
    ESP_LOGE(kTag, "draw error 0x%X", err);
  } else {
    ESP_LOGI(kTag, "refresh %d ms", static_cast<int>((esp_timer_get_time() - start) / 1000));
  }
}

}  // namespace display

