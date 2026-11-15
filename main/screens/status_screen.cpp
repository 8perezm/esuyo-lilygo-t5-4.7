#include "screens/status_screen.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "app/power.h"
#include "app/settings.h"
#include "display/display.h"
#include "display/ui.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "net/wifi.h"
#include "screens/common.h"

namespace {

const char* battery_text() {
  static char buf[32];
  const power::BatteryInfo info = power::battery_info();
  switch (info.state) {
    case power::BatteryState::kAbsent:
      return "--";
    case power::BatteryState::kUsb:
      snprintf(buf, sizeof(buf), "USB (%d.%02d V)", info.mv / 1000, (info.mv % 1000) / 10);
      return buf;
    case power::BatteryState::kCharging:
      snprintf(buf, sizeof(buf), "CHG (%d.%02d V)", info.mv / 1000, (info.mv % 1000) / 10);
      return buf;
    case power::BatteryState::kBattery:
      break;
  }
  // Rough single-cell curve; good enough for a status line. While USB is
  // connected the charger inflates the reading, so that state is reported as
  // CHG/USB above instead of a percent.
  snprintf(buf, sizeof(buf), "%d%% (%d.%02d V)", info.pct, info.mv / 1000,
           (info.mv % 1000) / 10);
  return buf;
}

}  // namespace

void StatusScreen::build(lv_obj_t* root) {
  screens::header(root, "STATUS", "DEVICE AND NETWORK", &clock_, &date_);
  screens::rule(root, 100);

  static const char* labels[] = {"NETWORK", "ADDRESS",  "SIGNAL",  "BATTERY", "MEMORY",
                                 "UPTIME",  "FIRMWARE", "PANEL",   "MAC"};
  for (int i = 0; i < 9; i++) {
    int y = 124 + i * 40;
    ui::label(root, &font_roboto_bold_28, screens::kGrey, labels[i], 28, y);
    value_[i] = ui::label(root, &font_roboto_medium_28, screens::kInk, "--", 220, y);
  }

  screens::footer(root, "< SCREEN   HOLD > SLEEP", &updated_, &batt_text_, &batt_frame_,
                  &batt_fill_);
}

bool StatusScreen::load() {
  screens::refresh_chrome(clock_, date_);
  screens::refresh_battery(batt_text_, batt_fill_, batt_frame_);
  char buf[64];

  const char* mode = net::ap_active() ? "SETUP AP" : (net::wifi_connected() ? "CONNECTED" : "OFFLINE");
  snprintf(buf, sizeof(buf), "%s  %s", mode, net::wifi_ssid()[0] ? net::wifi_ssid() : "-");
  ui::set_text(value_[0], buf);

  ui::set_text(value_[1], net::wifi_ip());

  snprintf(buf, sizeof(buf), "%d dBm", net::wifi_rssi());
  ui::set_text(value_[2], net::wifi_connected() ? buf : "--");

  ui::set_text(value_[3], battery_text());

  snprintf(buf, sizeof(buf), "%u KB free / %u KB PSRAM",
           static_cast<unsigned>(esp_get_free_heap_size() / 1024),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  ui::set_text(value_[4], buf);

  int64_t up = esp_timer_get_time() / 1000000;
  snprintf(buf, sizeof(buf), "%lldh %02lldm", up / 3600, (up % 3600) / 60);
  ui::set_text(value_[5], buf);

  const esp_app_desc_t* desc = esp_app_get_description();
  ui::set_text(value_[6], desc != nullptr ? desc->version : "?");

  snprintf(buf, sizeof(buf), "%dx%d  16 GREY", display::width(), display::height());
  ui::set_text(value_[7], buf);

  uint8_t mac[6] = {};
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
  ui::set_text(value_[8], buf);

  if (updated_ != nullptr) {
    snprintf(buf, sizeof(buf), "FREE %u KB", static_cast<unsigned>(esp_get_free_heap_size() / 1024));
    ui::set_text(updated_, buf);
  }
  return true;
}
