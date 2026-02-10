// TFT LCD Shield — MCUFRIEND_kbv test on UNO R4 WiFi
// ILI9486 confirmed via register read (0xD3 = 0x94)

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>

MCUFRIEND_kbv tft;

void setup() {
  Serial.begin(9600);
  delay(3000);

  Serial.println("=== MCUFRIEND_kbv TFT Test (digitalWrite mode) ===");

  uint16_t id = tft.readID();
  Serial.print("readID() = 0x");
  Serial.println(id, HEX);

  // Force ILI9486 if ID not detected
  if (id == 0x0 || id == 0xD3D3 || id == 0xFFFF || id == 0x9494) {
    id = 0x9486;
    Serial.println("Forcing 0x9486");
  }

  tft.begin(id);
  tft.setRotation(1);  // landscape 480x320
  Serial.print("Size: ");
  Serial.print(tft.width());
  Serial.print("x");
  Serial.println(tft.height());

  Serial.println("fillScreen RED...");
  tft.fillScreen(TFT_RED);
  Serial.println("Done fillScreen");

  // Colour bands
  int bandH = tft.height() / 6;
  tft.fillRect(0, 0 * bandH, tft.width(), bandH, TFT_RED);
  tft.fillRect(0, 1 * bandH, tft.width(), bandH, TFT_GREEN);
  tft.fillRect(0, 2 * bandH, tft.width(), bandH, TFT_BLUE);
  tft.fillRect(0, 3 * bandH, tft.width(), bandH, TFT_YELLOW);
  tft.fillRect(0, 4 * bandH, tft.width(), bandH, TFT_CYAN);
  tft.fillRect(0, 5 * bandH, tft.width(), bandH, TFT_MAGENTA);

  tft.setCursor(10, 10);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.print("ID: 0x");
  tft.println(id, HEX);
  tft.setCursor(10, 50);
  tft.setTextSize(2);
  tft.print(tft.width());
  tft.print("x");
  tft.println(tft.height());
  tft.setCursor(10, 80);
  tft.println("UNO R4 WiFi + ILI9486");

  Serial.println("Display test complete.");
}

void loop() {
  delay(5000);
  Serial.println("OK");
}
