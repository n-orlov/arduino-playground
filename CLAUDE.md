# Arduino Playground

## Environment

- **Arduino CLI**: `C:\Users\nikol\arduino-cli\arduino-cli.exe`
- **Platform**: Windows
- **Core installed**: `arduino:renesas_uno@1.5.2`

## Connected Hardware

- **Arduino UNO R4 WiFi** on **COM7** (FQBN: `arduino:renesas_uno:unor4wifi`, Core: `arduino:renesas_uno`)
- **1602A v1.2 LCD** (16x2, parallel 4-bit mode) — wired: RS=8, E=9, D4=4, D5=5, D6=6, D7=7
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
