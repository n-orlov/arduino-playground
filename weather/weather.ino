#include "Arduino_LED_Matrix.h"
#include <WiFiS3.h>

ArduinoLEDMatrix matrix;

const char* ssid = "YOUR_SSID";
const char* pass = "YOUR_PASSWORD";

const char* host = "api.open-meteo.com";
const char* path = "/v1/forecast?latitude=52.79&longitude=-0.15&current=temperature_2m&forecast_days=1";

const unsigned long REFRESH_MS = 600000; // 10 minutes
unsigned long lastFetch = 0;
bool firstRun = true;

// 3x5 font for digits 0-9, minus sign, decimal point, degree symbol, C
// Each character is 3 columns wide, 5 rows tall
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
  { 0b000, 0b000, 0b000, 0b000, 0b010 }, // 11 = decimal point (1 wide)
  { 0b010, 0b101, 0b010, 0b000, 0b000 }, // 12 = degree symbol
  { 0b111, 0b100, 0b100, 0b100, 0b111 }, // 13 = C
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

void showTemp(float temp) {
  byte frame[8][12] = {};

  // Build the string of character indices
  int chars[8];
  int nChars = 0;

  // Round to nearest integer, show like "-7C"
  int rounded = (int)(temp + (temp >= 0 ? 0.5 : -0.5));
  bool negative = rounded < 0;
  if (negative) rounded = -rounded;

  if (negative) chars[nChars++] = 10; // minus

  if (rounded >= 10) {
    chars[nChars++] = rounded / 10;
  }
  chars[nChars++] = rounded % 10;
  chars[nChars++] = 13; // C

  // Calculate total width: each char is 3 wide + 1 gap, except decimal = 1 wide + 1 gap
  int totalWidth = 0;
  for (int i = 0; i < nChars; i++) {
    totalWidth += 4; // each char is 3 wide + 1 gap
  }
  totalWidth--; // no trailing gap

  // Center on the 12-wide display, vertically offset by 1 from top
  int xStart = (12 - totalWidth) / 2;
  int yStart = 2; // vertically center 5px tall font in 8px

  int x = xStart;
  for (int i = 0; i < nChars; i++) {
    drawChar(frame, chars[i], x, yStart);
    x += 4;
  }

  matrix.renderBitmap(frame, 8, 12);
}

// Show a simple "connecting" animation
void showConnecting() {
  byte frame[8][12] = {};
  // Three dots that animate
  static int dot = 0;
  for (int i = 0; i <= dot; i++) {
    int x = 3 + i * 3;
    frame[4][x] = 1;
  }
  dot = (dot + 1) % 3;
  matrix.renderBitmap(frame, 8, 12);
}

// Show error X
void showError() {
  byte frame[8][12] = {};
  frame[1][3] = 1; frame[1][8] = 1;
  frame[2][4] = 1; frame[2][7] = 1;
  frame[3][5] = 1; frame[3][6] = 1;
  frame[4][5] = 1; frame[4][6] = 1;
  frame[5][4] = 1; frame[5][7] = 1;
  frame[6][3] = 1; frame[6][8] = 1;
  matrix.renderBitmap(frame, 8, 12);
}

bool connectWiFi() {
  int status = WiFi.status();
  Serial.print("WiFi status: ");
  Serial.println(status);

  if (status == WL_CONNECTED) return true;

  Serial.print("Connecting to SSID: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    showConnecting();
    delay(500);
    Serial.print("Attempt ");
    Serial.print(attempts);
    Serial.print(" status=");
    Serial.println(WiFi.status());
    attempts++;
  }

  status = WiFi.status();
  Serial.print("Final WiFi status: ");
  Serial.println(status);

  if (status == WL_CONNECTED) {
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength (RSSI): ");
    Serial.println(WiFi.RSSI());
    delay(1000); // Let network stack stabilize
    return true;
  }

  Serial.println("WiFi connection FAILED");
  // Status codes: 0=IDLE, 1=NO_SSID_AVAIL, 4=CONNECT_FAILED, 6=DISCONNECTED
  return false;
}

float fetchTemperature() {
  WiFiClient client;
  float temp = -999;

  Serial.println("Fetching weather...");
  Serial.print("Connecting to ");
  Serial.print(host);
  Serial.println(":80 ...");

  // Try connecting with retries
  bool connected = false;
  for (int retry = 0; retry < 3 && !connected; retry++) {
    if (retry > 0) {
      Serial.print("Retry ");
      Serial.println(retry);
      delay(2000);
    }
    connected = client.connect(host, 80);
  }
  if (!connected) {
    Serial.println("Connection to server failed after 3 attempts");
    return temp;
  }
  Serial.println("Connected to server!");

  client.print("GET ");
  client.print(path);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(host);
  client.println("Connection: close");
  client.println();

  // Wait for response with timeout
  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 15000) {
      Serial.println("HTTP timeout");
      client.stop();
      return temp;
    }
    delay(100);
  }

  // Read entire response, waiting for all data
  String response = "";
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += (char)client.read();
    } else {
      delay(10);
    }
  }
  client.stop();

  // Find "temperature_2m": in the JSON
  int idx = response.indexOf("\"temperature_2m\":");
  if (idx == -1) {
    // Try in the current object block (after "current":{)
    idx = response.indexOf("temperature_2m\":");
  }

  if (idx != -1) {
    // Find the value - scan past "temperature_2m": to the number
    // There are two occurrences: one in current_units and one in current
    // We want the one in "current" block (second occurrence)
    int secondIdx = response.indexOf("\"temperature_2m\":", idx + 1);
    if (secondIdx != -1) idx = secondIdx;

    int colonPos = response.indexOf(':', idx);
    if (colonPos != -1) {
      // Extract the number after the colon
      String numStr = "";
      for (int i = colonPos + 1; i < response.length() && i < colonPos + 15; i++) {
        char c = response.charAt(i);
        if (c == '-' || c == '.' || (c >= '0' && c <= '9')) {
          numStr += c;
        } else if (numStr.length() > 0) {
          break;
        }
      }
      if (numStr.length() > 0) {
        temp = numStr.toFloat();
        Serial.print("Temperature: ");
        Serial.print(temp);
        Serial.println(" C");
      }
    }
  } else {
    Serial.println("Could not parse temperature from response");
    Serial.println(response.substring(response.length() - 200));
  }

  return temp;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  matrix.begin();

  Serial.println("Weather Station - Spalding, UK");
}

void loop() {
  unsigned long now = millis();

  if (firstRun || (now - lastFetch >= REFRESH_MS)) {
    firstRun = false;
    lastFetch = now;

    if (connectWiFi()) {
      float temp = fetchTemperature();
      if (temp > -999) {
        showTemp(temp);
      } else {
        showError();
      }
    } else {
      showError();
    }
  }
}
