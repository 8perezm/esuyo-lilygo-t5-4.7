#pragma once

namespace probe {

// Continuous monitor of every pin that is not used by the panel, flash, PSRAM
// or the USB bridge. It exists so the real button wiring can be discovered:
// press a button and the level (and ADC value for the input-only pins) shows
// up in /api/gpio/live and in the serial log.
void start();

int pin_count();
int pin_gpio(int index);
int pin_level(int index);      // current debounced level, 0 or 1
int pin_mv(int index);         // ADC millivolts, or -1 when not an ADC pin
unsigned pin_presses(int index);
unsigned pin_age_ms(int index);  // ms since the last level change

// Ring buffer of debounced level changes, newest first. Lives on the device so
// a slow or busy HTTP client cannot miss a press.
struct Event {
  unsigned ms;   // uptime
  int gpio;
  int level;
  int mv;
};

int event_count();
bool event_at(int index, Event& out);  // index 0 = newest
void clear_events();

}  // namespace probe
