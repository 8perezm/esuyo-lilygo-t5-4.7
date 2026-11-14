#pragma once

#include <cstdint>

#include "epdiy.h"

namespace display {

bool init();

// The panel rails are only powered while an update runs.
void power_on();
void power_off();

uint8_t* fb();
int width();
int height();

void clear();
// Drive the whole panel to white and reset both high-level buffers to match,
// so the next frame is diffed against what is actually on the glass. Needed on
// every screen change and after a cold boot.
void wipe();
// Stronger (slow) clear for image retention left by an older firmware.
void repair();
void refresh(enum EpdDrawMode mode = MODE_GC16);

}  // namespace display
