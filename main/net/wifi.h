#pragma once

#include <cstddef>

#include "esp_wifi_types.h"

namespace net {

// Connect with the stored credentials. On failure (or when there are none)
// bring up the setup access point instead, so the device is never stranded.
bool wifi_start();

bool wifi_connected();
bool ap_active();

bool wifi_connect(const char* ssid, const char* pass, int timeout_ms);
bool wifi_save_credentials(const char* ssid, const char* pass);

int wifi_rssi();
const char* wifi_ip();
const char* wifi_ssid();
const char* ap_ssid();

int wifi_scan(wifi_ap_record_t* out, int max);

void time_sync_start();
bool time_valid();
void format_clock(char* out, size_t cap);    // "14:05"
void format_date(char* out, size_t cap);     // "Sat 14 Nov"

}  // namespace net
