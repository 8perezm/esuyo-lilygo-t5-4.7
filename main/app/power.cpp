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
  // Channels used by the button probe: 3 = GPIO39, 6 = GPIO34, 7 = GPIO35.
  adc_oneshot_config_channel(g_adc, ADC_CHANNEL_3, &chan);
  adc_oneshot_config_channel(g_adc, ADC_CHANNEL_6, &chan);
  adc_oneshot_config_channel(g_adc, ADC_CHANNEL_7, &chan);
}

int adc_millivolts(int channel) {
  if (g_adc == nullptr) return -1;
  int raw = 0;
  if (adc_oneshot_read(g_adc, static_cast<adc_channel_t>(channel), &raw) != ESP_OK) return -1;
  return raw * 3300 / 4095;
}

int battery_mv() {
  if (g_adc == nullptr) return 0;
  int raw = 0;
  if (adc_oneshot_read(g_adc, ADC_CHANNEL_0, &raw) != ESP_OK) return 0;
  float pin_mv = raw * 3300.0f / 4095.0f;
  return static_cast<int>(pin_mv * board::kBatteryDivider);
}

int battery_pct() {
  int mv = battery_mv();
  if (mv <= 0) return -1;
  int pct = (mv - 3300) * 100 / (4200 - 3300);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

bool battery_present() { return battery_mv() > 0; }

namespace {
// Nominal thresholds; adjust kBatteryDivider first if a meter disagrees.
constexpr int kUsbMinMv = 4260;     // above any cell: no-cell USB rail floats ~4360
constexpr int kChargeMinMv = 4210;  // above full resting cell (~4200): charger CV
}  // namespace

BatteryInfo battery_info() {
  const int mv = battery_mv();
  if (mv <= 0) return {BatteryState::kAbsent, 0, -1};
  if (mv > kUsbMinMv) return {BatteryState::kUsb, mv, -1};
  if (mv >= kChargeMinMv) return {BatteryState::kCharging, mv, -1};
  int pct = (mv - 3300) * 100 / (4200 - 3300);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return {BatteryState::kBattery, mv, pct};
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
