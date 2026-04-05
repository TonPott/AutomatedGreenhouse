#include "HAInterface.h"

#include <math.h>

#include "Config.h"

#if defined(__has_include)
#if __has_include("Credentials.h")
#include "Credentials.h"
#else
#error "Missing Credentials.h in sketch folder. Copy Credentials.example.h to Credentials.h and fill values."
#endif
#else
#include "Credentials.h"
#endif

#ifndef DEVICE_NAME
#error "Credentials.h must define DEVICE_NAME."
#endif
#ifndef DEVICE_ID
#error "Credentials.h must define DEVICE_ID."
#endif
#ifndef MQTT_HOST
#error "Credentials.h must define MQTT_HOST."
#endif
#ifndef MQTT_USERNAME
#error "Credentials.h must define MQTT_USERNAME."
#endif
#ifndef MQTT_PASSWORD
#error "Credentials.h must define MQTT_PASSWORD."
#endif

HAInterface* HAInterface::instance_ = nullptr;

namespace {

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

uint16_t clampUInt16(long value, uint16_t minValue, uint16_t maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return static_cast<uint16_t>(value);
}

uint8_t clampPercent(long value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return static_cast<uint8_t>(value);
}

}  // namespace

HAInterface::HAInterface(FanController& fanController,
                         LightController& lightController,
                         MoistureSensor& moistureSensor,
                         ClockService& clockService,
                         SHTa& sht,
                         PersistentConfigManager& configManager,
                         PersistentConfigData& configData,
                         NetworkManager& networkManager)
    : fanController_(fanController),
      lightController_(lightController),
      moistureSensor_(moistureSensor),
      clockService_(clockService),
      sht_(sht),
      configManager_(configManager),
      configData_(configData),
      networkManager_(networkManager),
      device_(DEVICE_ID),
      mqtt_(networkClient_, device_, 40),
      temperatureSensor_("temperature"),
      humiditySensor_("humidity"),
      soilPercentSensor_("soil_moisture_percent"),
      soilRawSensor_("soil_moisture_raw"),
      fanRpmSensor_("fan_rpm"),
      fanSwitch_("fan_switch"),
      fanAutoModeSwitch_("fan_auto_mode"),
      lightAutoModeSwitch_("light_auto_mode"),
      lightHardPowerOffSwitch_("light_hard_power_off"),
      lightFallbackUseAutoModeSwitch_("light_fallback_use_auto_mode"),
      growLight_("grow_light", HALight::BrightnessFeature),
      tempHighSetNumber_("temp_high_set", HANumber::PrecisionP1),
      tempHighClearNumber_("temp_high_clear", HANumber::PrecisionP1),
      tempLowSetNumber_("temp_low_set", HANumber::PrecisionP1),
      tempLowClearNumber_("temp_low_clear", HANumber::PrecisionP1),
      humHighSetNumber_("hum_high_set", HANumber::PrecisionP1),
      humHighClearNumber_("hum_high_clear", HANumber::PrecisionP1),
      humLowSetNumber_("hum_low_set", HANumber::PrecisionP1),
      humLowClearNumber_("hum_low_clear", HANumber::PrecisionP1),
      lightOnTimeNumber_("light_on_time_minutes"),
      lightOffTimeNumber_("light_off_time_minutes"),
      defaultLightDimMinutesNumber_("light_dim_minutes"),
      soilAirNumber_("soil_air"),
      soilWaterNumber_("soil_water"),
      soilDepthNumber_("soil_depth_mm"),
      haDimTargetPercentNumber_("ha_dim_target_percent"),
      haDimDurationMinutesNumber_("ha_dim_duration_minutes"),
      syncTimeButton_("sync_time"),
      readSoilRawButton_("read_soil_raw_value"),
      startHaDimButton_("start_ha_dim") {
}

