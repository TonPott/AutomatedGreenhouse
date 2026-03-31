#include <Arduino.h>

#include "Config.h"
#include "FanController.h"

namespace {

FanController fan;
uint32_t lastPrintMs = 0;

void printState() {
  Serial.print(F("autoMode="));
  Serial.print(fan.isAutoMode() ? F("ON") : F("OFF"));
  Serial.print(F(", manualState="));
  Serial.print(fan.getManualState() ? F("ON") : F("OFF"));
  Serial.print(F(", effectiveState="));
  Serial.print(fan.isOn() ? F("ON") : F("OFF"));
  Serial.print(F(", rpm="));
  Serial.println(fan.getRPM());
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char command = static_cast<char>(Serial.read());
  if (command == 'a') {
    fan.setAutoMode(true);
  } else if (command == 'm') {
    fan.setAutoMode(false);
  } else if (command == '1') {
    fan.setManualState(true);
  } else if (command == '0') {
    fan.setManualState(false);
  } else if (command == 'd') {
    fan.setAutoDemand(true);
  } else if (command == 'c') {
    fan.setAutoDemand(false);
  }

  printState();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
  }

  fan.begin();
  fan.setAutoMode(false);
  fan.setManualState(false);

  Serial.println(F("FanControllerTest ready."));
  Serial.println(F("Commands: a=auto mode, m=manual mode, 1=manual on, 0=manual off, d=auto demand on, c=auto demand off"));
  printState();
}

void loop() {
  const uint32_t nowMs = millis();
  fan.update(nowMs);
  handleSerialCommand();

  if (nowMs - lastPrintMs >= 1000UL) {
    lastPrintMs = nowMs;
    printState();
  }
}
