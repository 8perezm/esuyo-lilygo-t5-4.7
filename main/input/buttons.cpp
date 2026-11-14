#include "input/buttons.h"

#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {

constexpr const char* kTag = "buttons";
constexpr int kPollMs = 20;
constexpr int kDebounceTicks = 2;     // 40 ms
constexpr int kLongMs = 1200;

struct Event {
  Btn btn;
  BtnEvent ev;
};

struct State {
  int gpio;
  Btn btn;
  bool stable;        // debounced level, true = pressed (active low)
  int raw_ticks;
  int held_ms;
  bool long_sent;
};

State g_state[3] = {
    {board::kBtnLeft, Btn::Left, false, 0, 0, false},
    {board::kBtnMid, Btn::Mid, false, 0, 0, false},
    {board::kBtnRight, Btn::Right, false, 0, 0, false},
};

QueueHandle_t g_queue = nullptr;
volatile bool g_suppress_until_boot = true;
TickType_t g_boot_tick = 0;

void task(void*) {
  for (;;) {
    bool pressed_now = false;
    for (auto& s : g_state) {
      bool level = gpio_get_level(static_cast<gpio_num_t>(s.gpio)) == 0;
      if (level == s.stable) {
        s.raw_ticks = 0;
      } else if (++s.raw_ticks >= kDebounceTicks) {
        s.stable = level;
        s.raw_ticks = 0;
        if (s.stable) {
          s.held_ms = 0;
          s.long_sent = false;
        } else if (!s.long_sent) {
          Event e{s.btn, BtnEvent::Short};
          xQueueSend(g_queue, &e, 0);
        }
      }
      if (s.stable) {
        pressed_now = true;
        s.held_ms += kPollMs;
        if (!s.long_sent && s.held_ms >= kLongMs) {
          s.long_sent = true;
          Event e{s.btn, BtnEvent::Long};
          xQueueSend(g_queue, &e, 0);
        }
      }
    }

    // The press that woke the board must not also trigger an action or sleep.
    if (g_suppress_until_boot) {
      if (!pressed_now && (xTaskGetTickCount() - g_boot_tick) > pdMS_TO_TICKS(400)) {
        g_suppress_until_boot = false;
        ESP_LOGI(kTag, "buttons live");
      } else if (g_suppress_until_boot) {
        Event discard;
        while (xQueueReceive(g_queue, &discard, 0) == pdTRUE) {
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

}  // namespace

namespace buttons {

void begin() {
  for (auto& s : g_state) {
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << s.gpio;
    cfg.mode = GPIO_MODE_INPUT;
    // GPIO34-39 are input-only and have no internal pull-ups; the board fits
    // external ones, so asking for an internal one only logs an error.
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
  }

  g_queue = xQueueCreate(16, sizeof(Event));
  g_boot_tick = xTaskGetTickCount();
  for (auto& s : g_state) {
    ESP_LOGI(kTag, "gpio %d idle level %d", s.gpio,
             gpio_get_level(static_cast<gpio_num_t>(s.gpio)));
  }
  g_suppress_until_boot = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
  xTaskCreatePinnedToCore(task, "buttons", 3072, nullptr, 6, nullptr, 0);
}

bool wait(BtnEvent* ev, Btn* btn, int timeout_ms) {
  Event e{};
  if (xQueueReceive(g_queue, &e, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
  if (ev != nullptr) *ev = e.ev;
  if (btn != nullptr) *btn = e.btn;
  return true;
}

bool pressed(Btn btn) {
  for (auto& s : g_state) {
    if (s.btn == btn) return gpio_get_level(static_cast<gpio_num_t>(s.gpio)) == 0;
  }
  return false;
}

void arm_wake() {
  uint64_t mask = 0;
  for (auto& s : g_state) mask |= (1ULL << s.gpio);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
}

int wake_pin() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return -1;
  uint64_t status = esp_sleep_get_ext1_wakeup_status();
  for (auto& s : g_state) {
    if (status & (1ULL << s.gpio)) return s.gpio;
  }
  return -1;
}

}  // namespace buttons
