#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "pins.h"

static void pulsePin(int pin, const char* name) {
  pinMode(pin, OUTPUT);
  Serial.printf("TEST %s GPIO%d LOW for 2s\n", name, pin);
  digitalWrite(pin, LOW);
  delay(2000);
  Serial.printf("TEST %s GPIO%d HIGH for 2s\n", name, pin);
  digitalWrite(pin, HIGH);
  delay(2000);
}

static void electricalPinTest() {
  Serial.println();
  Serial.println("=== ELECTRICAL PIN TEST ===");
  Serial.println("Measure at HW-125 header with meter/scope.");
  pulsePin(PIN_SD_CS, "CS");
  pulsePin(PIN_SD_MOSI, "MOSI");
  pulsePin(PIN_SD_SCK, "SCK");

  pinMode(PIN_SD_MISO, INPUT_PULLUP);
  Serial.printf("MISO GPIO%d input level=%d (expect HIGH if card/module pulls it high or pullup wins)\n",
                PIN_SD_MISO, digitalRead(PIN_SD_MISO));
  Serial.println("=== END ELECTRICAL PIN TEST ===");
}

static void sdMountTest() {
  Serial.println();
  Serial.println("=== SD MOUNT TEST ===");
  Serial.printf("Pins: CS=%d MOSI=%d SCK=%d MISO=%d\n",
                PIN_SD_CS, PIN_SD_MOSI, PIN_SD_SCK, PIN_SD_MISO);

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_SD_MISO, INPUT);

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

    File file = SD.open("/sd_diag.txt", FILE_APPEND);
    if (!file) {
      Serial.println("OPEN /sd_diag.txt FAILED");
      continue;
    }

    file.printf("boot_ms=%lu spi_hz=%lu test=PASS\n",
                (unsigned long)millis(),
                (unsigned long)hz);
    file.flush();
    file.close();
    Serial.println("WRITE PASS");

    file = SD.open("/sd_diag.txt", FILE_READ);
    if (!file) {
      Serial.println("READBACK OPEN FAILED");
      continue;
    }

    Serial.println("READBACK:");
    while (file.available()) Serial.write(file.read());
    file.close();
    Serial.println("=== END READBACK ===");
  }

  Serial.println(mounted ? "RESULT: SD PASS." : "RESULT: SD FAILED at all tested SPI speeds.");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== NorthStar SD bare-bones diagnostic ===");

  electricalPinTest();
  sdMountTest();
}

void loop() {
  delay(1000);
}
