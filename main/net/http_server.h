#pragma once

namespace server {

void start();

// True when a client hit the panel within the last `within_ms` milliseconds.
// The web UI polls /api/status every 5 s while its page is open, so this is how
// the main loop knows someone is looking at the panel and holds off deep sleep
// instead of going dark under an open page.
bool client_active(unsigned within_ms);

}  // namespace server
