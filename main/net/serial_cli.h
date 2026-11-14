#pragma once

namespace net {

// Minimal line console on UART0, so Wi-Fi can be provisioned over USB even when
// the browser or the setup AP is misbehaving. Commands:
//   wifi <ssid> <password>   save credentials and reboot
//   status                  print the stored SSID and the current link
//   reboot                  restart
//   help
void serial_cli_start();

}  // namespace net
