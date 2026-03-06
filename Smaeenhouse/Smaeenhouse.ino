#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "PersistentConfig.h"
#include "SHTa.h"
#include "FanController.h"
#include "LightController.h"
#include "MoistureSensor.h"
#include "ClockService.h"
#include "NetworkManager.h"
#include "HAInterface.h"

SHTa sht;
FanController fan;
LightController light;
MoistureSensor soil;
ClockService clockSvc;
NetworkManager net;
HAInterface ha;

ConfigStore store;
PersistentConfig cfg;

void shtISR() {
  fan.alertTriggered();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!store.load(cfg)) {
    cfg.tempHighSet = 30;
    cfg.tempHighClr = 28;
    cfg.humHighSet = 90;
    cfg.humHighClr = 70;
    cfg.lightOnTime = 480;
    cfg.lightOffTime = 1200;
    cfg.lightDimMinutes = 30;
    cfg.lightAutoMode = true;
    cfg.fanAutoMode = true;
    cfg.soilAir = 3000;
    cfg.soilWater = 1500;
    cfg.soilDepthMm = 50;
    store.save(cfg);
  }

  sht.begin();
  fan.begin();
  light.begin();
  soil.begin(cfg.soilAir, cfg.soilWater);
  clockSvc.begin();
  net.begin();
  ha.begin();

  pinMode(PIN_SHT_ALERT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_SHT_ALERT), shtISR, RISING);
}

void loop() {
  uint32_t now = millis();
  net.update();
  fan.update(now);
  light.update(now);
  ha.update();
}
