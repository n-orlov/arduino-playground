// TFT Weather Station — Rich graphical dashboard
// 3.5" ILI9486 TFT Shield (480x320) on Arduino UNO R4 WiFi
// Fetches weather from Open-Meteo API for Spalding, UK
//
// Layout (landscape 480x320):
//   Top bar:      Location + last-update time
//   Centre-left:  Large weather icon (drawn with GFX primitives)
//   Centre-right: Large temperature + condition text
//   Bottom:       Humidity bar, wind speed, feels-like, pressure info
//   Right strip:  24h temp mini-chart (hourly temps)

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <WiFiS3.h>

// --- Colours (RGB565) ---
#define C_BG        0x1082  // very dark blue-grey
#define C_PANEL     0x2124  // dark grey panel
#define C_HEADER    0x0A3A  // deep teal
#define C_WHITE     0xFFFF
#define C_LGREY     0xB596
#define C_DGREY     0x6B4D
#define C_YELLOW    0xFEA0
#define C_ORANGE    0xFC00
#define C_RED       0xF800
#define C_CYAN      0x07FF
#define C_BLUE      0x03BF
#define C_DBLUE     0x016C
#define C_GREEN     0x07C0
#define C_LGREEN    0x47E4
#define C_RAIN_BLUE 0x5D7F
#define C_SNOW_W    0xE73C
#define C_SUN_Y     0xFFE0

// --- Config ---
const char* ssid = "YOUR_SSID";
const char* pass = "YOUR_PASSWORD";
const char* host = "api.open-meteo.com";
const char* path = "/v1/forecast?latitude=52.79&longitude=-0.15"
                   "&current=temperature_2m,relative_humidity_2m,"
                   "wind_speed_10m,weather_code,apparent_temperature"
                   "&hourly=temperature_2m"
                   "&forecast_days=1";
const unsigned long REFRESH_MS = 600000;  // 10 min

// --- Hardware ---
MCUFRIEND_kbv tft;

// --- State ---
unsigned long lastFetch = 0;
bool firstRun = true;
float curTemp = 0, curFeels = 0, curWind = 0;
int   curHumidity = 0, curWeatherCode = 0;
char  curTime[6] = "--:--";
float hourlyTemp[24];
int   hourlyCount = 0;
float tempMin24 = 99, tempMax24 = -99;

// ===================== Drawing Helpers =====================

void drawRoundPanel(int x, int y, int w, int h, uint16_t colour) {
  tft.fillRoundRect(x, y, w, h, 6, colour);
}

// ===================== Weather Icons (64x64 area) =====================

void drawSun(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, C_SUN_Y);
  // Rays
  for (int a = 0; a < 360; a += 45) {
    float rad = a * 3.14159 / 180.0;
    int x1 = cx + cos(rad) * (r + 4);
    int y1 = cy + sin(rad) * (r + 4);
    int x2 = cx + cos(rad) * (r + 10);
    int y2 = cy + sin(rad) * (r + 10);
    tft.drawLine(x1, y1, x2, y2, C_SUN_Y);
    tft.drawLine(x1+1, y1, x2+1, y2, C_SUN_Y);
  }
}

void drawCloud(int x, int y, uint16_t colour) {
  tft.fillCircle(x, y, 12, colour);
  tft.fillCircle(x + 14, y - 4, 16, colour);
  tft.fillCircle(x + 30, y, 12, colour);
  tft.fillRoundRect(x - 12, y, 54, 16, 4, colour);
}

void drawRainDrops(int x, int y) {
  for (int i = 0; i < 4; i++) {
    int dx = x + i * 12;
    int dy = y + (i % 2) * 6;
    tft.drawLine(dx, dy, dx - 3, dy + 8, C_RAIN_BLUE);
    tft.drawLine(dx + 1, dy, dx - 2, dy + 8, C_RAIN_BLUE);
  }
}

void drawSnowflakes(int x, int y) {
  for (int i = 0; i < 4; i++) {
    int dx = x + i * 12;
    int dy = y + (i % 2) * 8;
    tft.drawPixel(dx, dy, C_SNOW_W);
    tft.drawPixel(dx-2, dy-2, C_SNOW_W);
    tft.drawPixel(dx+2, dy-2, C_SNOW_W);
    tft.drawPixel(dx-2, dy+2, C_SNOW_W);
    tft.drawPixel(dx+2, dy+2, C_SNOW_W);
    tft.drawPixel(dx, dy-3, C_SNOW_W);
    tft.drawPixel(dx, dy+3, C_SNOW_W);
    tft.drawPixel(dx-3, dy, C_SNOW_W);
    tft.drawPixel(dx+3, dy, C_SNOW_W);
  }
}