void HAInterface::begin() {
  instance_ = this;

  device_.setName(DEVICE_NAME);

  temperatureSensor_.setName("Temperature");
  temperatureSensor_.setUnitOfMeasurement("C");

  humiditySensor_.setName("Humidity");
  humiditySensor_.setUnitOfMeasurement("%");

  soilPercentSensor_.setName("Soil Moisture Percent");
  soilPercentSensor_.setUnitOfMeasurement("%");

  soilRawSensor_.setName("Soil Moisture Raw");

  fanRpmSensor_.setName("Fan RPM");
  fanRpmSensor_.setUnitOfMeasurement("rpm");

  fanSwitch_.setName("Fan Switch");
  fanAutoModeSwitch_.setName("Fan Auto Mode");
  lightAutoModeSwitch_.setName("Light Auto Mode");
  lightHardPowerOffSwitch_.setName("Light Hard Power Off");
  lightFallbackUseAutoModeSwitch_.setName("Light Fallback Use Auto Mode");

  growLight_.setName("Grow Light");
  growLight_.setBrightnessScale(100);

  tempHighSetNumber_.setName("Temp High Set");
  tempHighSetNumber_.setUnitOfMeasurement("C");
  tempHighSetNumber_.setMin(-40.0f);
  tempHighSetNumber_.setMax(125.0f);
  tempHighSetNumber_.setStep(0.1f);

  tempHighClearNumber_.setName("Temp High Clear");
  tempHighClearNumber_.setUnitOfMeasurement("C");
  tempHighClearNumber_.setMin(-40.0f);
  tempHighClearNumber_.setMax(125.0f);
  tempHighClearNumber_.setStep(0.1f);

  tempLowSetNumber_.setName("Temp Low Set");
  tempLowSetNumber_.setUnitOfMeasurement("C");
  tempLowSetNumber_.setMin(-40.0f);
  tempLowSetNumber_.setMax(125.0f);
  tempLowSetNumber_.setStep(0.1f);

  tempLowClearNumber_.setName("Temp Low Clear");
  tempLowClearNumber_.setUnitOfMeasurement("C");
  tempLowClearNumber_.setMin(-40.0f);
  tempLowClearNumber_.setMax(125.0f);
  tempLowClearNumber_.setStep(0.1f);

  humHighSetNumber_.setName("Hum High Set");
  humHighSetNumber_.setUnitOfMeasurement("%");
  humHighSetNumber_.setMin(0.0f);
  humHighSetNumber_.setMax(100.0f);
  humHighSetNumber_.setStep(0.1f);

  humHighClearNumber_.setName("Hum High Clear");
  humHighClearNumber_.setUnitOfMeasurement("%");
  humHighClearNumber_.setMin(0.0f);
  humHighClearNumber_.setMax(100.0f);
  humHighClearNumber_.setStep(0.1f);

  humLowSetNumber_.setName("Hum Low Set");
  humLowSetNumber_.setUnitOfMeasurement("%");
  humLowSetNumber_.setMin(0.0f);
  humLowSetNumber_.setMax(100.0f);
  humLowSetNumber_.setStep(0.1f);

  humLowClearNumber_.setName("Hum Low Clear");
  humLowClearNumber_.setUnitOfMeasurement("%");
  humLowClearNumber_.setMin(0.0f);
  humLowClearNumber_.setMax(100.0f);
  humLowClearNumber_.setStep(0.1f);

  lightOnTimeNumber_.setName("Light On Time Minutes");
  lightOnTimeNumber_.setUnitOfMeasurement("min");
  lightOnTimeNumber_.setMin(0.0f);
  lightOnTimeNumber_.setMax(1439.0f);
  lightOnTimeNumber_.setStep(1.0f);

  lightOffTimeNumber_.setName("Light Off Time Minutes");
  lightOffTimeNumber_.setUnitOfMeasurement("min");
  lightOffTimeNumber_.setMin(0.0f);
  lightOffTimeNumber_.setMax(1439.0f);
  lightOffTimeNumber_.setStep(1.0f);

  defaultLightDimMinutesNumber_.setName("Light Default Dim Minutes");
  defaultLightDimMinutesNumber_.setUnitOfMeasurement("min");
  defaultLightDimMinutesNumber_.setMin(0.0f);
  defaultLightDimMinutesNumber_.setMax(1440.0f);
  defaultLightDimMinutesNumber_.setStep(1.0f);

  soilAirNumber_.setName("Soil Air");
  soilAirNumber_.setMin(0.0f);
  soilAirNumber_.setMax(1000.0f);
  soilAirNumber_.setStep(1.0f);

  soilWaterNumber_.setName("Soil Water");
  soilWaterNumber_.setMin(0.0f);
  soilWaterNumber_.setMax(1000.0f);
  soilWaterNumber_.setStep(1.0f);

  soilDepthNumber_.setName("Soil Depth mm");
  soilDepthNumber_.setUnitOfMeasurement("mm");
  soilDepthNumber_.setMin(0.0f);
  soilDepthNumber_.setMax(120.0f);
  soilDepthNumber_.setStep(1.0f);

  haDimTargetPercentNumber_.setName("HA Dim Target Percent");
  haDimTargetPercentNumber_.setUnitOfMeasurement("%");
  haDimTargetPercentNumber_.setMin(0.0f);
  haDimTargetPercentNumber_.setMax(100.0f);
  haDimTargetPercentNumber_.setStep(1.0f);

  haDimDurationMinutesNumber_.setName("HA Dim Duration Minutes");
  haDimDurationMinutesNumber_.setUnitOfMeasurement("min");
  haDimDurationMinutesNumber_.setMin(0.0f);
  haDimDurationMinutesNumber_.setMax(1440.0f);
  haDimDurationMinutesNumber_.setStep(1.0f);

  syncTimeButton_.setName("Sync Time");
  readSoilRawButton_.setName("Read Soil Raw Value");
  startHaDimButton_.setName("Start HA Dim");

  fanSwitch_.onCommand(onFanSwitchCommandStatic);
  fanAutoModeSwitch_.onCommand(onFanAutoModeCommandStatic);
  lightAutoModeSwitch_.onCommand(onLightAutoModeCommandStatic);
  lightHardPowerOffSwitch_.onCommand(onLightHardPowerOffCommandStatic);
  lightFallbackUseAutoModeSwitch_.onCommand(onFallbackSwitchCommandStatic);

  growLight_.onStateCommand(onLightStateCommandStatic);
  growLight_.onBrightnessCommand(onLightBrightnessCommandStatic);

  tempHighSetNumber_.onCommand(onNumberCommandStatic);
  tempHighClearNumber_.onCommand(onNumberCommandStatic);
  tempLowSetNumber_.onCommand(onNumberCommandStatic);
  tempLowClearNumber_.onCommand(onNumberCommandStatic);
  humHighSetNumber_.onCommand(onNumberCommandStatic);
  humHighClearNumber_.onCommand(onNumberCommandStatic);
  humLowSetNumber_.onCommand(onNumberCommandStatic);
  humLowClearNumber_.onCommand(onNumberCommandStatic);
  lightOnTimeNumber_.onCommand(onNumberCommandStatic);
  lightOffTimeNumber_.onCommand(onNumberCommandStatic);
  defaultLightDimMinutesNumber_.onCommand(onNumberCommandStatic);
  soilAirNumber_.onCommand(onNumberCommandStatic);
  soilWaterNumber_.onCommand(onNumberCommandStatic);
  soilDepthNumber_.onCommand(onNumberCommandStatic);
  haDimTargetPercentNumber_.onCommand(onNumberCommandStatic);
  haDimDurationMinutesNumber_.onCommand(onNumberCommandStatic);

  syncTimeButton_.onCommand(onSyncTimeButtonCommandStatic);
  readSoilRawButton_.onCommand(onReadSoilRawButtonCommandStatic);
  startHaDimButton_.onCommand(onStartHaDimButtonCommandStatic);

  fanController_.setAutoMode(configData_.fanAutoMode);
  lightController_.setAutoMode(configData_.lightAutoMode);
  moistureSensor_.setCalibration(configData_.soilAir, configData_.soilWater, configData_.soilDepthMm);

  pendingHaDimTargetPercent_ = 100;
  pendingHaDimDurationMinutes_ = configData_.defaultLightDimMinutes;

  const bool mqttBeginOk = mqtt_.begin(MQTT_HOST, MQTT_USERNAME, MQTT_PASSWORD);
  Serial.print(F("HAMqtt begin: initialized="));
  Serial.print(mqttBeginOk ? F("YES") : F("NO"));
  Serial.print(F(", broker="));
  Serial.print(MQTT_HOST);
  Serial.print(F(", connectedNow="));
  Serial.println(mqtt_.isConnected() ? F("YES") : F("NO"));

  if (mqttBeginOk) {
    mqtt_.loop();
    Serial.print(F("HAMqtt first connect attempt: connected="));
    Serial.print(mqtt_.isConnected() ? F("YES") : F("NO"));
    Serial.print(F(", state="));
    Serial.println(static_cast<int>(mqtt_.getState()));
  } else {
    Serial.println(F("HAMqtt setup failed before any broker connection attempt."));
  }

  wasWifiConnected_ = networkManager_.isWifiConnected();
  wasMqttConnected_ = mqtt_.isConnected();
  networkManager_.setMqttConnected(wasMqttConnected_, millis());
}

