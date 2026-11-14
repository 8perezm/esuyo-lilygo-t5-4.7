# REST API

All responses are JSON. No authentication: the panel is intended for a trusted LAN,
and the setup AP is password protected. Paths are available both on the setup AP
(`192.168.4.1`) and on the board's LAN address.

## Status

`GET /api/status`

```json
{
  "fw": "1.0.0",
  "heap": 47251,
  "psram_free": 2569400,
  "uptime_s": 42,
  "rssi": -57,
  "ip": "192.168.5.29",
  "ssid": "FOO",
  "screen": "weather",
  "subscreen": "",
  "battery_mv": 3980,
  "wifi_mode": "sta"
}
```

`heap` is **free internal RAM** (the scarce resource on this board), not total
heap. `psram_free` is reported separately. `wifi_mode` is `sta` when connected and
`ap+sta` while the setup AP is up. `subscreen` is `crypto`, `indices` or
`commodities` while the Markets screen is showing; it is empty on the other
screens.

## Settings

`GET /api/settings`

```json
{
  "wifi": { "ssid": "FOO", "configured": true },
  "weather": {
    "cities": [{ "name": "Basingstoke", "lat": 51.2667, "lon": -1.0833 }],
    "active": 0,
    "units": "c"
  },
  "crypto": { "ids": ["bitcoin", "ethereum", "solana", "ripple", "cardano"] },
  "indices": { "symbols": ["^GSPC", "^IXIC", "^DJI", "^FTSE", "^GDAXI"] },
  "commodities": { "symbols": ["GC=F", "SI=F", "NG=F", "CL=F"] },
  "refresh_min": 15,
  "sleep_after_s": 120
}
```

`PUT /api/settings` accepts the same shape. Lists are capped at 8 entries; latitude
and longitude are required per city. Changes persist in NVS and take effect on the
next refresh (units and the active city apply immediately).

`wifi.password` may be included in the PUT. If present and non-empty it is stored,
and the device restarts after saving — this is what the panel's top **Save** button
does when a password has been typed.

## Wi-Fi

`GET /api/wifi/scan` → `{"networks":[{"ssid":"...","rssi":-60,"auth":4}]}`

`POST /api/wifi` with `{"ssid":"...","password":"..."}` saves the credentials and
restarts the board (the response is sent first). The setup AP stays up until the
station connection succeeds.

`GET /api/geocode?q=Basingstoke` proxies Open-Meteo geocoding:
`{"results":[{"name":"Basingstoke","country":"United Kingdom","admin1":"England","lat":51.2667,"lon":-1.0833}]}`

**Captive portal:** `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt` and
similar probes return `302` to `http://192.168.4.1/`; any other unknown path serves
the control panel.

## Screens

`GET /api/screens` → `{"screens":[{"id":"weather","title":"Weather"},{"id":"markets","title":"Markets"},{"id":"status","title":"Status"}]}`

`POST /api/display` with `{"screen":"markets"}` queues a screen change (rendered by
the main loop, ~4 s including the wipe). The Markets screen keeps its last
sub-screen.

The old market ids still work as shortcuts and select the matching sub-screen:
`{"screen":"crypto"}`, `{"screen":"indices"}`, `{"screen":"commodities"}`.

`POST /api/refresh` with an optional screen id refreshes the data.

The last screen, market sub-screen and weather city are saved in NVS. After a
reboot (or a timer wake) the panel restores that view and fetches fresh data.

## Firmware

`POST /api/ota` with the raw application `.bin` as the body
(`Content-Type: application/octet-stream`) writes the inactive OTA slot, sets it as
the boot partition and restarts. A rejected image returns HTTP 400.

`POST /api/reboot` restarts without touching the slots.

`POST /api/display/repair` performs a deep panel clear (3 × 300 ms) and redraw,
about 20 s. Useful after long retention or an aborted draw.

## Diagnostics

`GET /api/gpio/live`

```json
{"pins":[
  {"gpio":34,"level":1,"mv":3300,"presses":0,"age_ms":4294967295},
  {"gpio":37,"level":0,"mv":941,"presses":0,"age_ms":4294967295}
]}
```

`level` is the debounced level (1 = idle high), `mv` is the ADC value for the
input-only pins (-1 otherwise), `presses` counts high→low transitions, and
`age_ms` is the time since the last change (`4294967295` = never).

`GET /api/gpio/events` returns the device-side ring buffer, newest first, so a slow
client cannot miss a press:

```json
{"uptime_ms":298475,
 "events":[{"ms":247523,"gpio":39,"level":1,"mv":3300},
           {"ms":247262,"gpio":39,"level":0,"mv":0}]}
```

`POST /api/gpio/clear` empties that buffer.

`GET /api/gpio/scan` blocks for five seconds, samples GPIO 12, 13, 14, 34, 35, 36
and 39, and reports which pins changed — the older, simpler discovery method.