void drawLightning(int x, int y) {
  // Simple bolt shape
  tft.drawLine(x+6, y, x, y+10, C_YELLOW);
  tft.drawLine(x, y+10, x+8, y+10, C_YELLOW);
  tft.drawLine(x+8, y+10, x+2, y+22, C_YELLOW);
  tft.drawLine(x+7, y, x+1, y+10, C_YELLOW);
  tft.drawLine(x+1, y+10, x+9, y+10, C_YELLOW);
  tft.drawLine(x+9, y+10, x+3, y+22, C_YELLOW);
}

void drawFog(int x, int y) {
  for (int i = 0; i < 4; i++) {
    int dy = y + i * 7;
    int w = 44 - (i % 2) * 8;
    tft.drawFastHLine(x, dy, w, C_LGREY);
    tft.drawFastHLine(x, dy + 1, w, C_LGREY);
  }
}

void drawWeatherIcon(int x, int y, int code) {
  // Clear icon area
  tft.fillRect(x, y, 70, 70, C_PANEL);

  int cx = x + 35;
  int cy = y + 30;

  if (code == 0) {
    // Clear sky — sun
    drawSun(cx, cy, 18);
  } else if (code <= 3) {
    // Cloudy
    if (code == 1) {
      drawSun(cx + 10, cy - 10, 12);
      drawCloud(cx - 16, cy + 2, C_LGREY);
    } else {
      drawCloud(cx - 16, cy - 8, C_LGREY);
      drawCloud(cx - 10, cy + 6, C_DGREY);
    }
  } else if (code <= 49) {
    // Fog
    drawFog(x + 12, y + 14);
  } else if (code <= 69) {
    // Rain / drizzle
    drawCloud(cx - 16, cy - 10, C_DGREY);
    drawRainDrops(x + 14, y + 44);
  } else if (code <= 79) {
    // Snow
    drawCloud(cx - 16, cy - 10, C_LGREY);
    drawSnowflakes(x + 14, y + 44);
  } else if (code <= 84) {
    // Showers
    drawCloud(cx - 16, cy - 10, C_DGREY);
    drawRainDrops(x + 14, y + 42);
  } else {
    // Thunderstorm
    drawCloud(cx - 16, cy - 12, C_DGREY);
    drawLightning(x + 24, y + 38);
  }
}

// ===================== WMO Condition Text =====================

const char* weatherDesc(int code) {
  if (code == 0) return "Clear Sky";
  if (code == 1) return "Mostly Clear";
  if (code == 2) return "Partly Cloudy";
  if (code == 3) return "Overcast";
  if (code <= 49) return "Fog";
  if (code <= 55) return "Drizzle";
  if (code <= 59) return "Freezing Drizzle";
  if (code <= 65) return "Rain";
  if (code <= 69) return "Freezing Rain";
  if (code <= 75) return "Snowfall";
  if (code <= 79) return "Snow Grains";
  if (code <= 82) return "Rain Showers";
  if (code <= 84) return "Snow Showers";
  if (code == 95) return "Thunderstorm";
  return "Storm + Hail";
}

// ===================== Colour for temperature =====================

uint16_t tempColour(float t) {
  if (t <= -5)  return C_BLUE;
  if (t <= 0)   return C_CYAN;
  if (t <= 10)  return C_LGREEN;
  if (t <= 20)  return C_GREEN;
  if (t <= 28)  return C_YELLOW;
  if (t <= 35)  return C_ORANGE;
  return C_RED;
}

// ===================== UI Panels =====================

void drawHeader() {
  tft.fillRect(0, 0, 480, 32, C_HEADER);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("Spalding, UK");

  // Current time
  tft.setTextSize(2);
  tft.setCursor(340, 8);
  tft.print(curTime);

  // WiFi indicator
  tft.setCursor(420, 12);
  tft.setTextSize(1);
  tft.print(WiFi.RSSI());
  tft.print("dBm");
}