void HAInterface::update(uint32_t nowMs) {
  const bool wifiConnected = networkManager_.isWifiConnected();
  if (wifiConnected != wasWifiConnected_) {
    if (wifiConnected) {
      Serial.println(F("WiFi connected, retrying MQTT setup."));
      mqtt_.disconnect();

      const bool mqttBeginOk = mqtt_.begin(MQTT_HOST, MQTT_USERNAME, MQTT_PASSWORD);
      Serial.print(F("HAMqtt re-begin after WiFi connect: initialized="));
      Serial.print(mqttBeginOk ? F("YES") : F("NO"));
      Serial.print(F(", connectedNow="));
      Serial.println(mqtt_.isConnected() ? F("YES") : F("NO"));
    } else {
      Serial.println(F("WiFi disconnected."));
    }

    wasWifiConnected_ = wifiConnected;
  }

  if (!networkManager_.isWifiConnected() && mqtt_.isConnected()) {
    mqtt_.disconnect();
  }

  mqtt_.loop();

  const bool mqttConnected = mqtt_.isConnected();
  networkManager_.setMqttConnected(mqttConnected, nowMs);

  if (mqttConnected && !wasMqttConnected_) {
    onMqttConnected();
  }

  wasMqttConnected_ = mqttConnected;

  if (!mqttConnected) {
    return;
  }

  publishSensorValues(false);
}

