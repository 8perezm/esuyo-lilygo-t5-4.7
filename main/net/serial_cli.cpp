#include "net/serial_cli.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "app/settings.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/wifi.h"

namespace {

constexpr const char* kTag = "cli";
constexpr int kLineMax = 160;

void print_help() {
  printf("\nserial console:\n");
  printf("  wifi <ssid> <password>   save Wi-Fi and reboot\n");
  printf("  status                   stored SSID, link state, heap\n");
  printf("  reboot\n");
  printf("  help\n\n");
}

void print_status() {
  const Settings& s = settings::get();
  printf("stored ssid : %s\n", s.ssid[0] != '\0' ? s.ssid : "(none)");
  printf("link        : %s, ip %s, ap %s\n", net::wifi_connected() ? "connected" : "down",
         net::wifi_ip(), net::ap_active() ? "up" : "off");
  printf("clock       : %s\n", net::time_valid() ? "synced" : "not synced");
  printf("heap        : internal %u, psram %u\n",
         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

// Two tokens at most: the password may contain spaces, so everything after the
// SSID is taken verbatim.
bool parse_wifi(char* line, char** ssid, char** pass) {
  char* rest = line + 4;  // skip "wifi"
  while (*rest == ' ') rest++;
  if (*rest == '\0') return false;
  *ssid = rest;
  char* space = strchr(rest, ' ');
  if (space == nullptr) {
    *pass = rest + strlen(rest);  // empty password
  } else {
    *space = '\0';
    *pass = space + 1;
    while (**pass == ' ') (*pass)++;
  }
  return true;
}

void handle_line(char* line) {
  // Strip CR/LF and surrounding blanks.
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
    line[--len] = '\0';
  }
  if (len == 0) return;

  if (strncmp(line, "wifi ", 5) == 0) {
    char* ssid = nullptr;
    char* pass = nullptr;
    if (!parse_wifi(line, &ssid, &pass) || ssid[0] == '\0') {
      printf("usage: wifi <ssid> <password>\n");
      return;
    }
    net::wifi_save_credentials(ssid, pass);
    printf("saved '%s' (%u password chars), restarting...\n", ssid,
           static_cast<unsigned>(strlen(pass)));
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
  } else if (strcmp(line, "status") == 0) {
    print_status();
  } else if (strcmp(line, "reboot") == 0) {
    printf("restarting...\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  } else if (strcmp(line, "help") == 0) {
    print_help();
  } else {
    printf("unknown command '%s' (try help)\n", line);
  }
}

void cli_task(void*) {
  char line[kLineMax];
  int used = 0;
  bool overflow = false;
  uint8_t ch = 0;

  for (;;) {
    int n = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(200));
    if (n <= 0) continue;

    // Echo so the terminal shows what is typed.
    uart_write_bytes(UART_NUM_0, reinterpret_cast<const char*>(&ch), 1);

    if (ch == '\r' || ch == '\n') {
      if (!overflow) {
        line[used] = '\0';
        handle_line(line);
      }
      used = 0;
      overflow = false;
      continue;
    }
    if (ch == 8 || ch == 127) {  // backspace
      if (used > 0) {
        used--;
        uart_write_bytes(UART_NUM_0, "\b \b", 3);
      }
      continue;
    }
    if (!isprint(static_cast<int>(ch))) continue;
    if (used < kLineMax - 1) {
      line[used++] = static_cast<char>(ch);
    } else {
      overflow = true;
    }
  }
}

}  // namespace

namespace net {

void serial_cli_start() {
  uart_config_t cfg{};
  cfg.baud_rate = 115200;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;

  if (uart_param_config(UART_NUM_0, &cfg) != ESP_OK) {
    ESP_LOGW(kTag, "uart config failed, console disabled");
    return;
  }
  if (uart_driver_install(UART_NUM_0, 1024, 0, 0, nullptr, 0) != ESP_OK) {
    ESP_LOGW(kTag, "uart driver already installed, console disabled");
    return;
  }

  xTaskCreate(cli_task, "serial_cli", 4096, nullptr, 2, nullptr);
  ESP_LOGI(kTag, "serial console ready (type help)");
}

}  // namespace net