void drawTemperaturePanel() {
  // Main temperature panel: left-centre area
  drawRoundPanel(10, 40, 290, 130, C_PANEL);

  // Weather icon
  drawWeatherIcon(18, 50, curWeatherCode);

  // Large temperature
  tft.setTextColor(tempColour(curTemp));
  tft.setTextSize(5);
  tft.setCursor(100, 55);
  if (curTemp >= 0 && curTemp < 10) tft.print(" ");
  tft.print(curTemp, 1);

  // Degree symbol + C
  tft.setTextSize(2);
  tft.setCursor(tft.getCursorX() + 2, 55);
  tft.print("o");
  tft.setTextSize(4);
  tft.setCursor(tft.getCursorX() + 2, 55);
  tft.print("C");

  // Condition text
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(100, 110);
  tft.print(weatherDesc(curWeatherCode));

  // Feels like
  tft.setTextColor(C_LGREY);
  tft.setTextSize(1);
  tft.setCursor(100, 140);
  tft.print("Feels like ");
  tft.print(curFeels, 1);
  tft.print(" C");
}

void drawHumidityPanel() {
  drawRoundPanel(10, 180, 140, 130, C_PANEL);

  tft.setTextColor(C_CYAN);
  tft.setTextSize(1);
  tft.setCursor(20, 190);
  tft.print("HUMIDITY");

  // Large humidity value
  tft.setTextColor(C_WHITE);
  tft.setTextSize(4);
  tft.setCursor(25, 210);
  tft.print(curHumidity);
  tft.setTextSize(2);
  tft.print("%");

  // Humidity bar
  int barW = 110;
  int barH = 10;
  int barX = 20;
  int barY = 260;
  tft.drawRoundRect(barX, barY, barW, barH, 3, C_DGREY);
  int fillW = map(constrain(curHumidity, 0, 100), 0, 100, 0, barW - 2);
  uint16_t barCol = curHumidity > 80 ? C_BLUE : (curHumidity > 50 ? C_CYAN : C_GREEN);
  tft.fillRoundRect(barX + 1, barY + 1, fillW, barH - 2, 2, barCol);

  // Label
  tft.setTextColor(C_LGREY);
  tft.setTextSize(1);
  tft.setCursor(barX, barY + 14);
  tft.print("0%");
  tft.setCursor(barX + barW - 24, barY + 14);
  tft.print("100%");
}

void drawWindPanel() {
  drawRoundPanel(160, 180, 140, 130, C_PANEL);

  tft.setTextColor(C_GREEN);
  tft.setTextSize(1);
  tft.setCursor(170, 190);
  tft.print("WIND SPEED");

  // Large wind value
  tft.setTextColor(C_WHITE);
  tft.setTextSize(4);
  tft.setCursor(170, 210);
  if (curWind < 10) tft.print(" ");
  tft.print(curWind, 0);

  tft.setTextSize(1);
  tft.setCursor(170, 255);
  tft.setTextColor(C_LGREY);
  tft.print("km/h");

  // Beaufort scale description
  tft.setCursor(170, 275);
  tft.setTextColor(C_LGREEN);
  if (curWind < 2)       tft.print("Calm");
  else if (curWind < 12) tft.print("Light breeze");
  else if (curWind < 29) tft.print("Gentle breeze");
  else if (curWind < 50) tft.print("Moderate wind");
  else if (curWind < 75) tft.print("Strong wind");
  else                   tft.print("Gale force!");

  // Wind strength indicator dots
  int dots = min((int)(curWind / 10), 8);
  for (int i = 0; i < 8; i++) {
    uint16_t col = i < dots ? (i < 4 ? C_GREEN : (i < 6 ? C_YELLOW : C_RED)) : C_DGREY;
    tft.fillCircle(175 + i * 14, 292, 4, col);
  }
}