void HAInterface::onMqttConnected() {
  Serial.println(F("Connected to MQTT broker."));
  publishAllStates();
}

void HAInterface::publishAllStates() {
  publishConfigValues();
  publishSensorValues(true);
}

void HAInterface::publishSensorValues(bool force) {
  if (!wasMqttConnected_ && !mqtt_.isConnected()) {
    return;
  }

  const uint32_t nowMs = millis();

  if (force || (nowMs - lastTempHumPublishMs_ >= TEMP_HUM_PUBLISH_INTERVAL_MS)) {
    float temperature = NAN;
    float humidity = NAN;
    if (sht_.readMeasurement(temperature, humidity)) {
      temperatureSensor_.setValue(temperature);
      humiditySensor_.setValue(humidity);
    } else {
      if (!isnan(sht_.getLastTemperature())) {
        temperatureSensor_.setValue(sht_.getLastTemperature());
      }
      if (!isnan(sht_.getLastHumidity())) {
        humiditySensor_.setValue(sht_.getLastHumidity());
      }
    }

    lastTempHumPublishMs_ = nowMs;
  }

  if (force || (nowMs - lastSoilPublishMs_ >= SOIL_PUBLISH_INTERVAL_MS)) {
    soilPercentSensor_.setValue(static_cast<int32_t>(moistureSensor_.getLastPercent()));
    soilRawSensor_.setValue(static_cast<int32_t>(moistureSensor_.getLastRaw()));
    lastSoilPublishMs_ = nowMs;
  }

  if (force || (nowMs - lastFanPublishMs_ >= FAN_RPM_PUBLISH_INTERVAL_MS)) {
    fanRpmSensor_.setValue(static_cast<int32_t>(fanController_.getRPM()));
    lastFanPublishMs_ = nowMs;
  }
}

void HAInterface::publishConfigValues() {
  publishSwitchAndLightStates();

  tempHighSetNumber_.setState(configData_.tempHighSet);
  tempHighClearNumber_.setState(configData_.tempHighClear);
  tempLowSetNumber_.setState(configData_.tempLowSet);
  tempLowClearNumber_.setState(configData_.tempLowClear);

  humHighSetNumber_.setState(configData_.humHighSet);
  humHighClearNumber_.setState(configData_.humHighClear);
  humLowSetNumber_.setState(configData_.humLowSet);
  humLowClearNumber_.setState(configData_.humLowClear);

  lightOnTimeNumber_.setState(static_cast<int32_t>(configData_.lightOnTimeMinutes));
  lightOffTimeNumber_.setState(static_cast<int32_t>(configData_.lightOffTimeMinutes));
  defaultLightDimMinutesNumber_.setState(static_cast<int32_t>(configData_.defaultLightDimMinutes));

  soilAirNumber_.setState(static_cast<int32_t>(configData_.soilAir));
  soilWaterNumber_.setState(static_cast<int32_t>(configData_.soilWater));
  soilDepthNumber_.setState(static_cast<int32_t>(configData_.soilDepthMm));

  haDimTargetPercentNumber_.setState(static_cast<int32_t>(pendingHaDimTargetPercent_));
  haDimDurationMinutesNumber_.setState(static_cast<int32_t>(pendingHaDimDurationMinutes_));
}

