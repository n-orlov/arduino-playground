# Arduino Playground

## Environment

- **Arduino CLI**: `C:\Users\nikol\arduino-cli\arduino-cli.exe`
- **Platform**: Windows
- **Cores installed**: `arduino:renesas_uno@1.5.2`, `rp2040:rp2040@5.5.0` (Philhower arduino-pico)

## Credentials

- WiFi credentials and GitHub token are stored in `.env` (gitignored)
- Sketches use `YOUR_SSID`/`YOUR_PASSWORD` placeholders — replace from `.env` values before uploading

## Connected Hardware

- **Arduino UNO R4 WiFi** on **COM7** (FQBN: `arduino:renesas_uno:unor4wifi`, Core: `arduino:renesas_uno`)
- **1602A v1.2 LCD** (16x2, parallel 4-bit mode) — wired: RS=8, E=9, D4=4, D5=5, D6=6, D7=7 *(conflicts with TFT shield — cannot use both)*
- **3.5" TFT LCD Shield** (480x320, ILI9486, 8-bit parallel) — plugs directly onto UNO headers, includes resistive touchscreen + SD card slot
- **Challenger NB RP2040 WiFi** on **COM8** (FQBN: `rp2040:rp2040:challenger_nb_2040_wifi`, Core: `rp2040:rp2040`)
- Unknown device on **COM3**

## Board Notes (UNO R4 WiFi)

- Has a **12x8 onboard LED matrix** controllable via `Arduino_LED_Matrix.h` (`ArduinoLEDMatrix` class)
- `renderBitmap()` does not accept `const` arrays — use non-const `byte frame[8][12]`
- Uses native USB CDC — DTR toggle does not reset the board. To capture serial from boot, add `while (!Serial)` wait in `setup()`
- **WiFiSSLClient fails** for some hosts (e.g. `api.open-meteo.com`). Use plain `WiFiClient` on port 80 when the API supports it
- First TCP connection after WiFi join often fails — **add a retry loop** (3 attempts, 2s backoff) for `client.connect()`
- After `WiFi.begin()` connects, add a short `delay(1000)` before making HTTP requests

## TFT Shield Notes (3.5" ILI9486)

- **Driver IC**: ILI9486 (confirmed via register 0xD3 readback = 0x94)
- **Interface**: 8-bit parallel on standard UNO shield pinout — D2-D9 (data), A0(RD), A1(WR), A2(RS), A3(CS), A4(RST)
- **Resolution**: 480x320, 16-bit colour (RGB565)
- **Library**: `MCUFRIEND_kbv@3.0.0` with custom Renesas section in `utility/mcufriend_shield.h`
- **Critical R4 WiFi issue**: Renesas RA4M1 uses **per-pin PFS registers** for GPIO control. Port-wide PODR register writes have **no effect** after `pinMode()` configures the PFS. All GPIO must use `digitalWrite()`/`digitalRead()`
- The custom Renesas section in `mcufriend_shield.h` uses `digitalWrite()` for all pin access — works correctly but is slow (~30s for full-screen fill)
- **Optimisation tip**: Avoid `fillScreen()` — update only changed regions. Full dashboard redraw takes ~6s which is acceptable for a 10 min refresh cycle
- **No RTC**: R4 WiFi has no real-time clock. Parse `HH:MM` from Open-Meteo API's `"time"` field in the `"current"` block (format: `"2024-01-15T17:30"`)
- The shield occupies **all** UNO data pins — cannot use LED matrix, 1602A LCD, or other shields simultaneously
- **Waveshare_ILI9486** library (SPI-based) does NOT work — it's for Waveshare-specific shields with onboard shift registers, not generic 8-bit parallel shields
- Library installed: `MCUFRIEND_kbv@3.0.0-Release`, `Adafruit GFX Library@1.12.4`, `Adafruit BusIO@1.17.4`

## LCD Notes (1602A)

- Standard `LiquidCrystal` library does **not work** on the R4 WiFi — timing too fast for HD44780
- Use **`hd44780`** library with `hd44780_pinIO` class instead (installed: `hd44780@1.3.2`)
- Library installed: `LiquidCrystal@1.0.7` (does not work), `hd44780@1.3.2` (works)

## Sketches

- `smiley/` — LED matrix animation cycling through 8 facial expressions
- `weather/` — Fetches temperature for Spalding, UK from Open-Meteo API (plain HTTP, port 80) and displays rounded value on LED matrix. Refreshes every 10 minutes
- `weather_station/` — Combined weather station: rounded temp on LED matrix, full details (temp, humidity, wind, conditions) on 1602A LCD. Uses Open-Meteo API, refreshes every 10 minutes
- `lcd_test/` — LCD diagnostic sketch
- `tft_test/` — TFT shield diagnostic: ILI9486 driver identification and colour band test
- `tft_weather/` — TFT weather dashboard: rich graphical display (large temp, weather icons, humidity/wind gauges, 24h temp chart, current time) on 480x320 TFT. Uses Open-Meteo API, refreshes every 10 min. ~38% flash, ~31% RAM
- `challenger_wifi/` — Weather dashboard: fetches Spalding weather via WiFi, shows temp as NeoPixel colour (blue→cyan→green→yellow→red), and runs a web server with live dashboard (weather + board status). Auto-reconnects on WiFi drop. Refreshes every 10 min (weather) / 30s (web page)

## Board Notes (Challenger NB RP2040 WiFi)

- Uses **Earle Philhower's arduino-pico** core (board manager URL: `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`)
- Upload works via UF2 bootloader — auto-resets into flash mode, appears as USB drive, then reconnects on same COM port
- Uses RP2040 chip with **ESP8285** WiFi co-processor on **Serial2** (UART1, pins 4/5) with AT commands
- WiFi via `WiFiEspAT` library — reset ESP with long pulse (100ms low, 2s boot wait), then `WiFi.init(Serial2)`
- **`WiFi.begin()` is blocking** in WiFiEspAT — it either connects or fails, don't poll `WiFi.status()` in a loop after. Retry the `begin()` call itself with a full hardware reset of ESP between retries
- WiFiEspAT uses its own enum: `WL_CONNECTED=2`, `WL_CONNECT_FAILED=3`, `WL_CONNECTION_LOST=4`, `WL_DISCONNECTED=5` (different from standard Arduino WiFi!)
- **Do NOT use NeoPixel writes during WiFi/serial comms** — NeoPixel disables interrupts and corrupts UART data from ESP8285
- **Do NOT call `WiFi.scanNetworks()`** — `AT+CWLAP` disconnects from the AP and with weak signal it can't reconnect
- `WiFiServer` works but outgoing HTTP connections disrupt the listener — call `server.begin()` again after each outbound fetch
- When serving HTTP responses, call `client.flush()` + `delay(500)` before `client.stop()` — ESP8285 AT firmware needs time to send buffered data over the wire, otherwise Chrome gets `ERR_CONNECTION_RESET`
- WiFi signal is weak (-90 to -94 dBm) — drops frequently, needs auto-reconnect logic with 30s cooldown between attempts
- Has onboard **NeoPixel** on pin `NEOPIXEL` (GPIO 11) and LED on `LED_BUILTIN` (GPIO 12)
- Library installed: `WiFiEspAT@1.5.0`, `Adafruit NeoPixel@1.15.3`
