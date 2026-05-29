#pragma once

#include <Arduino.h>
#include <ArduinoHA.h>
#include <WiFiNINA.h>

#include "ClockService.h"
#include "FanController.h"
#include "LightController.h"
#include "MoistureSensor.h"
#include "NetworkManager.h"
#include "PersistentConfig.h"
#include "SHTa.h"

class HAInterface {
public:
  HAInterface(FanController& fanController,
              LightController& lightController,
              MoistureSensor& moistureSensor,
              ClockService& clockService,
              SHTa& sht,
              PersistentConfigManager& configManager,
              PersistentConfigData& configData,
              NetworkManager& networkManager);

  void begin();
  void update(uint32_t nowMs);
  void onMqttConnected();

  void publishAllStates();
  void publishSensorValues(bool force = false);
  void publishConfigValues();

private:
  static HAInterface* instance_;

  static void onFanSwitchCommandStatic(bool state, HASwitch* sender);
  static void onFanAutoModeCommandStatic(bool state, HASwitch* sender);
  static void onLightAutoModeCommandStatic(bool state, HASwitch* sender);
  static void onLightHardPowerOffCommandStatic(bool state, HASwitch* sender);
  static void onFallbackSwitchCommandStatic(bool state, HASwitch* sender);

  static void onLightStateCommandStatic(bool state, HALight* sender);
  static void onLightBrightnessCommandStatic(uint8_t brightness, HALight* sender);

  static void onNumberCommandStatic(HANumeric number, HANumber* sender);

  static void onSyncTimeButtonCommandStatic(HAButton* sender);
  static void onReadSoilRawButtonCommandStatic(HAButton* sender);
  static void onStartHaDimButtonCommandStatic(HAButton* sender);

  void handleFanSwitchCommand(bool state);
  void handleFanAutoModeCommand(bool state);
  void handleLightAutoModeCommand(bool state);
  void handleLightHardPowerOffCommand(bool state);
  void handleFallbackSwitchCommand(bool state);

  void handleLightStateCommand(bool state);
  void handleLightBrightnessCommand(uint8_t brightness);

  void handleNumberCommand(HANumeric number, HANumber* sender);

  void handleSyncTimeButtonCommand();
  void handleReadSoilRawButtonCommand();
  void handleStartHaDimButtonCommand();

  void publishSwitchAndLightStates();

  FanController& fanController_;
  LightController& lightController_;
  MoistureSensor& moistureSensor_;
  ClockService& clockService_;
  SHTa& sht_;
  PersistentConfigManager& configManager_;
  PersistentConfigData& configData_;
  NetworkManager& networkManager_;

  WiFiClient networkClient_;
  HADevice device_;
  HAMqtt mqtt_;

  HASensorNumber temperatureSensor_;
  HASensorNumber humiditySensor_;
  HASensorNumber soilPercentSensor_;
  HASensorNumber soilRawSensor_;
  HASensorNumber fanRpmSensor_;

  HASwitch fanSwitch_;
  HASwitch fanAutoModeSwitch_;
  HASwitch lightAutoModeSwitch_;
  HASwitch lightHardPowerOffSwitch_;
  HASwitch lightFallbackUseAutoModeSwitch_;

  HALight growLight_;

  HANumber tempHighSetNumber_;
  HANumber tempHighClearNumber_;
  HANumber tempLowSetNumber_;
  HANumber tempLowClearNumber_;
  HANumber humHighSetNumber_;
  HANumber humHighClearNumber_;
  HANumber humLowSetNumber_;
  HANumber humLowClearNumber_;

  HANumber lightOnTimeNumber_;
  HANumber lightOffTimeNumber_;
  HANumber defaultLightDimMinutesNumber_;

  HANumber soilAirNumber_;
  HANumber soilWaterNumber_;
  HANumber soilDepthNumber_;

  HANumber haDimTargetPercentNumber_;
  HANumber haDimDurationMinutesNumber_;

  HAButton syncTimeButton_;
  HAButton readSoilRawButton_;
  HAButton startHaDimButton_;

  bool wasMqttConnected_ = false;

  uint32_t lastTempHumPublishMs_ = 0;
  uint32_t lastSoilPublishMs_ = 0;
  uint32_t lastFanPublishMs_ = 0;

  uint8_t pendingHaDimTargetPercent_ = 100;
  uint16_t pendingHaDimDurationMinutes_ = 30;
};