void HAInterface::publishSwitchAndLightStates() {
  fanSwitch_.setState(fanController_.getManualState());
  fanAutoModeSwitch_.setState(fanController_.isAutoMode());

  lightAutoModeSwitch_.setState(lightController_.isAutoMode());
  lightHardPowerOffSwitch_.setState(lightController_.isHardPowerOffActive());

  lightFallbackUseAutoModeSwitch_.setState(configData_.lightFallbackMode == LIGHT_FALLBACK_USE_AUTO_MODE);

  growLight_.setState(lightController_.isPowerOn());
  growLight_.setBrightness(lightController_.getCurrentBrightness());
}

void HAInterface::onFanSwitchCommandStatic(bool state, HASwitch* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleFanSwitchCommand(state);
  }
}

void HAInterface::onFanAutoModeCommandStatic(bool state, HASwitch* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleFanAutoModeCommand(state);
  }
}

void HAInterface::onLightAutoModeCommandStatic(bool state, HASwitch* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleLightAutoModeCommand(state);
  }
}

void HAInterface::onLightHardPowerOffCommandStatic(bool state, HASwitch* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleLightHardPowerOffCommand(state);
  }
}

void HAInterface::onFallbackSwitchCommandStatic(bool state, HASwitch* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleFallbackSwitchCommand(state);
  }
}

void HAInterface::onLightStateCommandStatic(bool state, HALight* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleLightStateCommand(state);
  }
}

void HAInterface::onLightBrightnessCommandStatic(uint8_t brightness, HALight* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleLightBrightnessCommand(brightness);
  }
}

void HAInterface::onNumberCommandStatic(HANumeric number, HANumber* sender) {
  if (instance_ != nullptr) {
    instance_->handleNumberCommand(number, sender);
  }
}

void HAInterface::onSyncTimeButtonCommandStatic(HAButton* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleSyncTimeButtonCommand();
  }
}

void HAInterface::onReadSoilRawButtonCommandStatic(HAButton* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleReadSoilRawButtonCommand();
  }
}

void HAInterface::onStartHaDimButtonCommandStatic(HAButton* /*sender*/) {
  if (instance_ != nullptr) {
    instance_->handleStartHaDimButtonCommand();
  }
}

void HAInterface::handleFanSwitchCommand(bool state) {
  fanController_.setManualState(state);
  publishSwitchAndLightStates();
}

void HAInterface::handleFanAutoModeCommand(bool state) {
  configData_.fanAutoMode = state;
  fanController_.setAutoMode(state);
  configManager_.saveIfChanged(configData_);
  publishSwitchAndLightStates();
}

void HAInterface::handleLightAutoModeCommand(bool state) {
  configData_.lightAutoMode = state;
  lightController_.setAutoMode(state);
  configManager_.saveIfChanged(configData_);
  clockService_.configureScheduleAlarms(configData_.lightOnTimeMinutes, configData_.lightOffTimeMinutes);
  publishSwitchAndLightStates();
}

void HAInterface::handleLightHardPowerOffCommand(bool state) {
  lightController_.applyHardPowerOff(state);
  publishSwitchAndLightStates();
}

void HAInterface::handleFallbackSwitchCommand(bool state) {
  configData_.lightFallbackMode = state ? LIGHT_FALLBACK_USE_AUTO_MODE : LIGHT_FALLBACK_TURN_OFF;
  configManager_.saveIfChanged(configData_);
  publishSwitchAndLightStates();
}

void HAInterface::handleLightStateCommand(bool state) {
  lightController_.applyManualOnOff(state);
  publishSwitchAndLightStates();
}

void HAInterface::handleLightBrightnessCommand(uint8_t brightness) {
  lightController_.applyManualBrightness(clampPercent(brightness));
  publishSwitchAndLightStates();
}

