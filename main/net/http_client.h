#pragma once

#include <string>

namespace net {

// HTTPS GET with the ESP certificate bundle. Returns true on HTTP 200.
// Requests are serialised across tasks; a second caller waits for the first.
bool http_get(const std::string& url, std::string& out, int timeout_ms = 15000);

// Percent-encode everything outside [A-Za-z0-9-._~].
std::string url_encode(const char* text);

// Reverse url_encode; '+' is treated as a space, as in form queries.
std::string url_decode(const char* text);

}  // namespace net
