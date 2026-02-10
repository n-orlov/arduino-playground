// Challenger NB RP2040 WiFi — Weather dashboard
// Fetches weather for Spalding, UK and shows temperature as NeoPixel colour.
// Also runs a web server with a live dashboard (board status + nearby networks).
// NeoPixel: blue=freezing, cyan=cold, green=mild, yellow=warm, red=hot
// Refreshes weather every 10 minutes.

#include <ChallengerWiFi.h>
#include <WiFiEspAT.h>
#include <Adafruit_NeoPixel.h>

const char* ssid = "YOUR_SSID";
const char* pass = "YOUR_PASSWORD";

const char* weatherHost = "api.open-meteo.com";
const char* weatherPath = "/v1/forecast?latitude=52.79&longitude=-0.15"
                          "&current=temperature_2m,relative_humidity_2m,"
                          "wind_speed_10m,weather_code&forecast_days=1";

const unsigned long REFRESH_MS = 600000; // 10 minutes
unsigned long lastFetch = 0;
bool firstRun = true;

// Cached weather values
float curTemp = -999;
float curHumidity = -999;
float curWind = -999;
int curWeatherCode = -999;
String curTime = "";            // API observation time (ISO 8601)
unsigned long lastFetchMs = 0;  // millis() when weather was last fetched


Adafruit_NeoPixel pixel(1, NEOPIXEL, NEO_GRB + NEO_KHZ800);
WiFiServer server(80);

// --- NeoPixel helpers (only call when no WiFi comms active) ---

void tempToColour(float temp, uint8_t& r, uint8_t& g, uint8_t& b) {
  float t = constrain(temp, -10, 35);
  if (t <= 0) {
    float f = (t + 10.0) / 10.0;
    r = 0; g = (uint8_t)(f * 255); b = 255;
  } else if (t <= 10) {
    float f = t / 10.0;
    r = 0; g = 255; b = (uint8_t)((1.0 - f) * 255);
  } else if (t <= 20) {
    float f = (t - 10.0) / 10.0;
    r = (uint8_t)(f * 255); g = 255; b = 0;
  } else {
    float f = (t - 20.0) / 15.0;
    r = 255; g = (uint8_t)((1.0 - f) * 255); b = 0;
  }
}

void setPixel(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r / 4, g / 4, b / 4));
  pixel.show();
}

// --- Weather helpers ---

const char* weatherDesc(int code) {
  if (code == 0) return "Clear sky";
  if (code == 1) return "Mainly clear";
  if (code == 2) return "Partly cloudy";
  if (code == 3) return "Overcast";
  if (code <= 49) return "Fog";
  if (code <= 59) return "Drizzle";
  if (code <= 69) return "Rain";
  if (code <= 79) return "Snow";
  if (code <= 82) return "Rain showers";
  if (code <= 86) return "Snow showers";
  if (code >= 95) return "Thunderstorm";
  return "Unknown";
}

float extractFloat(const String& json, const char* key) {
  int idx = json.indexOf(key);
  if (idx != -1) {
    int secondIdx = json.indexOf(key, idx + 1);
    if (secondIdx != -1) idx = secondIdx;
  }
  if (idx == -1) return -999;
  int colon = json.indexOf(':', idx);
  if (colon == -1) return -999;
  String numStr;
  for (int i = colon + 1; i < (int)json.length() && i < colon + 15; i++) {
    char c = json.charAt(i);
    if (c == '-' || c == '.' || (c >= '0' && c <= '9')) {
      numStr += c;
    } else if (numStr.length() > 0) {
      break;
    }
  }
  return numStr.length() > 0 ? numStr.toFloat() : -999;
}

int extractInt(const String& json, const char* key) {
  float v = extractFloat(json, key);
  return v > -998 ? (int)v : -999;
}

// --- WiFi ---

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // WiFi.begin() is blocking in WiFiEspAT — retry the call itself
  for (int attempt = 0; attempt < 5; attempt++) {
    if (attempt > 0) {
      Serial.print("  Retry ");
      Serial.println(attempt);
      // Hardware reset the ESP between retries
      digitalWrite(PIN_ESP8285_RST, LOW);
      delay(100);
      digitalWrite(PIN_ESP8285_RST, HIGH);
      delay(2000);
      while (Serial2.available()) Serial2.read();
      WiFi.init(Serial2);
      delay(1000);
    }
    if (WiFi.begin(ssid, pass) == WL_CONNECTED) {
      Serial.print("Connected! IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      return true;
    }
  }

  Serial.println("WiFi connection failed");
  return false;
}

// --- Weather fetch ---

