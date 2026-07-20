#include <Arduino.h>
#include <Wire.h>

#include "ClockService.h"
#include "Config.h"
#include "FanController.h"
#include "HAInterface.h"
#include "LightController.h"
#include "MoistureSensor.h"
#include "NetworkManager.h"
#include "PersistentConfig.h"
#include "SHTa.h"

namespace {

PersistentConfigManager configManager;
PersistentConfigData cfg{};

SHTa sht;
FanController fan;
LightController light;
MoistureSensor moisture(PIN_SOIL_SENSOR);
ClockService clockService;
NetworkManager networkManager;
HAInterface ha(fan, light, moisture, clockService, sht, configManager, cfg, networkManager);

uint32_t lastStatusMs = 0;

void printStatus() {
  Serial.print(F("wifi="));
  Serial.print(networkManager.isWifiConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", mqtt="));
  Serial.print(networkManager.isMqttConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", lightAuto="));
  Serial.print(light.isAutoMode() ? F("ON") : F("OFF"));
  Serial.print(F(", lightBrightness="));
  Serial.print(light.getCurrentBrightness());
  Serial.print(F(", fanAuto="));
  Serial.print(fan.isAutoMode() ? F("ON") : F("OFF"));
  Serial.print(F(", fanOn="));
  Serial.print(fan.isOn() ? F("ON") : F("OFF"));
  Serial.print(F(", soil="));
  Serial.print(moisture.getLastPercent());
  Serial.println(F("%"));
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char command = static_cast<char>(Serial.read());
  if (command == 'p') {
    ha.publishAllStates();
    Serial.println(F("Forced HA state publish."));
  } else if (command == 's') {
    moisture.sampleNow();
    ha.publishSensorValues(true);
    Serial.println(F("Forced sensor publish."));
  } else if (command == 't') {
    const bool ok = clockService.syncFromNTP();
    Serial.println(ok ? F("Manual NTP sync succeeded.") : F("Manual NTP sync failed."));
  }

  printStatus();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
  }

  Wire.begin();

  configManager.begin();
  configManager.load(cfg);

  sht.begin();
  sht.startPeriodicMeasurement(REPEATABILITY_MEDIUM, MPS_ONE_PER_SECOND);
  sht.applyThresholdConfig(cfg);

  fan.begin();
  fan.setAutoMode(cfg.fanAutoMode);

  light.begin();
  light.setAutoMode(cfg.lightAutoMode);

  moisture.begin(cfg.soilAir, cfg.soilWater, cfg.soilDepthMm);

  clockService.begin();
  networkManager.begin();
  ha.begin();

  Serial.println(F("HAInterfaceTest ready."));
  Serial.println(F("Commands: p=publish all states, s=publish sensors, t=manual NTP sync"));
  printStatus();
}

void loop() {
  const uint32_t nowMs = millis();

  networkManager.update(nowMs);
  clockService.update(nowMs);
  fan.update(nowMs);
  light.update(nowMs);
  moisture.update(nowMs);
  ha.update(nowMs);
  handleSerialCommand();

  if (nowMs - lastStatusMs >= 5000UL) {
    lastStatusMs = nowMs;
    printStatus();
  }
}
