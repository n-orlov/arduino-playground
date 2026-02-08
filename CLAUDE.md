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
- **1602A v1.2 LCD** (16x2, parallel 4-bit mode) — wired: RS=8, E=9, D4=4, D5=5, D6=6, D7=7
- **Challenger NB RP2040 WiFi** on **COM8** (FQBN: `rp2040:rp2040:challenger_nb_2040_wifi`, Core: `rp2040:rp2040`)
- Unknown device on **COM3**

## Board Notes (UNO R4 WiFi)

- Has a **12x8 onboard LED matrix** controllable via `Arduino_LED_Matrix.h` (`ArduinoLEDMatrix` class)
- `renderBitmap()` does not accept `const` arrays — use non-const `byte frame[8][12]`
- Uses native USB CDC — DTR toggle does not reset the board. To capture serial from boot, add `while (!Serial)` wait in `setup()`
- **WiFiSSLClient fails** for some hosts (e.g. `api.open-meteo.com`). Use plain `WiFiClient` on port 80 when the API supports it
- First TCP connection after WiFi join often fails — **add a retry loop** (3 attempts, 2s backoff) for `client.connect()`
- After `WiFi.begin()` connects, add a short `delay(1000)` before making HTTP requests

## LCD Notes (1602A)

- Standard `LiquidCrystal` library does **not work** on the R4 WiFi — timing too fast for HD44780
- Use **`hd44780`** library with `hd44780_pinIO` class instead (installed: `hd44780@1.3.2`)
- Library installed: `LiquidCrystal@1.0.7` (does not work), `hd44780@1.3.2` (works)

## Sketches

- `smiley/` — LED matrix animation cycling through 8 facial expressions
- `weather/` — Fetches temperature for Spalding, UK from Open-Meteo API (plain HTTP, port 80) and displays rounded value on LED matrix. Refreshes every 10 minutes
- `weather_station/` — Combined weather station: rounded temp on LED matrix, full details (temp, humidity, wind, conditions) on 1602A LCD. Uses Open-Meteo API, refreshes every 10 minutes
- `lcd_test/` — LCD diagnostic sketch
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
- WiFi signal is weak (-90 to -94 dBm) — drops frequently, needs auto-reconnect logic with 30s cooldown between attempts
- Has onboard **NeoPixel** on pin `NEOPIXEL` (GPIO 11) and LED on `LED_BUILTIN` (GPIO 12)
- Library installed: `WiFiEspAT@1.5.0`, `Adafruit NeoPixel@1.15.3`
