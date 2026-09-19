#include <Arduino.h>
#include "pins.h"

static void setAll(bool high) {
  digitalWrite(PIN_SD_CS, high ? HIGH : LOW);
  digitalWrite(PIN_SD_MOSI, high ? HIGH : LOW);
  digitalWrite(PIN_SD_SCK, high ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_SD_CS, OUTPUT);
  pinMode(PIN_SD_MOSI, OUTPUT);
  pinMode(PIN_SD_SCK, OUTPUT);

  Serial.println();
  Serial.println("=== NorthStar SD electrical toggle diagnostic ===");
  Serial.printf("CS=GPIO%d MOSI=GPIO%d SCK=GPIO%d\n",
                PIN_SD_CS, PIN_SD_MOSI, PIN_SD_SCK);
  Serial.println("All three outputs will toggle together:");
  Serial.println("LOW for 2 seconds, HIGH for 2 seconds, repeating forever.");
  Serial.println("Stop by resetting/powering off or uploading new firmware.");
}

void loop() {
  Serial.println("CS/MOSI/SCK -> LOW");
  setAll(false);
  delay(2000);

  Serial.println("CS/MOSI/SCK -> HIGH");
  setAll(true);
  delay(2000);
}
