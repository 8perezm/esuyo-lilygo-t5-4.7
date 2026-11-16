#pragma once

#include <cstdint>

namespace board {

// LilyGo T5 4.7" e-paper, ESP32-WROVER-E, ED047TC1 panel.
// Native panel is 960x540 in landscape.
inline constexpr int kPanelWidth = 960;
inline constexpr int kPanelHeight = 540;

// The three user buttons, active low with external pull-ups.
// GPIO0 is the display shift-register strobe (epdiy CFG_STR) and must not be
// used as an input; GPIO34-39 are input-only RTC GPIOs, which is what makes
// EXT1 deep-sleep wake possible.
inline constexpr int kBtnLeft = 39;
inline constexpr int kBtnMid = 35;
inline constexpr int kBtnRight = 34;

// Battery divider on ADC1 channel 0. End-to-end cal 2026-11-16: 4.19 V cell
// read 3818 mV at 2.0x, so 2.195x.
inline constexpr int kBatteryAdc = 36;
inline constexpr float kBatteryDivider = 2.195f;

inline constexpr int kSpiFlashSizeMb = 16;

}  // namespace board
