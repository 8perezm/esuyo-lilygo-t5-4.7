#include "input/gpio_probe.h"

#include <cstring>

#include "app/power.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char* kTag = "probe";
constexpr int kPollMs = 20;
constexpr int kDebounceTicks = 5;  // 100 ms: also filters floating input-only pins
constexpr int kEventCap = 64;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
probe::Event g_events[kEventCap];
int g_head = 0;
int g_len = 0;

struct Entry {
  int gpio;
  int adc_channel;  // -1 when the pin is not on ADC1
  bool pullup;
  bool stable;      // debounced, 1 = idle high
  int raw_ticks;
  int mv;
  unsigned presses;
  int64_t changed_us;
};

// 34/35/39 are the documented user buttons and have external pull-ups.
// 36 is the battery divider, kept in the list because it is also an input-only
// pin a button could share.
// 12/13/14 are free header pins; they get internal pull-ups so a button to GND
// can be seen.
Entry g_pins[] = {
    {34, 6, false, true, 0, -1, 0, 0},
    {35, 7, false, true, 0, -1, 0, 0},
    {36, 0, false, true, 0, -1, 0, 0},
    {39, 3, false, true, 0, -1, 0, 0},
    {37, 1, false, true, 0, -1, 0, 0},
    {38, 2, false, true, 0, -1, 0, 0},
    {12, -1, true, true, 0, -1, 0, 0},
    {13, -1, true, true, 0, -1, 0, 0},
    {14, -1, true, true, 0, -1, 0, 0},
    {24, -1, true, true, 0, -1, 0, 0},
    {28, -1, true, true, 0, -1, 0, 0},
    {29, -1, true, true, 0, -1, 0, 0},
    {30, -1, true, true, 0, -1, 0, 0},
    {31, -1, true, true, 0, -1, 0, 0},
};

constexpr int kCount = sizeof(g_pins) / sizeof(g_pins[0]);

void configure() {
  for (auto& p : g_pins) {
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << p.gpio;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = p.pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    p.stable = gpio_get_level(static_cast<gpio_num_t>(p.gpio)) != 0;
  }
}

void task(void*) {
  int ticks = 0;
  for (;;) {
    int64_t now = esp_timer_get_time();
    for (auto& p : g_pins) {
      int level = gpio_get_level(static_cast<gpio_num_t>(p.gpio)) != 0;
      if (level == p.stable) {
        p.raw_ticks = 0;
      } else if (++p.raw_ticks >= kDebounceTicks) {
        p.stable = level;
        p.raw_ticks = 0;
        p.changed_us = now;
        if (!level) p.presses++;  // button to GND = press
        ESP_LOGI(kTag, "gpio %d -> %d after %u presses (adc %d mV)", p.gpio, level,
                 p.presses, p.mv);
        probe::Event ev{static_cast<unsigned>(now / 1000), p.gpio, level, p.mv};
        taskENTER_CRITICAL(&g_mux);
        g_events[g_head] = ev;
        g_head = (g_head + 1) % kEventCap;
        if (g_len < kEventCap) g_len++;
        taskEXIT_CRITICAL(&g_mux);
      }
      if (p.adc_channel >= 0) {
        p.mv = (ticks % 5 == 0) ? power::adc_millivolts(p.adc_channel) : p.mv;
      }
    }
    ticks++;
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

}  // namespace

namespace probe {

void start() {
  configure();
  xTaskCreate(task, "gpio_probe", 3072, nullptr, 2, nullptr);
  ESP_LOGI(kTag, "monitoring %d candidate pins", kCount);
}

int pin_count() { return kCount; }
int pin_gpio(int index) { return (index >= 0 && index < kCount) ? g_pins[index].gpio : -1; }
int pin_level(int index) { return (index >= 0 && index < kCount) ? g_pins[index].stable : -1; }
int pin_mv(int index) { return (index >= 0 && index < kCount) ? g_pins[index].mv : -1; }

unsigned pin_presses(int index) {
  return (index >= 0 && index < kCount) ? g_pins[index].presses : 0;
}

unsigned pin_age_ms(int index) {
  if (index < 0 || index >= kCount || g_pins[index].changed_us == 0) return 0xFFFFFFFFu;
  return static_cast<unsigned>((esp_timer_get_time() - g_pins[index].changed_us) / 1000);
}

int event_count() {
  int n;
  taskENTER_CRITICAL(&g_mux);
  n = g_len;
  taskEXIT_CRITICAL(&g_mux);
  return n;
}

bool event_at(int index, Event& out) {
  bool ok = false;
  taskENTER_CRITICAL(&g_mux);
  if (index >= 0 && index < g_len) {
    int slot = (g_head - 1 - index + kEventCap * 2) % kEventCap;
    out = g_events[slot];
    ok = true;
  }
  taskEXIT_CRITICAL(&g_mux);
  return ok;
}

void clear_events() {
  taskENTER_CRITICAL(&g_mux);
  g_head = 0;
  g_len = 0;
  taskEXIT_CRITICAL(&g_mux);
}

}  // namespace probe
