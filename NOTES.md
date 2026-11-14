# NOTES

Working notes, 2026-11-14. Long-form context lives in `docs/HANDOVER.md`.

## State

- Firmware builds and runs: panel updates, Wi-Fi joins (`FOO`), live weather and
  markets, web panel at **192.168.5.29**, OTA and deep sleep work.
- Three user buttons (35, 34, 39). RESET and BOOT are the other two switches; BOOT
  is the panel strobe (`CFG_STR`) and is not readable at runtime.
- Battery connector is JST-PH 2.0; a protected 2000 mAh cell has been chosen but
  not fitted or calibrated yet.

## Fixed recently

- Add city search rebooted the board: `/api/geocode` ran mbedTLS on the 5 KB
  httpd task and overflowed its stack. It now runs on a dedicated 8 KB worker
  task, so the HTTP handler never touches TLS.
- Add city with a space in the name looked up the literal `%20` because the
  httpd query value is not decoded; `net::url_decode` now runs before the
  upstream encoder.
- Setup AP aborted on client association (`ESP_ERR_NO_MEM`) → epdiy
  `EPD_FEED_QUEUE_8`, ~46 KB internal RAM recovered.
- Boot fell back to AP after ~30 ms → Wi-Fi retries, storage in RAM only.
- TLS failed with `MBEDTLS_ERR_MPI_ALLOC_FAILED` → mbedTLS heap moved to PSRAM.
- Open-Meteo 400 → removed `time` from the `daily` list.
- Yahoo 429 → browser User-Agent.
- Weather could paint `NO DATA` after one transient failure → one retry, IPv4
  pinned, SNTP wait before the first HTTPS request.
- Market sparklines were always empty → `indicators.quote` is an array; index 0.

## Next actions

1. Decide: add BOOT as deep-sleep wake-only, and swap 34/39 so the rightmost button
   advances.
2. Fit the battery, compare `battery_mv` with a multimeter, adjust
   `board::kBatteryDivider`.
3. Refresh the clock label when SNTP completes after the boot window.
4. Optional: cache the last weather snapshot in NVS for offline cold boots.
5. Optional: partial refresh for faster page turns.

## Commands

```powershell
cd C:\SERVER\REPOS\Esuyo\empty-14
. .\scripts\env.ps1
idf.py build
idf.py -p COM12 -b 460800 flash
.\scripts\capture.ps1 COM12 20
```

After adding source files: `idf.py reconfigure` first (configure-time glob).

## Docs

- `docs/HANDOVER.md` — full state and history
- `docs/architecture.md` — layers, rendering, memory
- `docs/api.md` — REST endpoints
- `docs/buttons.md` — physical map and discovery
- `docs/battery.md` — cell requirements and runtime
- `docs/troubleshooting.md` — symptoms and fixes
