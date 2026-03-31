#include <Arduino.h>

#include "Config.h"
#include "MoistureSensor.h"

namespace {

MoistureSensor moisture(PIN_SOIL_SENSOR);
uint32_t lastPrintMs = 0;

void printState() {
  Serial.print(F("raw="));
  Serial.print(moisture.getLastRaw());
  Serial.print(F(", percent="));
  Serial.print(moisture.getLastPercent());
  Serial.print(F(", air="));
  Serial.print(moisture.getSoilAir());
  Serial.print(F(", water="));
  Serial.print(moisture.getSoilWater());
  Serial.print(F(", depthMm="));
  Serial.println(moisture.getSoilDepthMm());
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char command = static_cast<char>(Serial.read());
  if (command == 's') {
    moisture.sampleNow();
  } else if (command == 'd') {
    moisture.setCalibration(DEFAULT_SOIL_AIR, DEFAULT_SOIL_WATER, DEFAULT_SOIL_DEPTH_MM);
  }

  printState();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
  }

  moisture.begin(DEFAULT_SOIL_AIR, DEFAULT_SOIL_WATER, DEFAULT_SOIL_DEPTH_MM);

  Serial.println(F("MoistureSensorTest ready."));
  Serial.println(F("Commands: s=sample now, d=reset calibration defaults"));
  printState();
}

void loop() {
  const uint32_t nowMs = millis();
  moisture.update(nowMs);
  handleSerialCommand();

  if (nowMs - lastPrintMs >= 2000UL) {
    lastPrintMs = nowMs;
    printState();
  }
}