void fetchWeather() {
  WiFiClient client;
  Serial.println("\n--- Fetching weather ---");

  bool connected = false;
  for (int retry = 0; retry < 3 && !connected; retry++) {
    if (retry > 0) { Serial.print("Retry "); Serial.println(retry); delay(2000); }
    connected = client.connect(weatherHost, 80);
  }
  if (!connected) {
    Serial.println("Server connection failed");
    return;
  }

  client.print("GET ");
  client.print(weatherPath);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(weatherHost);
  client.println("Connection: close");
  client.println();

  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 15000) {
      Serial.println("HTTP timeout");
      client.stop();
      return;
    }
    delay(100);
  }

  String response;
  while (client.connected() || client.available()) {
    if (client.available()) response += (char)client.read();
    else delay(10);
  }
  client.stop();

  float temp = extractFloat(response, "\"temperature_2m\"");
  float humidity = extractFloat(response, "\"relative_humidity_2m\"");
  float wind = extractFloat(response, "\"wind_speed_10m\"");
  int weatherCode = extractInt(response, "\"weather_code\"");

  if (temp < -998) {
    Serial.println("Failed to parse weather data");
    return;
  }

  // Extract observation time from "time":"2026-02-08T21:30"
  String timeStr = "";
  int timeIdx = response.indexOf("\"time\":\"");
  // Skip the first "time" (in current_units) — find the one in "current" block
  if (timeIdx != -1) {
    int secondIdx = response.indexOf("\"time\":\"", timeIdx + 1);
    if (secondIdx != -1) timeIdx = secondIdx;
  }
  if (timeIdx != -1) {
    int start = timeIdx + 8; // skip past "time":"
    int end = response.indexOf('"', start);
    if (end != -1) timeStr = response.substring(start, end);
  }

  // Cache values
  curTemp = temp;
  curHumidity = humidity;
  curWind = wind;
  curWeatherCode = weatherCode;
  curTime = timeStr;
  lastFetchMs = millis();

  Serial.println("Spalding, UK:");
  Serial.print("  Temperature: "); Serial.print(temp, 1); Serial.println(" C");
  if (humidity > -998) { Serial.print("  Humidity:    "); Serial.print(humidity, 0); Serial.println("%"); }
  if (wind > -998) { Serial.print("  Wind:        "); Serial.print(wind, 1); Serial.println(" km/h"); }
  if (weatherCode > -998) { Serial.print("  Conditions:  "); Serial.println(weatherDesc(weatherCode)); }

  // Update NeoPixel after comms are done
  uint8_t r, g, b;
  tempToColour(temp, r, g, b);
  setPixel(r, g, b);
  int rounded = (int)(temp + (temp >= 0 ? 0.5 : -0.5));
  Serial.print("NeoPixel: "); Serial.print(rounded); Serial.println("C");

  // Restart server — outgoing connection may have disrupted the listener
  server.begin();
  Serial.println("Server restarted");
}

// --- Web server ---

String uptimeStr() {
  unsigned long s = millis() / 1000;
  unsigned long m = s / 60;
  unsigned long h = m / 60;
  s %= 60; m %= 60;
  String out;
  if (h > 0) { out += h; out += "h "; }
  if (m > 0 || h > 0) { out += m; out += "m "; }
  out += s; out += "s";
  return out;
}

