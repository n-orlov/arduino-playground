#include "Arduino_LED_Matrix.h"
#include <WiFiS3.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>

// --- Config ---
const char* ssid = "YOUR_SSID";
const char* pass = "YOUR_PASSWORD";
const char* host = "api.open-meteo.com";
const char* path = "/v1/forecast?latitude=52.79&longitude=-0.15"
                   "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code"
                   "&forecast_days=1";
const unsigned long REFRESH_MS = 600000; // 10 minutes

// --- Hardware ---
ArduinoLEDMatrix matrix;
hd44780_pinIO lcd(8, 9, 4, 5, 6, 7); // RS=8, E=9, D4=4, D5=5, D6=6, D7=7

// --- State ---
unsigned long lastFetch = 0;
bool firstRun = true;

// --- 3x5 pixel font for LED matrix (digits 0-9, minus, C) ---
const byte font3x5[][5] = {
  { 0b111, 0b101, 0b101, 0b101, 0b111 }, // 0
  { 0b010, 0b110, 0b010, 0b010, 0b111 }, // 1
  { 0b111, 0b001, 0b111, 0b100, 0b111 }, // 2
  { 0b111, 0b001, 0b111, 0b001, 0b111 }, // 3
  { 0b101, 0b101, 0b111, 0b001, 0b001 }, // 4
  { 0b111, 0b100, 0b111, 0b001, 0b111 }, // 5
  { 0b111, 0b100, 0b111, 0b101, 0b111 }, // 6
  { 0b111, 0b001, 0b010, 0b010, 0b010 }, // 7
  { 0b111, 0b101, 0b111, 0b101, 0b111 }, // 8
  { 0b111, 0b101, 0b111, 0b001, 0b111 }, // 9
  { 0b000, 0b000, 0b111, 0b000, 0b000 }, // 10 = minus
  { 0b111, 0b100, 0b100, 0b100, 0b111 }, // 11 = C
};

void drawChar(byte frame[8][12], int charIdx, int xOff, int yOff) {
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      int x = xOff + col;
      int y = yOff + row;
      if (x >= 0 && x < 12 && y >= 0 && y < 8) {
        if (font3x5[charIdx][row] & (0b100 >> col)) {
          frame[y][x] = 1;
        }
      }
    }
  }
}

// LED matrix: show rounded temp like "7C" or "-3C"
void matrixShowTemp(float temp) {
  byte frame[8][12] = {};
  int chars[6];
  int nChars = 0;

  int rounded = (int)(temp + (temp >= 0 ? 0.5 : -0.5));
  bool negative = rounded < 0;
  if (negative) rounded = -rounded;

  if (negative) chars[nChars++] = 10;
  if (rounded >= 10) chars[nChars++] = rounded / 10;
  chars[nChars++] = rounded % 10;
  chars[nChars++] = 11; // C

  int totalWidth = nChars * 4 - 1;
  int x = (12 - totalWidth) / 2;

  for (int i = 0; i < nChars; i++) {
    drawChar(frame, chars[i], x, 2);
    x += 4;
  }

  matrix.renderBitmap(frame, 8, 12);
}

void matrixShowConnecting() {
  byte frame[8][12] = {};
  static int dot = 0;
  for (int i = 0; i <= dot; i++) {
    frame[4][3 + i * 3] = 1;
  }
  dot = (dot + 1) % 3;
  matrix.renderBitmap(frame, 8, 12);
}

void matrixShowError() {
  byte frame[8][12] = {};
  frame[1][3] = 1; frame[1][8] = 1;
  frame[2][4] = 1; frame[2][7] = 1;
  frame[3][5] = 1; frame[3][6] = 1;
  frame[4][5] = 1; frame[4][6] = 1;
  frame[5][4] = 1; frame[5][7] = 1;
  frame[6][3] = 1; frame[6][8] = 1;
  matrix.renderBitmap(frame, 8, 12);
}

// WMO weather code to short description
const char* weatherDesc(int code) {
  if (code == 0)  return "Clear";
  if (code <= 3)  return "Cloudy";
  if (code <= 49) return "Fog";
  if (code <= 59) return "Drizzle";
  if (code <= 69) return "Rain";
  if (code <= 79) return "Snow";
  if (code <= 84) return "Showers";
  if (code <= 94) return "Snow shwrs";
  return "Storm";
}