void drawTempChart() {
  // 24h temperature chart on the right strip
  drawRoundPanel(310, 40, 160, 270, C_PANEL);

  tft.setTextColor(C_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(320, 50);
  tft.print("24h TEMPERATURE");

  if (hourlyCount < 2) {
    tft.setCursor(330, 160);
    tft.setTextColor(C_DGREY);
    tft.print("No data");
    return;
  }

  // Chart area
  int chartX = 345, chartY = 68;
  int chartW = 115, chartH = 220;

  // Y-axis labels
  tft.setTextColor(C_LGREY);
  tft.setTextSize(1);

  float range = tempMax24 - tempMin24;
  if (range < 2) range = 2;  // minimum range
  float padMin = tempMin24 - range * 0.1;
  float padMax = tempMax24 + range * 0.1;
  float padRange = padMax - padMin;

  // Draw grid lines and labels
  for (int i = 0; i <= 4; i++) {
    float t = padMin + (padRange * i / 4);
    int y = chartY + chartH - (int)((t - padMin) / padRange * chartH);
    tft.drawFastHLine(chartX, y, chartW, C_DGREY);
    tft.setCursor(318, y - 3);
    tft.print((int)round(t));
  }

  // X-axis labels (hours)
  tft.setCursor(chartX, chartY + chartH + 4);
  tft.print("0h");
  tft.setCursor(chartX + chartW - 18, chartY + chartH + 4);
  tft.print("23h");

  // Plot temperature line
  for (int i = 1; i < hourlyCount && i < 24; i++) {
    int x0 = chartX + (int)((float)(i - 1) / 23 * chartW);
    int x1 = chartX + (int)((float)i / 23 * chartW);
    int y0 = chartY + chartH - (int)((hourlyTemp[i-1] - padMin) / padRange * chartH);
    int y1 = chartY + chartH - (int)((hourlyTemp[i] - padMin) / padRange * chartH);
    y0 = constrain(y0, chartY, chartY + chartH);
    y1 = constrain(y1, chartY, chartY + chartH);
    tft.drawLine(x0, y0, x1, y1, C_YELLOW);
    tft.drawLine(x0, y0+1, x1, y1+1, C_YELLOW);
  }

  // Mark current hour with a dot
  // (rough approximation — millis since midnight not available,
  //  but we can highlight the last known data point)
}

void drawStatusBar() {
  tft.fillRect(0, 312, 480, 8, C_BG);
  tft.setTextColor(C_DGREY);
  tft.setTextSize(1);
  tft.setCursor(10, 313);
  tft.print("Open-Meteo API | 10 min refresh | UNO R4 WiFi + ILI9486");
}

void drawFullDashboard() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawTemperaturePanel();
  drawHumidityPanel();
  drawWindPanel();
  drawTempChart();
  drawStatusBar();
}

void drawSplash(const char* line1, const char* line2) {
  tft.fillScreen(C_BG);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(3);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((480 - w) / 2, 120);
  tft.print(line1);

  if (line2) {
    tft.setTextSize(2);
    tft.setTextColor(C_LGREY);
    tft.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((480 - w) / 2, 170);
    tft.print(line2);
  }
}

// ===================== WiFi =====================

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.print("Connecting WiFi...");
  drawSplash("Connecting WiFi", ssid);

  WiFi.begin(ssid, pass);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
    // Animated dots
    tft.setTextSize(3);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(200 + (attempts % 4) * 24, 210);
    tft.print(".");
    if (attempts % 4 == 0) {
      tft.fillRect(200, 210, 120, 30, C_BG);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("OK IP:");
    Serial.println(WiFi.localIP());
    delay(1000);
    return true;
  }

  Serial.println("Failed");
  return false;
}

// ===================== JSON Parsing =====================

float parseFloat(const String& resp, const char* key, int fromIdx) {
  int idx = resp.indexOf(key, fromIdx);
  if (idx == -1) return -999;
  int colon = resp.indexOf(':', idx);
  if (colon == -1) return -999;

  String num = "";
  for (int i = colon + 1; i < (int)resp.length() && i < colon + 15; i++) {
    char c = resp.charAt(i);
    if (c == '-' || c == '.' || (c >= '0' && c <= '9')) num += c;
    else if (num.length() > 0) break;
  }
  return num.length() > 0 ? num.toFloat() : -999;
}

int parseInt(const String& resp, const char* key, int fromIdx) {
  float v = parseFloat(resp, key, fromIdx);
  return v > -998 ? (int)v : -999;
}

