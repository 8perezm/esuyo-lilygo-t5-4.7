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
void refresh(enum EpdDrawMode mode = MODE_GC16);

}  // namespace display
