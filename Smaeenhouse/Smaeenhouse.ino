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

volatile bool shtAlertPending = false;
volatile bool rtcAlarmPending = false;

PersistentConfigManager configManager;
PersistentConfigData cfg{};

SHTa sht;
FanController fan;
ClockService clockService;
LightController light(configManager, cfg, clockService);
MoistureSensor moisture(PIN_SOIL_SENSOR);
NetworkManager networkManager;
HAInterface ha(fan, light, moisture, clockService, sht, configManager, cfg, networkManager);

bool fallbackWasActive = false;
bool lastKnownAutoMode = DEFAULT_LIGHT_AUTO_MODE;
bool lastKnownWifiConnected = false;
uint32_t lastShtStatusPollMs = 0;
bool hasFallbackResumeSnapshot = false;
LightResumeState fallbackResumeSnapshot{};

bool minuteIsInsideOnWindow(uint16_t minuteNow, uint16_t onMinute, uint16_t offMinute) {
  if (onMinute == offMinute) {
    return false;
  }

  if (onMinute < offMinute) {
    return minuteNow >= onMinute && minuteNow < offMinute;
  }

  return minuteNow >= onMinute || minuteNow < offMinute;
}

uint8_t currentAutoWindowTargetPercent() {
  if (!clockService.isTimeValid()) {
    return 0;
  }

  const uint16_t nowMinute = clockService.minutesSinceMidnight();
  const bool shouldBeOn =
      minuteIsInsideOnWindow(nowMinute, cfg.lightOnTimeMinutes, cfg.lightOffTimeMinutes);
  return shouldBeOn ? 100 : 0;
}

void alignLightWithCurrentAutoWindow() {
  if (!light.isAutoMode() || !clockService.isTimeValid()) {
    return;
  }

  light.startArduinoDimJob(currentAutoWindowTargetPercent(), 0);
}

void evaluateShtAlertsAndFanDemand() {
  if (sht.hasSensorReset()) {
    sht.applyThresholdConfig(cfg);
    sht.clearStatus();
  }

  const bool autoDemand = sht.hasHumAlert() || sht.hasTempAlert();
  fan.setAutoDemand(autoDemand);
}

void onShtAlertInterrupt() {
  shtAlertPending = true;
}

void onRtcAlarmInterrupt() {
  rtcAlarmPending = true;
}

}  // namespace

void setup() {
  Serial.begin(115200);

  Wire.begin();

  configManager.begin(Wire);
  cfg = configManager.current();
  Serial.print(F("PersistentConfig init: storageAvailable="));
  Serial.print(configManager.isStorageAvailable() ? F("YES") : F("NO"));
  Serial.print(F(", lightOnMinutes="));
  Serial.print(cfg.lightOnTimeMinutes);
  Serial.print(F(", lightOffMinutes="));
  Serial.print(cfg.lightOffTimeMinutes);
  Serial.print(F(", dimMinutes="));
  Serial.print(cfg.defaultLightDimMinutes);
  Serial.print(F(", fanAutoMode="));
  Serial.print(cfg.fanAutoMode ? F("ON") : F("OFF"));
  Serial.print(F(", lightAutoMode="));
  Serial.println(cfg.lightAutoMode ? F("ON") : F("OFF"));

  clockService.begin();
  const bool alarmsConfigured =
      clockService.configureScheduleAlarms(cfg.lightOnTimeMinutes, cfg.lightOffTimeMinutes);
  Serial.print(F("ClockService alarm config: success="));
  Serial.println(alarmsConfigured ? F("YES") : F("NO"));

  sht.begin();
  sht.startPeriodicMeasurement(REPEATABILITY_MEDIUM, MPS_ONE_PER_SECOND);
  sht.applyThresholdConfig(cfg);

  float initialTemperature = NAN;
  float initialHumidity = NAN;
  if (sht.readMeasurement(initialTemperature, initialHumidity)) {
    Serial.print(F("SHT init: temperature="));
    Serial.print(initialTemperature, 2);
    Serial.print(F(" C, humidity="));
    Serial.println(initialHumidity, 1);
  } else {
    Serial.println(F("SHT init: no initial measurement available."));
  }

  fan.begin();
  fan.setAutoMode(cfg.fanAutoMode);
  fan.setManualState(false);

  light.begin(Wire);
  light.setAutoMode(cfg.lightAutoMode);
  light.restoreOnBoot(cfg.lightAutoMode && clockService.isTimeValid(), currentAutoWindowTargetPercent());

  moisture.begin(cfg.soilAir, cfg.soilWater, cfg.soilDepthMm);

  networkManager.begin();
  lastKnownWifiConnected = networkManager.isWifiConnected();
  ha.begin();

  if (clockService.syncFromNTP() && light.isAutoMode()) {
    alignLightWithCurrentAutoWindow();
  }

  pinMode(PIN_SHT_ALERT, INPUT_PULLUP);
  const int shtInterruptId = digitalPinToInterrupt(PIN_SHT_ALERT);
  if (shtInterruptId >= 0) {
    attachInterrupt(shtInterruptId, onShtAlertInterrupt, FALLING);
  } else {
    Serial.println(F("SHT alert pin does not support interrupts."));
  }

  pinMode(PIN_RTC_ALARM, INPUT_PULLUP);
  const int rtcInterruptId = digitalPinToInterrupt(PIN_RTC_ALARM);
  if (rtcInterruptId >= 0) {
    attachInterrupt(rtcInterruptId, onRtcAlarmInterrupt, FALLING);
  } else {
    Serial.println(F("RTC alarm pin does not support interrupts."));
  }

  shtAlertPending = true;
  lastKnownAutoMode = light.isAutoMode();
}

