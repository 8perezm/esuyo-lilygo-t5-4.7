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
// Drive the whole panel to white. Needed after a cold boot or a deep-sleep
// wake: the panel may still hold an image (or another firmware's image), while
// the freshly allocated high-level state believes it is blank.
void wipe();
void refresh(enum EpdDrawMode mode = MODE_GC16);

}  // namespace display
