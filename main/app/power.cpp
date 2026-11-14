#include "app/power.h"

#include "board.h"
#include "display/display.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "input/buttons.h"

namespace {

constexpr const char* kTag = "power";

adc_oneshot_unit_handle_t g_adc = nullptr;

}  // namespace

namespace power {

void init() {
  adc_oneshot_unit_init_cfg_t unit{};
  unit.unit_id = ADC_UNIT_1;
  unit.ulp_mode = ADC_ULP_MODE_DISABLE;
  if (adc_oneshot_new_unit(&unit, &g_adc) != ESP_OK) {
    ESP_LOGW(kTag, "ADC unavailable");
    g_adc = nullptr;
    return;
  }
  adc_oneshot_chan_cfg_t chan{};
  chan.atten = ADC_ATTEN_DB_12;
  chan.bitwidth = ADC_BITWIDTH_12;
  adc_oneshot_config_channel(g_adc, ADC_CHANNEL_0, &chan);  // GPIO36
}

int battery_mv() {
  if (g_adc == nullptr) return 0;
  int raw = 0;
  if (adc_oneshot_read(g_adc, ADC_CHANNEL_0, &raw) != ESP_OK) return 0;
  float pin_mv = raw * 3300.0f / 4095.0f;
  return static_cast<int>(pin_mv * board::kBatteryDivider);
}

void sleep_now(int timer_min) {
  ESP_LOGW(kTag, "sleeping (%d min timer)", timer_min);
  display::power_off();
  buttons::arm_wake();
  if (timer_min > 0) {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timer_min) * 60ULL * 1000000ULL);
  }
  esp_deep_sleep_start();
  for (;;) {
  }
}

}  // namespace power
