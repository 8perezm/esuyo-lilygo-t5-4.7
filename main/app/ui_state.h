#pragma once

// Last screen and sub-screen selected on the panel. Kept in its own NVS
// namespace so it survives reboots without changing the Settings blob layout.
namespace ui_state {

void init();

const char* screen_id();  // "weather", "markets", "status"
int market_kind();        // MarketScreen::Kind as an int

void set_screen_id(const char* id);
void set_market_kind(int kind);

}  // namespace ui_state
