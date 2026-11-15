# T5 Dashboard

A C++ dashboard firmware for the **LilyGo T5 4.7" e-paper** board
(ESP32-WROVER-E, 960 × 540 ED047TC1, 16 greys). It is a purpose-built UI: several dashboard screens, a 4 bpp
greyscale renderer, physical button navigation, Wi-Fi setup without a cable, and
a web control panel.

> **Status:** working. The panel renders, updates on every screen change, joins
> Wi-Fi, fetches live data and serves the web panel. Known gaps and next steps
> are listed in [`docs/HANDOVER.md`](docs/HANDOVER.md).

## Screens

| Screen | Contents |
|---|---|
| Weather | Today emphasised (current temp, condition, feels-like, wind, rain, humidity) plus four following days; the middle button cycles the configured cities |
| Markets | One screen with three sub-screens selected by the middle button: Crypto (five coins with price, change and a five-day sparkline), Indices (S&P 500, Nasdaq, Dow, FTSE 100, DAX) and Commodities (gold, silver, natural gas, crude). All three lists are configurable |
| Quote | A random quote from ZenQuotes with its author; the middle button fetches another |
| Joke | A single-line joke from JokeAPI (filtering out NSFW, racist and explicit content server-side); the middle button fetches another |
| Status | Network, IP, signal, battery, memory, uptime, firmware, panel, MAC |

Any of these can be switched off in the web panel; the buttons then skip it. The
last screen, market sub-screen and weather city are stored in NVS, so a reboot
returns to the same view and fetches fresh data for it.

## Controls

Five switches sit above the panel. Left to right they are:

| # | Switch | Behaviour |
|---|---|---|
| 0 | **RESET** | Hardware reset |
| 1 | **BOOT / IO0** | Flashing only — cannot be read while the panel is driven |
| 2 | **GPIO 35** | Middle: cycles city on Weather, Crypto/Indices/Commodities on Markets, fetches another quote or joke, refreshes elsewhere |
| 3 | **GPIO 34** | Advances to the next screen; hold 1.2 s to sleep |
| 4 | **GPIO 39** | Steps back a screen; hold to refresh |

Any user button wakes the board from deep sleep. The panel keeps its image while
asleep. Full detail and the discovery procedure: [`docs/buttons.md`](docs/buttons.md).

## Web panel

On first boot, or when no saved network works, the board raises the hotspot
**`T5-Dashboard`** (password `t5dashboard`) and answers on
**http://192.168.4.1/** (captive-portal redirects included). Enter the home
network in the Wi-Fi card and press **Save & connect**. Afterwards the panel is
served at the board's LAN address, and the same page edits cities, coins, index
and commodity symbols, data-refresh, idle-sleep and deep-sleep-refresh timings,
and performs OTA updates.

## Build and flash

ESP-IDF 5.3+ and the component manager are required; LVGL 9 and epdiy are fetched
from the registry on the first build.

```powershell
. .\scripts\env.ps1                 # ESP-IDF 5.3 on this machine
idf.py build
idf.py -p <PORT> -b 460800 flash
idf.py -p <PORT> monitor             # or .\scripts\capture.ps1 <PORT> 20
```

Regenerate fonts and icons after changing the asset list:

```powershell
npm install
npm run assets
```

## Documentation

| Document | Contents |
|---|---|
| [`docs/HANDOVER.md`](docs/HANDOVER.md) | Current state, measured hardware, boot flow, known issues, next steps, history |
| [`docs/architecture.md`](docs/architecture.md) | Layers, rendering path, memory budget, how to add a screen |
| [`docs/api.md`](docs/api.md) | REST endpoints, request/response shapes |
| [`docs/buttons.md`](docs/buttons.md) | Physical button map, BOOT/RESET limits, discovery mode |
| [`docs/battery.md`](docs/battery.md) | Battery connector, cell requirements, polarity, runtime |
| [`docs/troubleshooting.md`](docs/troubleshooting.md) | No data, setup AP, stale panel, TLS, clocks, heap |
| [`NOTES.md`](NOTES.md) | One-page working notes and immediate next actions |

## Layout

```
main/
  app/         boot order, screen carousel, settings, power, diagnostics
  display/     epdiy wrapper + LVGL canvas and 4 bpp flush
  input/       debounced buttons, live GPIO probe
  net/         Wi-Fi, HTTP server/client, serial console
  data/        Open-Meteo and Yahoo providers
  screens/     weather, markets (3 sub-screens), quote, joke, status
  assets/      generated Roboto fonts and Lucide icons
  www/         embedded control panel
scripts/       env, flash, capture, asset pipeline
docs/          the documents above
```

## Useful 

https://github.com/Xinyuan-LilyGO/LilyGo-EPD47
https://wiki.lilygo.cc/products/t5-series/t5-e-paper/quick-start.html

# Printable case

https://www.printables.com/model/272575-case-for-lilygo-t5-47-inch-e-paper
https://www.thingiverse.com/thing:4890972
https://www.thingiverse.com/thing:4749075
https://www.thingiverse.com/thing:5638478