void loop() {
  const uint32_t nowMs = millis();

  networkManager.update(nowMs);
  const bool wifiConnectedNow = networkManager.isWifiConnected();
  if (wifiConnectedNow != lastKnownWifiConnected) {
    if (wifiConnectedNow) {
      Serial.println(F("Main loop detected WiFi connection."));
      if (!clockService.isTimeValid()) {
        clockService.syncFromNTP();
      }
    } else {
      Serial.println(F("Main loop detected WiFi disconnect."));
    }

    lastKnownWifiConnected = wifiConnectedNow;
  }

  clockService.update(nowMs);

  bool shouldDecodeShtStatus = false;
  if (shtAlertPending) {
    noInterrupts();
    shtAlertPending = false;
    interrupts();
    shouldDecodeShtStatus = true;
  }

  if (nowMs - lastShtStatusPollMs >= SHT_STATUS_POLL_INTERVAL_MS) {
    lastShtStatusPollMs = nowMs;
    shouldDecodeShtStatus = true;
  }

  if (shouldDecodeShtStatus && sht.decodeStatusRegister()) {
    evaluateShtAlertsAndFanDemand();
  }

  const bool autoModeNow = light.isAutoMode();
  if (autoModeNow != lastKnownAutoMode) {
    if (autoModeNow) {
      alignLightWithCurrentAutoWindow();
    }

    lastKnownAutoMode = autoModeNow;
  }

  if (rtcAlarmPending || clockService.hasPendingAlarmEvent()) {
    if (rtcAlarmPending) {
      noInterrupts();
      rtcAlarmPending = false;
      interrupts();
    }

    const ClockAlarmEvent alarmEvent = clockService.serviceAlarmFlags();
    if (light.isAutoMode()) {
      if (alarmEvent == ClockAlarmEvent::LightOnAlarm) {
        light.startArduinoDimJob(100, static_cast<uint32_t>(cfg.defaultLightDimMinutes) * 60000UL);
      } else if (alarmEvent == ClockAlarmEvent::LightOffAlarm) {
        light.startArduinoDimJob(0, static_cast<uint32_t>(cfg.defaultLightDimMinutes) * 60000UL);
      }
    }
  }

  const bool fallbackActive = networkManager.isFallbackActive();
  if (fallbackActive && !fallbackWasActive) {
    fallbackResumeSnapshot = cfg.lightResumeState;
    hasFallbackResumeSnapshot = true;

    if (cfg.lightFallbackMode == LIGHT_FALLBACK_USE_AUTO_MODE) {
      light.setAutoMode(true);
      alignLightWithCurrentAutoWindow();
    } else {
      light.applyHardPowerOff(true);
    }
  } else if (!fallbackActive && fallbackWasActive) {
    light.applyHardPowerOff(false);
    light.setAutoMode(cfg.lightAutoMode);

    if (!cfg.lightAutoMode && hasFallbackResumeSnapshot) {
      cfg.lightResumeState = fallbackResumeSnapshot;
      configManager.saveIfChanged(cfg);
      light.restoreOnBoot(false, 0);
    } else if (light.isAutoMode()) {
      alignLightWithCurrentAutoWindow();
    }

    hasFallbackResumeSnapshot = false;
  }
  fallbackWasActive = fallbackActive;

  fan.update(nowMs);
  light.update(nowMs);
  moisture.update(nowMs);
  ha.update(nowMs);
}
