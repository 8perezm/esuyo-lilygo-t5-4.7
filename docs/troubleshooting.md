# Troubleshooting

Commands assume the board is at `192.168.5.29`; substitute the address shown in
`/api/status` or the boot log.

## Add city search reboots the board

Fixed in the current firmware. `/api/geocode` used to perform the Open-Meteo
HTTPS lookup on the httpd task, whose 5 KB stack is smaller than mbedTLS needs.
The serial log showed:

```
***ERROR*** A stack overflow in task httpd has been detected.
```

The lookup now runs on a dedicated 8 KB `geocode` worker, and the HTTP handler
only waits for its result. If searches fail again, look for
`geocode: no memory for worker` in the serial log: it means internal RAM is too
fragmented to start the task, so reboot and check the heap numbers in
`/api/status`.

## The Quote screen stays on NO DATA

`zenquotes.io` answers HTTP 200 with neither `Content-Length` nor chunked framing:
the body ends when the server closes the socket. `esp_http_client` assumes an
unknown length means chunked and parses zero bytes, so `net::http_get()` retries
the same URL over a bare TLS socket (`http: close-delimited body, retrying over
raw TLS`) and strips the headers itself. If that still fails, the log says which
step broke (`raw TLS connect failed`, `raw TLS: bad status`, `raw TLS: no header
terminator`).

ZenQuotes also rate-limits the keyless endpoint to 5 requests per 30 seconds and
returns HTTP 200 with `"q": "Too many requests..."` rather than an error code.
That reply is detected and treated as a failure, so the previous quote stays on
screen. Normal use — one fetch per refresh plus middle-button presses — is far
below the limit; a script hammering `/api/refresh` is not.

## Jokes look wrong or the screen stays on NO DATA

The screen pins `type=single` so every reply is one block of text, and asks the
API to exclude `nsfw`, `racist` and `explicit` jokes. JokeAPI reports its own
failures as `"error": true` inside HTTP 200, so the body is checked, not the
status. The serial log shows the category and text of every fetch
(`jokes: Programming: ...`); a rejected or empty reply logs
`jokes: no usable joke`.

## The panel shows NO DATA

1. `GET /api/status` — check `ssid`, `ip`, `rssi`.
2. If `ssid` is empty the board is in setup AP mode; join `T5-Dashboard`
   (`t5dashboard`) and open `http://192.168.4.1/`.
3. If the network is up, look at the serial log while it boots:
   `weather: Basingstoke: -1 deg, sun, 5 days` means data arrived. A 400 means the
   Open-Meteo URL is malformed (the `daily` list must not contain `time`).
4. `esp-x509-crt-bundle` errors usually mean the clock is still 1970; the firmware
   waits up to 8 s for SNTP before the first fetch.

## Joining the setup AP drops the connection and the panel flashes

The Wi-Fi stack ran out of internal RAM while allocating its coexistence timer when
the client associated. This was caused by epdiy's default 32-line feed queue and is
fixed by `EPD_FEED_QUEUE_8`. Check the boot log for:

```
app: heap after server: internal free 4xxxx, largest 3xxxx
```

If internal free is below ~30 KB, something new is consuming internal RAM.

## The web page saves Wi-Fi but the board stays in AP mode

Use the **Wi-Fi card → Save & connect** button, not the top **Save** (the top Save
now also sends the password if one is typed). If the browser is unreliable, use the
UART console instead:

```
wifi <ssid> <password>
```

## The page will not open on a phone

Some phones keep their own connectivity page. Navigate directly to
`http://192.168.4.1/`. Android and iOS probes (`/generate_204`,
`/hotspot-detect.html`, …) are redirected to the panel by the firmware.

## Saved credentials are ignored after a reboot

The Wi-Fi driver stores its own copy and auto-connects at `esp_wifi_start()`,
raising a transient disconnect event. The firmware now retries association three
times and keeps Wi-Fi storage in RAM only. If it still falls to AP mode, the
password is wrong or the AP is 5 GHz-only (the ESP32 is 2.4 GHz).

## The panel keeps an old image

Every screen change does a full wipe and a full GC16 update, so a stale image means
the draw was never issued. Check the log for `display: refresh ...` after the
change. `POST /api/display/repair` runs a deep 3 × 300 ms clear (~20 s) and redraws.
A temperature outside 20–30 °C would make the built-in waveform fail, but epdiy
clamps it.

## The clock shows `--:--`

SNTP did not answer within the 8 s boot window. Data is unaffected. The label is
only redrawn on the next refresh, so wait for the refresh timer or press the middle
button.

## The web panel is slow, or requests time out

Check the link before blaming the firmware: `ping 192.168.5.29` from a machine on
the same network. A healthy ESP32 answers every packet in single-digit
milliseconds. Loss and RTTs in the hundreds of milliseconds to seconds mean the
2.4 GHz link is the problem, and TCP retransmits then show up as requests that
take 5–20 s. Compare with `ping <gateway>` to rule out the host's own link.

The panel is 2.4 GHz-only, so a PC on 5 GHz cannot judge that band from its own
scan. Modem sleep is already disabled in firmware; if loss persists the channel
is congested or the AP is dropping buffered frames — move the AP/panel or change
the AP's 2.4 GHz channel. Only three browser connections are served at once, so a
stalled link plus a second tab polling `/api/gpio/live` compounds the delay.

## The web panel says the device is offline

The board deep-sleeps after the configured idle time (default 2 minutes) and an
asleep board cannot be reached over Wi-Fi. While the control panel is open it
polls `/api/status` (every 5 s, or about once a minute from a background tab) and
that keeps the board awake, so the panel sleeps `sleep_after_s` after the page is
closed. Browsers freeze long-backgrounded tabs, which stops the polls and lets the
panel sleep; press any user button to wake it and reload the page. Deep sleep
keeps the last image on the glass.

## Text looks faint

The panel is a low-contrast 16-grey display. The palette is deliberately dark
(`0x1F1F1F` body, `0x2B2B2B` labels, `0x1F1F1F` rules that are 2-3 px thick) and
nothing is smaller than 28 px. Do not reintroduce lighter greys or 1 px rules.

## Battery voltage is wrong

`4.36 V` on USB is the charger rail. With a cell fitted, expect 3.7–4.2 V. The
divider is nominal 2:1 and uncalibrated; measure at the cell and adjust
`board::kBatteryDivider` if needed.

## Flashing fails or the port is missing

- The bridge is a CP210x on **COM12**.
- If esptool cannot sync, hold **BOOT**, tap **RESET**, release BOOT.
- `scripts/capture.ps1` de-asserts DTR/RTS before resetting; pyserial's defaults
  otherwise hold GPIO0 low and park the chip in download mode.
- New `.cpp` files require `idf.py reconfigure` before they are compiled, because
  `main/CMakeLists.txt` globs at configure time.

## Yahoo quotes return 429

The chart API requires a browser-like `User-Agent`. The firmware sends one; if you
add another provider or change the header, keep it.
