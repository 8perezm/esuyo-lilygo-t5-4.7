#pragma once

namespace power {

void init();

// Battery divider on ADC1; returns millivolts at the cell (0 if unreadable).
int battery_mv();

// Raw ADC1 reading in millivolts, e.g. for the button-scan pins.
int adc_millivolts(int channel);

// Deep sleep with the panel left powered down (the image stays). Wakes on any
// user button, plus a timer when timer_min > 0.
[[noreturn]] void sleep_now(int timer_min);

}  // namespace power
