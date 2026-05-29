#include <Arduino.h>

#include "LightController.h"

namespace {

LightController light;
uint32_t lastPrintMs = 0;

void printState() {
  Serial.print(F("autoMode="));
  Serial.print(light.isAutoMode() ? F("ON") : F("OFF"));
  Serial.print(F(", brightness="));
  Serial.print(light.getCurrentBrightness());
  Serial.print(F(", powerOn="));
  Serial.print(light.isPowerOn() ? F("ON") : F("OFF"));
  Serial.print(F(", hardPowerOff="));
  Serial.println(light.isHardPowerOffActive() ? F("ON") : F("OFF"));
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char command = static_cast<char>(Serial.read());
  if (command == 'a') {
    light.setAutoMode(true);
    light.startArduinoDimJob(100, 5000UL);
  } else if (command == 'h') {
    light.setAutoMode(false);
  } else if (command == '1') {
    light.applyManualOnOff(true);
  } else if (command == '0') {
    light.applyManualOnOff(false);
  } else if (command == 'b') {
    light.applyManualBrightness(25);
  } else if (command == 'n') {
    light.applyManualBrightness(75);
  } else if (command == 'd') {
    light.startHADimJob(100, 10000UL);
  } else if (command == 'x') {
    light.startHADimJob(0, 10000UL);
  } else if (command == 'p') {
    light.applyHardPowerOff(true);
  } else if (command == 'r') {
    light.applyHardPowerOff(false);
  }

  printState();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
  }

  light.begin();
  light.setAutoMode(false);

  Serial.println(F("LightControllerTest ready."));
  Serial.println(F("Commands: a=auto mode+auto ramp, h=HA mode, 1=on, 0=off, b=25%, n=75%, d=HA ramp up, x=HA ramp down, p=hard power off, r=release hard power off"));
  printState();
}

void loop() {
  const uint32_t nowMs = millis();
  light.update(nowMs);
  handleSerialCommand();

  if (nowMs - lastPrintMs >= 1000UL) {
    lastPrintMs = nowMs;
    printState();
  }
}