void serveClient(WiFiClient& client) {
  String request = client.readStringUntil('\n');
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line.length() <= 1) break;
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html><html><head>");
  client.println("<meta charset='utf-8'>");
  client.println("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  client.println("<meta http-equiv='refresh' content='30'>");
  client.println("<title>Challenger RP2040</title>");
  client.println("<style>");
  client.println("body{font-family:monospace;background:#1a1a2e;color:#e0e0e0;margin:2em;line-height:1.6}");
  client.println("h1{color:#0f0;font-size:1.4em}h2{color:#0af;font-size:1.1em;margin-top:1.5em}");
  client.println("table{border-collapse:collapse;width:100%;margin:0.5em 0}");
  client.println("td,th{border:1px solid #333;padding:6px 10px;text-align:left}");
  client.println("th{background:#16213e}.info td:first-child{color:#888;width:40%}");
  client.println(".big{font-size:2em;text-align:center;padding:0.5em;margin:0.5em 0;border-radius:8px}");
  client.println("</style></head><body>");

  client.println("<h1>> Challenger NB RP2040 WiFi</h1>");

  // Weather section
  client.println("<h2>Weather — Spalding, UK</h2>");
  if (curTemp > -998) {
    // Colour-coded temperature box
    uint8_t r, g, b;
    tempToColour(curTemp, r, g, b);
    client.print("<div class='big' style='background:rgb(");
    client.print(r); client.print(","); client.print(g); client.print(","); client.print(b);
    client.print(");color:");
    // Dark text on bright backgrounds
    client.print((r + g) > 300 ? "#000" : "#fff");
    client.print("'>");
    client.print(curTemp, 1);
    client.println(" &deg;C</div>");

    client.println("<table class='info'>");
    if (curHumidity > -998) {
      client.print("<tr><td>Humidity</td><td>"); client.print(curHumidity, 0); client.println("%</td></tr>");
    }
    if (curWind > -998) {
      client.print("<tr><td>Wind</td><td>"); client.print(curWind, 1); client.println(" km/h</td></tr>");
    }
    if (curWeatherCode > -998) {
      client.print("<tr><td>Conditions</td><td>"); client.print(weatherDesc(curWeatherCode)); client.println("</td></tr>");
    }
    if (curTime.length() > 0) {
      client.print("<tr><td>Observation</td><td>"); client.print(curTime); client.println(" UTC</td></tr>");
    }
    if (lastFetchMs > 0) {
      unsigned long ago = (millis() - lastFetchMs) / 1000;
      client.print("<tr><td>Fetched</td><td>");
      if (ago < 60) { client.print(ago); client.print("s ago"); }
      else { client.print(ago / 60); client.print("m ago"); }
      client.println("</td></tr>");
    }
    client.println("</table>");
  } else {
    client.println("<p>Waiting for first weather fetch...</p>");
  }

  // Board status
  client.println("<h2>Board Status</h2>");
  client.println("<table class='info'>");
  client.print("<tr><td>IP Address</td><td>"); client.print(WiFi.localIP()); client.println("</td></tr>");
  client.print("<tr><td>Connected to</td><td>"); client.print(WiFi.SSID()); client.println("</td></tr>");
  client.print("<tr><td>Signal</td><td>"); client.print(WiFi.RSSI()); client.println(" dBm</td></tr>");
  client.print("<tr><td>Uptime</td><td>"); client.print(uptimeStr()); client.println("</td></tr>");
  client.print("<tr><td>Free heap</td><td>"); client.print(rp2040.getFreeHeap()); client.println(" bytes</td></tr>");
  client.print("<tr><td>CPU temp</td><td>"); client.print(analogReadTemp(), 1); client.println(" &deg;C</td></tr>");
  client.println("</table>");

  client.print("<p style='margin-top:2em;color:#555'>Page loaded at uptime ");
  client.print(uptimeStr());
  client.println(" &middot; auto-refreshes every 30s</p>");
  client.println("</body></html>");

  client.flush();
  delay(500); // let ESP8285 AT firmware finish sending over the wire
  client.stop();
}

// --- Main ---

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("Challenger NB RP2040 WiFi — Weather Dashboard");

  pixel.begin();
  pixel.clear();
  pixel.show();

  // Reset ESP8285 with a generous pulse
  pinMode(PIN_ESP8285_RST, OUTPUT);
  pinMode(PIN_ESP8285_MODE, OUTPUT);
  digitalWrite(PIN_ESP8285_MODE, HIGH);
  digitalWrite(PIN_ESP8285_RST, LOW);
  delay(100);
  digitalWrite(PIN_ESP8285_RST, HIGH);
  delay(2000); // let the ESP fully boot
  Serial2.begin(DEFAULT_ESP_BAUDRATE);
  // Flush any boot garbage
  while (Serial2.available()) Serial2.read();
  Serial.println("ESP8285 ready");

  WiFi.init(Serial2);
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found — halting");
    setPixel(255, 0, 0);
    while (true) delay(1000);
  }

  connectWiFi(); // will retry in loop if it fails here
  server.begin();
  Serial.print("Dashboard: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
}

unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000; // wait 30s between reconnect attempts

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastReconnectAttempt < RECONNECT_INTERVAL) return;
  lastReconnectAttempt = now;
  Serial.println("WiFi dropped — reconnecting...");
  setPixel(255, 0, 0); // Red while disconnected
  if (connectWiFi()) {
    server.begin();
    Serial.print("Back online: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
  }
}

void loop() {
  // Check WiFi health every loop iteration
  ensureWiFi();

  // Periodic weather fetch
  unsigned long now = millis();
  if (firstRun || (now - lastFetch >= REFRESH_MS)) {
    firstRun = false;
    lastFetch = now;
    if (WiFi.status() == WL_CONNECTED) {
      fetchWeather();
    }
  }

  // Handle web clients
  WiFiClient client = server.available();
  if (client) {
    Serial.println("Web client connected");
    serveClient(client);
    Serial.println("Web client served");
  }
}
