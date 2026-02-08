#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>

// RS=8, E=9, D4=4, D5=5, D6=6, D7=7
hd44780_pinIO lcd(8, 9, 4, 5, 6, 7);

bool lcdReady = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
}

void loop() {
  if (!lcdReady) {
    Serial.println("--- LCD DIAGNOSTIC ---");
    Serial.println("Pins: RS=8, E=9, D4=4, D5=5, D6=6, D7=7");
    Serial.println("Please confirm wiring matches these pins.");
    Serial.println();

    Serial.println("Calling lcd.begin(16,2)...");
    int status = lcd.begin(16, 2);
    Serial.print("Result: ");
    Serial.println(status);

    if (status) {
      Serial.print("ERROR: lcd.begin failed with code ");
      Serial.println(status);
    } else {
      Serial.println("lcd.begin OK");
    }

    Serial.println("Writing text...");
    lcd.clear();
    delay(50);
    lcd.setCursor(0, 0);
    lcd.print("Hello");
    delay(50);
    lcd.setCursor(0, 1);
    lcd.print("World");

    Serial.println("Text written. Check LCD now.");
    Serial.println("If still blocks, the data lines may be miswired.");
    lcdReady = true;
  }

  delay(5000);
  Serial.println("alive");
}
