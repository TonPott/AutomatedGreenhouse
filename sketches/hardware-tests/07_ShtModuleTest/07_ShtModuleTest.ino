#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "PersistentConfig.h"
#include "SHTa.h"

namespace {

volatile bool shtAlertPending = false;

PersistentConfigManager configManager;
PersistentConfigData cfg{};
SHTa sht;

uint32_t lastPrintMs = 0;

void onShtAlertInterrupt() {
  shtAlertPending = true;
}

void printStatusAndMeasurement() {
  float temperature = NAN;
  float humidity = NAN;
  sht.readMeasurement(temperature, humidity);
  sht.decodeStatusRegister();

  Serial.print(F("Temp: "));
  Serial.print(temperature, 2);
  Serial.print(F(" C, Hum: "));
  Serial.print(humidity, 1);
  Serial.print(F(" %, tempAlert="));
  Serial.print(sht.hasTempAlert() ? F("1") : F("0"));
  Serial.print(F(", humAlert="));
  Serial.print(sht.hasHumAlert() ? F("1") : F("0"));
  Serial.print(F(", sensorReset="));
  Serial.println(sht.hasSensorReset() ? F("1") : F("0"));
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

  pinMode(PIN_SHT_ALERT, INPUT_PULLUP);
  const int interruptId = digitalPinToInterrupt(PIN_SHT_ALERT);
  if (interruptId >= 0) {
    attachInterrupt(interruptId, onShtAlertInterrupt, FALLING);
  }

  Serial.println(F("ShtModuleTest ready."));
}

void loop() {
  const uint32_t nowMs = millis();

  if (shtAlertPending) {
    noInterrupts();
    shtAlertPending = false;
    interrupts();
    Serial.println(F("SHT alert interrupt received."));
    printStatusAndMeasurement();
  }

  if (nowMs - lastPrintMs >= 2000UL) {
    lastPrintMs = nowMs;
    printStatusAndMeasurement();
  }
}