// LCD: show full weather details
void lcdShowWeather(float temp, int humidity, float wind, int weatherCode) {
  lcd.clear();

  // Line 1: temp + condition
  lcd.setCursor(0, 0);
  lcd.print(temp, 1);
  lcd.print("C ");
  lcd.print(weatherDesc(weatherCode));

  // Line 2: humidity + wind
  lcd.setCursor(0, 1);
  lcd.print("H:");
  lcd.print(humidity);
  lcd.print("% W:");
  lcd.print(wind, 0);
  lcd.print("km/h");
}

void lcdShowStatus(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  if (line2) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }
}

// --- WiFi ---
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.print("Connecting to WiFi...");
  lcdShowStatus("Connecting to", "WiFi...");

  WiFi.begin(ssid, pass);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    matrixShowConnecting();
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
    delay(1000);
    return true;
  }

  Serial.println("WiFi failed");
  return false;
}

// --- Parse a float value after a key in the response ---
float parseFloat(const String& resp, const char* key, int fromIdx) {
  int idx = resp.indexOf(key, fromIdx);
  if (idx == -1) return -999;
  int colon = resp.indexOf(':', idx);
  if (colon == -1) return -999;

  String num = "";
  for (int i = colon + 1; i < resp.length() && i < colon + 15; i++) {
    char c = resp.charAt(i);
    if (c == '-' || c == '.' || (c >= '0' && c <= '9')) num += c;
    else if (num.length() > 0) break;
  }
  return num.length() > 0 ? num.toFloat() : -999;
}

int parseInt(const String& resp, const char* key, int fromIdx) {
  float v = parseFloat(resp, key, fromIdx);
  return v > -999 ? (int)v : -999;
}

// --- HTTP fetch ---
bool fetchWeather(float &temp, int &humidity, float &wind, int &weatherCode) {
  WiFiClient client;

  Serial.println("Fetching weather...");
  lcdShowStatus("Spalding, UK", "Fetching...");

  bool connected = false;
  for (int retry = 0; retry < 3 && !connected; retry++) {
    if (retry > 0) delay(2000);
    connected = client.connect(host, 80);
  }
  if (!connected) {
    Serial.println("Server connection failed");
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

  // Find the "current":{ block (second occurrence of keys)
  int currentBlock = response.indexOf("\"current\":{");
  if (currentBlock == -1) currentBlock = response.indexOf("\"current\": {");
  if (currentBlock == -1) return false;

  temp = parseFloat(response, "\"temperature_2m\":", currentBlock);
  humidity = parseInt(response, "\"relative_humidity_2m\":", currentBlock);
  wind = parseFloat(response, "\"wind_speed_10m\":", currentBlock);
  weatherCode = parseInt(response, "\"weather_code\":", currentBlock);

  Serial.print("T:"); Serial.print(temp);
  Serial.print(" H:"); Serial.print(humidity);
  Serial.print(" W:"); Serial.print(wind);
  Serial.print(" WMO:"); Serial.println(weatherCode);

  return temp > -999;
}

// --- Main ---
void setup() {
  Serial.begin(115200);
  delay(1000);
  matrix.begin();
  lcd.begin(16, 2);

  Serial.println("Weather Station - Spalding, UK");
  lcdShowStatus("Weather Station", "Spalding, UK");
  delay(1500);
}

void loop() {
  unsigned long now = millis();

  if (firstRun || (now - lastFetch >= REFRESH_MS)) {
    firstRun = false;
    lastFetch = now;

    if (connectWiFi()) {
      float temp;
      int humidity, weatherCode;
      float wind;

      if (fetchWeather(temp, humidity, wind, weatherCode)) {
        matrixShowTemp(temp);
        lcdShowWeather(temp, humidity, wind, weatherCode);
      } else {
        matrixShowError();
        lcdShowStatus("Fetch failed", "Retrying...");
      }
    } else {
      matrixShowError();
      lcdShowStatus("WiFi failed", "Check connection");
    }
  }
}
