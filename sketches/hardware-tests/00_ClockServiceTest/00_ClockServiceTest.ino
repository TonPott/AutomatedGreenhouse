#include <Arduino.h>
#include <Wire.h>

#include "ClockService.h"

namespace {

ClockService clockService;
uint32_t lastPrintMs = 0;

void printTime() {
  const DateTime current = clockService.now();
  Serial.print(F("timeValid="));
  Serial.print(clockService.isTimeValid() ? F("YES") : F("NO"));
  Serial.print(F(", time="));
  Serial.print(current.year());
  Serial.print(F("-"));
  Serial.print(current.month());
  Serial.print(F("-"));
  Serial.print(current.day());
  Serial.print(F(" "));
  Serial.print(current.hour());
  Serial.print(F(":"));
  Serial.print(current.minute());
  Serial.print(F(":"));
  Serial.println(current.second());
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char command = static_cast<char>(Serial.read());
  if (command == 's') {
    const bool ok = clockService.syncFromNTP();
    Serial.println(ok ? F("Manual NTP sync succeeded.") : F("Manual NTP sync failed."));
    printTime();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
  }

  Wire.begin();

  clockService.begin();
  clockService.syncFromNTP();

  Serial.println(F("ClockServiceTest ready."));
  Serial.println(F("Command: s=manual NTP sync"));
  printTime();
}

void loop() {
  const uint32_t nowMs = millis();
  clockService.update(nowMs);
  handleSerialCommand();

  if (nowMs - lastPrintMs >= 5000UL) {
    lastPrintMs = nowMs;
    printTime();
  }
}
