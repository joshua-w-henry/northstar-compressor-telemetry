#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "pins.h"

static void printPinLevels() {
  Serial.printf("Pins: CS=%d MOSI=%d SCK=%d MISO=%d\n",
                PIN_SD_CS, PIN_SD_MOSI, PIN_SD_SCK, PIN_SD_MISO);
  Serial.printf("Idle levels: CS=%d MOSI=%d SCK=%d MISO=%d\n",
                digitalRead(PIN_SD_CS),
                digitalRead(PIN_SD_MOSI),
                digitalRead(PIN_SD_SCK),
                digitalRead(PIN_SD_MISO));
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== NorthStar SD bare-bones diagnostic ===");

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_SD_MISO, INPUT);

  printPinLevels();

  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  delay(100);

  const uint32_t speeds[] = {400000, 1000000, 4000000};

  bool mounted = false;
  for (uint8_t i = 0; i < 3 && !mounted; ++i) {
    const uint32_t hz = speeds[i];
    Serial.printf("\nTrying SD.begin at %lu Hz...\n", (unsigned long)hz);

    mounted = SD.begin(PIN_SD_CS, SPI, hz);
    if (!mounted) {
      Serial.println("SD.begin FAILED");
      SD.end();
      digitalWrite(PIN_SD_CS, HIGH);
      delay(500);
      continue;
    }

    const uint8_t type = SD.cardType();
    Serial.printf("SD.begin OK, cardType=%u\n", type);

    if (type == CARD_NONE) {
      Serial.println("No card detected");
      SD.end();
      mounted = false;
      delay(500);
      continue;
    }

    Serial.printf("Card size: %llu MB\n",
                  (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));

    File f = SD.open("/sd_diag.txt", FILE_APPEND);
    if (!f) {
      Serial.println("OPEN /sd_diag.txt FAILED");
      continue;
    }

    f.printf("boot_ms=%lu spi_hz=%lu test=PASS\n",
             (unsigned long)millis(),
             (unsigned long)hz);
    f.flush();
    f.close();
    Serial.println("WRITE PASS");

    f = SD.open("/sd_diag.txt", FILE_READ);
    if (!f) {
      Serial.println("READBACK OPEN FAILED");
      continue;
    }

    Serial.println("READBACK:");
    while (f.available()) Serial.write(f.read());
    f.close();
    Serial.println("=== END READBACK ===");
  }

  if (!mounted) {
    Serial.println();
    Serial.println("RESULT: SD FAILED at all tested SPI speeds.");
  } else {
    Serial.println();
    Serial.println("RESULT: SD PASS.");
  }
}

void loop() {
  delay(1000);
}
