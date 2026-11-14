# REST API

All responses are JSON. No authentication: the panel is intended for a trusted LAN,
and the setup AP is password protected.

## Status

`GET /api/status`

```json
{
  "fw": "1.0.0",
  "heap": 184320,
  "psram_free": 3145728,
  "uptime_s": 42,
  "rssi": -57,
  "ip": "192.168.5.29",
  "ssid": "FOO",
  "screen": "weather",
  "battery_mv": 3980,
  "wifi_mode": "sta"
}
```

`wifi_mode` is `sta` when connected and `ap+sta` while the setup AP is up.

## Settings

`GET /api/settings`

```json
{
  "wifi": { "ssid": "FOO", "configured": true },
  "weather": {
    "cities": [
      { "name": "Basingstoke", "lat": 51.2667, "lon": -1.0833 }
    ],
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
and longitude are required for a city. Changes are persisted in NVS and take effect
on the next refresh (or immediately for units and the active city).

Crypto ids may be CoinGecko slugs (`bitcoin`) or Yahoo tickers (`BTC-USD`); the
provider maps the common slugs and treats a bare word as `<WORD>-USD`.

## Wi-Fi

`GET /api/wifi/scan` → `{"networks":[{"ssid":"...","rssi":-60,"auth":4}]}`

`POST /api/wifi` with `{"ssid":"...","password":"..."}` saves the credentials and
restarts the board (the response is sent first).

`GET /api/geocode?q=Basingstoke` proxies Open-Meteo geocoding:
`{"results":[{"name":"Basingstoke","country":"United Kingdom","admin1":"England","lat":51.2667,"lon":-1.0833}]}`

## Screens

`GET /api/screens` → `{"screens":[{"id":"weather","title":"Weather"}, ...]}`

`POST /api/display` with `{"screen":"crypto"}` queues a screen change (rendered by
the main loop, ~2.5 s).

`POST /api/refresh` with an optional screen id refreshes the data.

## Firmware

`POST /api/ota` with the raw application `.bin` as the body
(`Content-Type: application/octet-stream`) writes the inactive OTA slot, sets it as
the boot partition and restarts. A rejected image returns HTTP 400.

`POST /api/reboot` restarts without touching the slots.