void parseHourlyTemps(const String& resp) {
  int block = resp.indexOf("\"hourly\"");
  if (block == -1) return;
  int tempArr = resp.indexOf("\"temperature_2m\"", block);
  if (tempArr == -1) return;
  int bracket = resp.indexOf('[', tempArr);
  if (bracket == -1) return;
  int end = resp.indexOf(']', bracket);
  if (end == -1) return;

  hourlyCount = 0;
  tempMin24 = 99;
  tempMax24 = -99;
  int pos = bracket + 1;

  while (pos < end && hourlyCount < 24) {
    // Skip whitespace/commas
    while (pos < end && (resp[pos] == ',' || resp[pos] == ' ')) pos++;
    if (pos >= end) break;

    String num = "";
    while (pos < end && resp[pos] != ',' && resp[pos] != ']') {
      num += resp[pos];
      pos++;
    }
    num.trim();
    if (num.length() > 0 && num != "null") {
      float t = num.toFloat();
      hourlyTemp[hourlyCount++] = t;
      if (t < tempMin24) tempMin24 = t;
      if (t > tempMax24) tempMax24 = t;
    }
  }

  Serial.print("Hourly temps parsed: ");
  Serial.print(hourlyCount);
  Serial.print(" min=");
  Serial.print(tempMin24);
  Serial.print(" max=");
  Serial.println(tempMax24);
}

// ===================== HTTP Fetch =====================

bool fetchWeather() {
  WiFiClient client;

  Serial.println("Fetching weather...");

  bool connected = false;
  for (int retry = 0; retry < 3 && !connected; retry++) {
    if (retry > 0) delay(2000);
    connected = client.connect(host, 80);
  }
  if (!connected) {
    Serial.println("Connect failed");
    return false;
  }

  client.print("GET ");
  client.print(path);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(host);
  client.println("Connection: close");
  client.println();

  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 15000) {
      client.stop();
      return false;
    }
    delay(100);
  }

  String response = "";
  while (client.connected() || client.available()) {
    if (client.available()) response += (char)client.read();
    else delay(10);
  }
  client.stop();

  // Parse current conditions
  int cur = response.indexOf("\"current\"");
  if (cur == -1) return false;

  // Parse time — "time":"2024-01-15T17:30"
  int timeKey = response.indexOf("\"time\"", cur);
  if (timeKey != -1) {
    int tPos = response.indexOf('T', timeKey);
    if (tPos != -1 && tPos + 5 < (int)response.length()) {
      curTime[0] = response[tPos + 1];
      curTime[1] = response[tPos + 2];
      curTime[2] = ':';
      curTime[3] = response[tPos + 4];
      curTime[4] = response[tPos + 5];
      curTime[5] = '\0';
    }
  }

  curTemp = parseFloat(response, "\"temperature_2m\":", cur);
  curHumidity = parseInt(response, "\"relative_humidity_2m\":", cur);
  curWind = parseFloat(response, "\"wind_speed_10m\":", cur);
  curWeatherCode = parseInt(response, "\"weather_code\":", cur);
  curFeels = parseFloat(response, "\"apparent_temperature\":", cur);

  Serial.print("T:"); Serial.print(curTemp);
  Serial.print(" H:"); Serial.print(curHumidity);
  Serial.print(" W:"); Serial.print(curWind);
  Serial.print(" WMO:"); Serial.print(curWeatherCode);
  Serial.print(" Feels:"); Serial.println(curFeels);

  // Parse hourly data
  parseHourlyTemps(response);

  return curTemp > -999;
}

// ===================== Main =====================

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("TFT Weather Station starting...");

  uint16_t id = tft.readID();
  if (id == 0x0 || id == 0xD3D3 || id == 0xFFFF || id == 0x9494) {
    id = 0x9486;
  }
  tft.begin(id);
  tft.setRotation(1);  // landscape 480x320

  Serial.print("TFT ID: 0x");
  Serial.print(id, HEX);
  Serial.print(" Size: ");
  Serial.print(tft.width());
  Serial.print("x");
  Serial.println(tft.height());

  drawSplash("Weather Station", "Spalding, UK");
  delay(1500);
}

void loop() {
  unsigned long now = millis();

  if (firstRun || (now - lastFetch >= REFRESH_MS)) {
    firstRun = false;
    lastFetch = now;

    if (connectWiFi()) {
      drawSplash("Fetching weather", "Please wait...");

      if (fetchWeather()) {
        Serial.println("Drawing dashboard...");
        drawFullDashboard();
        Serial.println("Dashboard complete.");
      } else {
        drawSplash("Fetch failed", "Retrying in 10 min");
      }
    } else {
      drawSplash("WiFi failed", "Check connection");
    }
  }
}
