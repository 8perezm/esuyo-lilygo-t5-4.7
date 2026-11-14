#pragma once

enum class Btn { Left, Mid, Right };
enum class BtnEvent { Short, Long };

namespace buttons {

void begin();

// Blocking receive; returns false on timeout.
bool wait(BtnEvent* ev, Btn* btn, int timeout_ms);

bool pressed(Btn btn);

// EXT1 wake configuration for deep sleep (all mapped buttons).
void arm_wake();
int wake_pin();  // -1 when the wake was not caused by a button

}  // namespace buttons
