#pragma once

#include <cstddef>

namespace app {

void run();

const char* current_screen_id();
int screen_count();
const char* screen_id(int index);
const char* screen_title(int index);

// Deferred requests from the web UI, handled by the main loop.
void request_screen(const char* id);
void request_refresh();
void request_repair();

bool apply_settings_json(const char* json, size_t len);  // PUT /api/settings
void settings_to_json(char* out, size_t cap);            // GET /api/settings

// True when the last PUT carried a new Wi-Fi password, so the caller can
// restart into it. Cleared by the caller.
bool take_wifi_changed();

void reboot();

}  // namespace app