void HAInterface::handleNumberCommand(HANumeric number, HANumber* sender) {
  const float floatValue = number.toFloat();
  const long intValue = static_cast<long>(lroundf(floatValue));

  bool persistConfig = false;
  bool reapplyThresholds = false;
  bool updateSoilCalibration = false;

  if (sender == &tempHighSetNumber_) {
    configData_.tempHighSet = clampFloat(floatValue, -40.0f, 125.0f);
    persistConfig = true;
    reapplyThresholds = true;
  } else if (sender == &tempHighClearNumber_) {
    configData_.tempHighClear = clampFloat(floatValue, -40.0f, 125.0f);
    persistConfig = true;
    reapplyThresholds = true;
  } else if (sender == &tempLowSetNumber_) {
    configData_.tempLowSet = clampFloat(floatValue, -40.0f, 125.0f);
    persistConfig = true;
    reapplyThresholds = true;
  } else if (sender == &tempLowClearNumber_) {
    configData_.tempLowClear = clampFloat(floatValue, -40.0f, 125.0f);
    persistConfig = true;
    reapplyThresholds = true;
  } else if (sender == &humHighSetNumber_) {
    configData_.humHighSet = clampFloat(floatValue, 0.0f, 100.0f);
    persistConfig = true;
    reapplyThresholds = true;
  } else if (sender == &humHighClearNumber_) {
    configData_.humHighClear = clampFloat(floatValue, 0.0f, 100.0f);
    persistConfig = true;
    reapplyThresholds = true;
  } else if (sender == &humLowSetNumber_) {
    configData_.humLowSet = clampFloat(floatValue, 0.0f, 100.0f);
    persistConfig = true;
    reapplyThresholds = true;
  } else if (sender == &humLowClearNumber_) {
    configData_.humLowClear = clampFloat(floatValue, 0.0f, 100.0f);
    persistConfig = true;
    reapplyThresholds = true;
  } else if (sender == &lightOnTimeNumber_) {
    configData_.lightOnTimeMinutes = clampUInt16(intValue, 0, 1439);
    persistConfig = true;
  } else if (sender == &lightOffTimeNumber_) {
    configData_.lightOffTimeMinutes = clampUInt16(intValue, 0, 1439);
    persistConfig = true;
  } else if (sender == &defaultLightDimMinutesNumber_) {
    configData_.defaultLightDimMinutes = clampUInt16(intValue, 0, 1440);
    pendingHaDimDurationMinutes_ = configData_.defaultLightDimMinutes;
    persistConfig = true;
  } else if (sender == &soilAirNumber_) {
    configData_.soilAir = static_cast<int16_t>(clampUInt16(intValue, 0, 1000));
    persistConfig = true;
    updateSoilCalibration = true;
  } else if (sender == &soilWaterNumber_) {
    configData_.soilWater = static_cast<int16_t>(clampUInt16(intValue, 0, 1000));
    persistConfig = true;
    updateSoilCalibration = true;
  } else if (sender == &soilDepthNumber_) {
    configData_.soilDepthMm = static_cast<int16_t>(clampUInt16(intValue, 0, 120));
    persistConfig = true;
    updateSoilCalibration = true;
  } else if (sender == &haDimTargetPercentNumber_) {
    pendingHaDimTargetPercent_ = clampPercent(intValue);
  } else if (sender == &haDimDurationMinutesNumber_) {
    pendingHaDimDurationMinutes_ = clampUInt16(intValue, 0, 1440);
  }

  if (updateSoilCalibration) {
    moistureSensor_.setCalibration(configData_.soilAir, configData_.soilWater, configData_.soilDepthMm);
  }

  if (reapplyThresholds) {
    sht_.applyThresholdConfig(configData_);
  }

  if (persistConfig) {
    configManager_.saveIfChanged(configData_);
  }

  if (sender == &lightOnTimeNumber_ || sender == &lightOffTimeNumber_) {
    clockService_.configureScheduleAlarms(configData_.lightOnTimeMinutes, configData_.lightOffTimeMinutes);
  }

  publishConfigValues();
}

void HAInterface::handleSyncTimeButtonCommand() {
  clockService_.syncFromNTP();
}

void HAInterface::handleReadSoilRawButtonCommand() {
  moistureSensor_.sampleNow();
  publishSensorValues(true);
}

void HAInterface::handleStartHaDimButtonCommand() {
  if (!lightController_.isAutoMode()) {
    const uint32_t durationMs = static_cast<uint32_t>(pendingHaDimDurationMinutes_) * 60000UL;
    lightController_.startHADimJob(pendingHaDimTargetPercent_, durationMs);
  }

  publishSwitchAndLightStates();
}


