#pragma once

namespace power {

void init();

// Battery divider on ADC1; returns millivolts at the cell (0 if unreadable).
int battery_mv();

// 0-100 % from the single-cell curve (3300 mV empty, 4150 mV full). Returns -1
// when no reading is available.
int battery_pct();

// True when the ADC gives a plausible cell reading. On USB with no cell the
// divider floats near the charger rail (~4.36 V), so this is "readable",
// not a dedicated presence pin.
bool battery_present();

// Charge state inferred from voltage alone: this board has no USB-sense or
// charger-status GPIO, so USB power cannot be detected directly. While USB is
// connected the charger holds the cell node at its ~4.2 V CV point and the raw
// voltage overstates the state of charge.
enum class BatteryState {
  kAbsent,    // no ADC reading
  kUsb,       // above any plausible cell voltage: USB rail, no cell (~4.36 V)
  kCharging,  // at the charger CV point: charging or full, SOC unknown
  kBattery,   // on-cell reading, pct is meaningful
};

struct BatteryInfo {
  BatteryState state;
  int mv;   // millivolts at the cell (0 when absent)
  int pct;  // 0-100, meaningful only for kBattery (-1 otherwise)
};

BatteryInfo battery_info();

// Raw ADC1 reading in millivolts, e.g. for the button-scan pins.
int adc_millivolts(int channel);

// Deep sleep with the panel left powered down (the image stays). Wakes on any
// user button, plus a timer when timer_min > 0.
[[noreturn]] void sleep_now(int timer_min);

}  // namespace power
