#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <spi_drv.h>
#include <ArduinoOTA.h>
#include <InternalStorage.h>
#include <ArduinoHA.h>

#include "Credentials.h"
#include "RuntimeWatchdog.h"
#include "SystemTestHaCleanup.h"

#ifndef WIFI_SSID
#error "Credentials.h must define WIFI_SSID."
#endif
#ifndef WIFI_PASSWORD
#error "Credentials.h must define WIFI_PASSWORD."
#endif
#ifndef MQTT_HOST
#error "Credentials.h must define MQTT_HOST."
#endif
#ifndef MQTT_PORT
#error "Credentials.h must define MQTT_PORT."
#endif
#ifndef MQTT_USERNAME
#error "Credentials.h must define MQTT_USERNAME."
#endif
#ifndef MQTT_PASSWORD
#error "Credentials.h must define MQTT_PASSWORD."
#endif
#ifndef OTA_NAME
#error "Credentials.h must define OTA_NAME."
#endif
#ifndef OTA_PASSWORD
#error "Credentials.h must define OTA_PASSWORD."
#endif

namespace {

constexpr char TEST_ID[] = "i2c_passive_baseline";
constexpr char SKETCH_NAME[] = "02_I2cPassiveBaseline";
constexpr char SKETCH_VERSION[] = "1.2.4";
constexpr char DEVICE_ID[] = "grow_controller_tests_persistence_rtc";
constexpr char DEVICE_NAME[] = "Grow Controller Tests";
constexpr char MQTT_DISCOVERY_PREFIX[] = "homeassistant";
constexpr char MQTT_DATA_PREFIX[] = "smaeenhouse/test/i2c_passive_baseline/ha";
constexpr char RETIRED_STATUS_TOPIC[] = "smaeenhouse/test/i2c_passive_baseline/status";

constexpr uint8_t PIN_SHT_ALERT = A7;
constexpr uint8_t PIN_RTC_ALARM = 10;
constexpr uint8_t PIN_FAN_SWITCH = 2;
constexpr uint8_t PIN_FAN_TACH = A1;
constexpr uint8_t PIN_LIGHT_POWER_RELAY = 3;
constexpr uint8_t PIN_LIGHT_DIM_SHDN = 4;
constexpr uint8_t PIN_SOIL_MOISTURE = A0;
constexpr uint8_t PIN_LIGHT_SENSOR_INT = 9;
constexpr uint8_t PIN_I2C_SDA = SDA;
constexpr uint8_t PIN_I2C_SCL = SCL;

constexpr uint8_t FAN_OFF_LEVEL = LOW;
constexpr uint8_t LIGHT_RELAY_OPEN_LEVEL = LOW;
constexpr uint8_t LIGHT_DIM_SHDN_ASSERTED_LEVEL = LOW;

constexpr uint8_t I2C_ADDRESS_SHT31 = 0x44;
constexpr uint8_t I2C_ADDRESS_DS3231 = 0x68;
constexpr uint8_t I2C_ADDRESS_AT24C32 = 0x57;
constexpr uint8_t I2C_ADDRESS_AD5263 = 0x2C;
constexpr uint8_t I2C_ADDRESS_TSL25911 = 0x29;

constexpr uint32_t NETWORK_OPERATION_TIMEOUT_MS = 1000UL;
constexpr uint32_t WIFI_SETTLE_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr uint8_t WIFI_TIMEOUTS_BEFORE_MODULE_RESET = 3;
constexpr uint32_t MQTT_REINITIALIZE_INTERVAL_MS = 10000UL;
constexpr uint32_t OTA_MAX_POLL_GAP_MS = 2000UL;
constexpr uint32_t I2C_PROBE_INTERVAL_MS = 30000UL;
constexpr uint32_t HA_PUBLISH_INTERVAL_MS = 30000UL;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 30000UL;
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 1400;
constexpr uint16_t HA_ENTITY_LIMIT = 40;
constexpr uint8_t DEVICE_COUNT = 5;
constexpr uint8_t STEP_QUEUE_CAPACITY = 12;

enum class WifiConnectState : uint8_t {
  Idle,
  Settling,
  Connecting
};

struct I2cDeviceState {
  I2cDeviceState(const char* deviceName, uint8_t deviceAddress)
      : name(deviceName), address(deviceAddress) {}

  const char* name;
  uint8_t address;
  bool available = false;
  uint32_t errors = 0;
  uint16_t consecutiveErrors = 0;
  uint8_t lastError = 0;
};

struct PendingStep {
  uint32_t index = 0;
  char value[128] = "";
};

WiFiClient networkClient;

HADevice device(DEVICE_ID);
HAMqtt mqtt(networkClient, device, HA_ENTITY_LIMIT);
SystemTestHaCleanup::CleanupCursor retainedEntityCleanup(
    SystemTestHaCleanup::TEST_02);

HASensorNumber uptimeSensor("i2c_uptime_seconds");
HASensorNumber wifiJoinsSensor("i2c_wifi_joins");
HASensorNumber wifiTimeoutsSensor("i2c_wifi_timeouts");
HASensorNumber wifiModuleResetsSensor("i2c_wifi_module_resets");
HASensorNumber otaGapSensor("i2c_ota_gap_violations");
HASensorNumber scanSequenceSensor("i2c_scan_sequence");
HASensorNumber shtErrorsSensor("i2c_sht31_probe_errors");
HASensorNumber rtcErrorsSensor("i2c_ds3231_probe_errors");
HASensorNumber eepromErrorsSensor("i2c_at24c32_probe_errors");
HASensorNumber ad5263ErrorsSensor("i2c_ad5263_probe_errors");
HASensorNumber tslErrorsSensor("i2c_tsl25911_probe_errors");
HASensorNumber shtConsecutiveSensor("i2c_sht31_consecutive_errors");
HASensorNumber rtcConsecutiveSensor("i2c_ds3231_consecutive_errors");
HASensorNumber eepromConsecutiveSensor("i2c_at24c32_consecutive_errors");
HASensorNumber ad5263ConsecutiveSensor("i2c_ad5263_consecutive_errors");
HASensorNumber tslConsecutiveSensor("i2c_tsl25911_consecutive_errors");
HASensorNumber testStepIndexSensor("i2c_test_step_index");
HASensorNumber busStuckCountSensor("i2c_bus_stuck_count");
HASensor sketchIdentitySensor("sketch_identity");
HASensor busStatusSensor("i2c_bus_status");
HASensor lastErrorSensor("i2c_last_error");
HASensor probePhaseSensor("i2c_probe_phase");
HASensor resetCauseSensor("i2c_reset_cause");
HASensor testStepSensor("i2c_test_step");
HABinarySensor shtAvailableSensor("i2c_sht31_available");
HABinarySensor rtcAvailableSensor("i2c_ds3231_available");
HABinarySensor eepromAvailableSensor("i2c_at24c32_available");
HABinarySensor ad5263AvailableSensor("i2c_ad5263_available");
HABinarySensor tslAvailableSensor("i2c_tsl25911_available");
HABinarySensor sdaHighSensor("i2c_sda_high");
HABinarySensor sclHighSensor("i2c_scl_high");
HABinarySensor shtAlertLineSensor("i2c_sht_alert_line");
HABinarySensor rtcAlarmLineSensor("i2c_rtc_alarm_line");
HABinarySensor fanSafeSensor("persistence_rtc_fan_safe");
HABinarySensor relaySafeSensor("persistence_rtc_relay_safe");
HABinarySensor shdnSafeSensor("persistence_rtc_shdn_safe");

WifiConnectState wifiConnectState = WifiConnectState::Idle;
I2cDeviceState i2cDevices[DEVICE_COUNT] = {
  {"sht31", I2C_ADDRESS_SHT31},
  {"ds3231", I2C_ADDRESS_DS3231},
  {"at24c32", I2C_ADDRESS_AT24C32},
  {"ad5263", I2C_ADDRESS_AD5263},
  {"tsl25911", I2C_ADDRESS_TSL25911}
};
PendingStep pendingSteps[STEP_QUEUE_CAPACITY];

bool wifiWasConnected = false;
bool wifiJoinAttempted = false;
bool mqttWasConnected = false;
bool mqttInitialized = false;
bool otaInitialized = false;
bool identityPublished = false;
bool retiredStatusTopicCleared = false;
uint32_t wifiStateStartedMs = 0;
uint32_t nextWifiAttemptMs = 0;
uint32_t wifiConnectionCount = 0;
uint32_t wifiConnectTimeoutCount = 0;
uint32_t wifiModuleResetCount = 0;
uint8_t consecutiveWifiConnectTimeouts = 0;
uint32_t lastMqttInitializeAttemptMs = 0;
uint32_t lastOtaPollMs = 0;
uint32_t otaPollGapViolations = 0;
uint32_t lastI2cProbeMs = 0;
uint32_t lastHaPublishMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t safeStateEnforceCount = 0;
uint32_t scanSequence = 0;
uint32_t testStepIndex = 0;
uint8_t pendingStepHead = 0;
uint8_t pendingStepCount = 0;
bool sdaHigh = false;
bool sclHigh = false;
char lastI2cError[128] = "none";
bool lastScanSkippedForStuckBus = false;
bool diagnosticsReady = false;
const uint8_t bootResetCause = RuntimeWatchdog::resetCause();
uint32_t busStuckCount = 0;
char probePhase[128] = "boot_waiting_for_first_scan";

bool serialAvailable() {
  return static_cast<bool>(Serial);
}

const char* wireErrorDetail(uint8_t error) {
  switch (error) {
    case 0: return "OK";
    case 1: return "DATA_TOO_LONG";
    case 2: return "ADDRESS_NACK";
    case 3: return "DATA_NACK";
    case 4: return "OTHER";
    case 5: return "TIMEOUT";
    default: return "UNKNOWN";
  }
}

void enforceSafeOutputs() {
  digitalWrite(PIN_LIGHT_POWER_RELAY, LIGHT_RELAY_OPEN_LEVEL);
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  safeStateEnforceCount++;
}

void configurePinsForSafeState() {
  digitalWrite(PIN_LIGHT_POWER_RELAY, LIGHT_RELAY_OPEN_LEVEL);
  pinMode(PIN_LIGHT_POWER_RELAY, OUTPUT);
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  pinMode(PIN_FAN_SWITCH, OUTPUT);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);

  pinMode(PIN_SHT_ALERT, INPUT);
  pinMode(PIN_RTC_ALARM, INPUT_PULLUP);
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);
  pinMode(PIN_SOIL_MOISTURE, INPUT);
}
void enableI2cLineInputBuffers() {
#if defined(ARDUINO_ARCH_SAMD)
  PORT->Group[g_APinDescription[PIN_I2C_SDA].ulPort]
      .PINCFG[g_APinDescription[PIN_I2C_SDA].ulPin].reg |= PORT_PINCFG_INEN;
  PORT->Group[g_APinDescription[PIN_I2C_SCL].ulPort]
      .PINCFG[g_APinDescription[PIN_I2C_SCL].ulPin].reg |= PORT_PINCFG_INEN;
#endif
}

bool i2cLineIsHigh(uint8_t pin) {
  for (uint8_t sample = 0; sample < 3; ++sample) {
    if (digitalRead(pin) != HIGH) {
      return false;
    }
    if (sample < 2) {
      delayMicroseconds(50);
    }
  }
  return true;
}

void publishProbePhase(const char* value) {
  strncpy(probePhase, value, sizeof(probePhase) - 1U);
  probePhase[sizeof(probePhase) - 1U] = '\0';
  if (mqtt.isConnected()) {
    probePhaseSensor.setValue(probePhase);
  }
}


void recordTestStep(const char* value) {
  testStepIndex++;
  if (pendingStepCount == STEP_QUEUE_CAPACITY) {
    pendingStepHead = (pendingStepHead + 1U) % STEP_QUEUE_CAPACITY;
    pendingStepCount--;
  }
  const uint8_t tail = (pendingStepHead + pendingStepCount) % STEP_QUEUE_CAPACITY;
  pendingSteps[tail].index = testStepIndex;
  snprintf(pendingSteps[tail].value,
           sizeof(pendingSteps[tail].value),
           "%lu %s uptime=%lu",
           static_cast<unsigned long>(testStepIndex),
           value,
           static_cast<unsigned long>(millis() / 1000UL));
  pendingStepCount++;
}

uint8_t probeI2cDevice(I2cDeviceState& deviceState) {
  char phase[128];
  snprintf(phase,
           sizeof(phase),
           "scan=%lu phase=started device=%s address=0x%02X",
           static_cast<unsigned long>(scanSequence),
           deviceState.name,
           deviceState.address);
  publishProbePhase(phase);
  Wire.beginTransmission(deviceState.address);
  const uint8_t error = Wire.endTransmission();

  snprintf(phase,
           sizeof(phase),
           "scan=%lu phase=returned device=%s code=%u detail=%s",
           static_cast<unsigned long>(scanSequence),
           deviceState.name,
           error,
           wireErrorDetail(error));
  publishProbePhase(phase);
  deviceState.lastError = error;
  deviceState.available = error == 0;
  if (error == 0) {
    deviceState.consecutiveErrors = 0;
  } else {
    deviceState.errors++;
    if (deviceState.consecutiveErrors < UINT16_MAX) {
      deviceState.consecutiveErrors++;
    }
    snprintf(lastI2cError,
             sizeof(lastI2cError),
             "scan=%lu device=%s address=0x%02X code=%u detail=%s",
             static_cast<unsigned long>(scanSequence),
             deviceState.name,
             deviceState.address,
             error,
             wireErrorDetail(error));
  }
  return error;
}

void probeI2cInventory(uint32_t nowMs, bool force) {
  if (!diagnosticsReady) {
    return;
  }
  if (!force && (nowMs - lastI2cProbeMs) < I2C_PROBE_INTERVAL_MS) {
    return;
  }

  lastI2cProbeMs = nowMs;
  scanSequence++;
  sdaHigh = i2cLineIsHigh(PIN_I2C_SDA);
  sclHigh = i2cLineIsHigh(PIN_I2C_SCL);
  if (!sdaHigh || !sclHigh) {
    lastScanSkippedForStuckBus = true;
    busStuckCount++;
    for (uint8_t index = 0; index < DEVICE_COUNT; ++index) {
      i2cDevices[index].available = false;
    }
    snprintf(lastI2cError,
             sizeof(lastI2cError),
             "scan=%lu device=bus code=BUS_STUCK detail=sda_%s_scl_%s",
             static_cast<unsigned long>(scanSequence),
             sdaHigh ? "high" : "low",
             sclHigh ? "high" : "low");
    char phase[128];
    snprintf(phase,
             sizeof(phase),
             "scan=%lu phase=skipped reason=bus_stuck sda=%u scl=%u",
             static_cast<unsigned long>(scanSequence),
             sdaHigh ? 1U : 0U,
             sclHigh ? 1U : 0U);
    publishProbePhase(phase);
    recordTestStep(phase);
    return;
  }

  lastScanSkippedForStuckBus = false;
  bool allAvailable = true;
  for (uint8_t index = 0; index < DEVICE_COUNT; ++index) {
    allAvailable = probeI2cDevice(i2cDevices[index]) == 0 && allAvailable;
  }
  if (allAvailable) {
    strncpy(lastI2cError, "none", sizeof(lastI2cError));
  }

  char step[112];
  snprintf(step,
           sizeof(step),
           "scan_complete scan=%lu result=%s sda=%u scl=%u rtc=%u ad5263=%u",
           static_cast<unsigned long>(scanSequence),
           allAvailable ? "ok" : "device_error",
           sdaHigh ? 1U : 0U,
           sclHigh ? 1U : 0U,
           i2cDevices[1].available ? 1U : 0U,
           i2cDevices[3].available ? 1U : 0U);
  recordTestStep(step);

  if (serialAvailable()) {
    Serial.print(F("[I2C] "));
    Serial.println(step);
  }
}

void configureHomeAssistant() {
  device.setName(DEVICE_NAME);
  device.setManufacturer("Smaeenhouse");
  device.setModel("Arduino Nano 33 IoT I2C Passive Baseline Test");
  device.setSoftwareVersion(SKETCH_VERSION);
  device.enableExtendedUniqueIds();
  device.enableSharedAvailability();
  device.enableLastWill();
  mqtt.setDiscoveryPrefix(MQTT_DISCOVERY_PREFIX);
  mqtt.setDataPrefix(MQTT_DATA_PREFIX);
  mqtt.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

  uptimeSensor.setName("I2C Uptime");
  uptimeSensor.setDeviceClass("duration");
  uptimeSensor.setStateClass("total_increasing");
  uptimeSensor.setUnitOfMeasurement("s");
  wifiJoinsSensor.setName("I2C WiFi Joins");
  wifiTimeoutsSensor.setName("I2C WiFi Timeouts");
  wifiModuleResetsSensor.setName("I2C WiFi Module Resets");
  otaGapSensor.setName("I2C OTA Gap Violations");
  scanSequenceSensor.setName("I2C Scan Sequence");
  testStepIndexSensor.setName("I2C Test Step Index");
  busStuckCountSensor.setName("I2C Bus Stuck Count");

  HASensorNumber* totalSensors[DEVICE_COUNT] = {
    &shtErrorsSensor, &rtcErrorsSensor, &eepromErrorsSensor, &ad5263ErrorsSensor, &tslErrorsSensor
  };
  HASensorNumber* consecutiveSensors[DEVICE_COUNT] = {
    &shtConsecutiveSensor, &rtcConsecutiveSensor, &eepromConsecutiveSensor,
    &ad5263ConsecutiveSensor, &tslConsecutiveSensor
  };
  const char* totalNames[DEVICE_COUNT] = {
    "SHT31 Probe Errors", "DS3231 Probe Errors", "AT24C32 Probe Errors",
    "AD5263 Probe Errors", "TSL25911 Probe Errors"
  };
  const char* consecutiveNames[DEVICE_COUNT] = {
    "SHT31 Consecutive Probe Errors", "DS3231 Consecutive Probe Errors",
    "AT24C32 Consecutive Probe Errors", "AD5263 Consecutive Probe Errors",
    "TSL25911 Consecutive Probe Errors"
  };
  for (uint8_t index = 0; index < DEVICE_COUNT; ++index) {
    totalSensors[index]->setName(totalNames[index]);
    totalSensors[index]->setStateClass("total_increasing");
    consecutiveSensors[index]->setName(consecutiveNames[index]);
  }
  wifiJoinsSensor.setStateClass("total_increasing");
  wifiTimeoutsSensor.setStateClass("total_increasing");
  wifiModuleResetsSensor.setStateClass("total_increasing");
  otaGapSensor.setStateClass("total_increasing");
  scanSequenceSensor.setStateClass("total_increasing");
  testStepIndexSensor.setStateClass("total_increasing");
  busStuckCountSensor.setStateClass("total_increasing");

  sketchIdentitySensor.setName("Sketch Identity");
  busStatusSensor.setName("I2C Bus Status");
  lastErrorSensor.setName("I2C Last Error");
  probePhaseSensor.setName("I2C Probe Phase");
  resetCauseSensor.setName("I2C Reset Cause");
  testStepSensor.setName("I2C Test Step");
  shtAvailableSensor.setName("SHT31 Available");
  rtcAvailableSensor.setName("DS3231 Available");
  eepromAvailableSensor.setName("AT24C32 Available");
  ad5263AvailableSensor.setName("AD5263 Available");
  tslAvailableSensor.setName("TSL25911 Available");
  sdaHighSensor.setName("I2C SDA High");
  sclHighSensor.setName("I2C SCL High");
  shtAlertLineSensor.setName("SHT Alert Line");
  rtcAlarmLineSensor.setName("RTC Alarm Line");
  fanSafeSensor.setName("Fan Output Off");
  relaySafeSensor.setName("Light Relay Open");
  shdnSafeSensor.setName("Light SHDN Asserted");
}

void publishHaBootIdentity() {
  if (!mqtt.isConnected() || identityPublished) {
    return;
  }
  char identity[64];
  snprintf(identity, sizeof(identity), "%s v%s", SKETCH_NAME, SKETCH_VERSION);
  identityPublished = sketchIdentitySensor.setValue(identity);
}

void cleanupRetiredStatusTopic() {
  if (!mqtt.isConnected() || retiredStatusTopicCleared) {
    return;
  }
  retiredStatusTopicCleared = mqtt.publish(RETIRED_STATUS_TOPIC, "", true);
}

void publishHaState(uint32_t nowMs, bool force) {
  if (!mqtt.isConnected()) {
    return;
  }
  if (!force && (nowMs - lastHaPublishMs) < HA_PUBLISH_INTERVAL_MS) {
    return;
  }
  lastHaPublishMs = nowMs;

  uptimeSensor.setValue(static_cast<uint32_t>(nowMs / 1000UL), force);
  wifiJoinsSensor.setValue(wifiConnectionCount, force);
  wifiTimeoutsSensor.setValue(wifiConnectTimeoutCount, force);
  wifiModuleResetsSensor.setValue(wifiModuleResetCount, force);
  otaGapSensor.setValue(otaPollGapViolations, force);
  scanSequenceSensor.setValue(scanSequence, true);

  busStuckCountSensor.setValue(busStuckCount, force);
  HASensorNumber* totalSensors[DEVICE_COUNT] = {
    &shtErrorsSensor, &rtcErrorsSensor, &eepromErrorsSensor, &ad5263ErrorsSensor, &tslErrorsSensor
  };
  HASensorNumber* consecutiveSensors[DEVICE_COUNT] = {
    &shtConsecutiveSensor, &rtcConsecutiveSensor, &eepromConsecutiveSensor,
    &ad5263ConsecutiveSensor, &tslConsecutiveSensor
  };
  for (uint8_t index = 0; index < DEVICE_COUNT; ++index) {
    totalSensors[index]->setValue(i2cDevices[index].errors, force);
    consecutiveSensors[index]->setValue(i2cDevices[index].consecutiveErrors, force);
  }

  char busStatus[128];
  snprintf(busStatus,
           sizeof(busStatus),
           "scan=%lu result=%s sda=%s scl=%s controls_rtc=%s controls_ad5263=%s",
           static_cast<unsigned long>(scanSequence),
           scanSequence == 0 ? "not_scanned"
                             : (lastScanSkippedForStuckBus ? "bus_stuck_skipped" : "completed"),
           sdaHigh ? "high" : "low",
           sclHigh ? "high" : "low",
           lastScanSkippedForStuckBus
               ? "not_probed"
               : (i2cDevices[1].available ? "ok" : "missing"),
           lastScanSkippedForStuckBus
               ? "not_probed"
               : (i2cDevices[3].available ? "ok" : "missing"));
  busStatusSensor.setValue(busStatus);
  lastErrorSensor.setValue(lastI2cError);
  probePhaseSensor.setValue(probePhase);
  resetCauseSensor.setValue(RuntimeWatchdog::resetCauseText(bootResetCause));

  shtAvailableSensor.setState(i2cDevices[0].available, force);
  rtcAvailableSensor.setState(i2cDevices[1].available, force);
  eepromAvailableSensor.setState(i2cDevices[2].available, force);
  ad5263AvailableSensor.setState(i2cDevices[3].available, force);
  tslAvailableSensor.setState(i2cDevices[4].available, force);
  sdaHighSensor.setState(sdaHigh, force);
  sclHighSensor.setState(sclHigh, force);
  shtAlertLineSensor.setState(digitalRead(PIN_SHT_ALERT) == HIGH, force);
  rtcAlarmLineSensor.setState(digitalRead(PIN_RTC_ALARM) == LOW, force);
  fanSafeSensor.setState(digitalRead(PIN_FAN_SWITCH) == FAN_OFF_LEVEL, force);
  relaySafeSensor.setState(digitalRead(PIN_LIGHT_POWER_RELAY) == LIGHT_RELAY_OPEN_LEVEL, force);
  shdnSafeSensor.setState(digitalRead(PIN_LIGHT_DIM_SHDN) == LIGHT_DIM_SHDN_ASSERTED_LEVEL, force);
  publishHaBootIdentity();
}

void flushPendingSteps() {
  if (!mqtt.isConnected() || pendingStepCount == 0) {
    return;
  }
  const PendingStep& step = pendingSteps[pendingStepHead];
  testStepIndexSensor.setValue(step.index, true);
  publishHaState(millis(), true);
  if (!testStepSensor.setValue(step.value)) {
    return;
  }
  pendingStepHead = (pendingStepHead + 1U) % STEP_QUEUE_CAPACITY;
  pendingStepCount--;
}

void printIpAddress(const __FlashStringHelper* label, const IPAddress& address) {
  if (!serialAvailable()) {
    return;
  }
  Serial.print(label);
  Serial.println(address);
}

void printWifiConnected() {
  if (!serialAvailable()) {
    return;
  }
  Serial.print(F("[WiFi] Connected to "));
  Serial.println(WiFi.SSID());
  printIpAddress(F("[WiFi] IP: "), WiFi.localIP());
  Serial.print(F("[WiFi] RSSI: "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
}

void beginOta() {
  ArduinoOTA.begin(WiFi.localIP(), OTA_NAME, OTA_PASSWORD, InternalStorage);
  otaInitialized = true;
  lastOtaPollMs = millis();
}

bool beginMqttConnection(uint32_t nowMs) {
  if (mqttInitialized) {
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (lastMqttInitializeAttemptMs != 0 &&
      (nowMs - lastMqttInitializeAttemptMs) < MQTT_REINITIALIZE_INTERVAL_MS) {
    return false;
  }

  lastMqttInitializeAttemptMs = nowMs;
  mqttInitialized = mqtt.begin(MQTT_HOST, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD);
  if (!mqttInitialized) {
    recordTestStep("mqtt_initialize_failed");
  }
  return mqttInitialized;
}

void onWifiConnected() {
  printWifiConnected();
  beginOta();
  beginMqttConnection(millis());
  recordTestStep("wifi_connected");
}

void onWifiDisconnected() {
  otaInitialized = false;
  if (mqttInitialized) {
    mqtt.disconnect();
    mqttInitialized = false;
  }
  lastMqttInitializeAttemptMs = 0;
  mqttWasConnected = false;
  recordTestStep("wifi_disconnected");
}

void startWifiReconnect(uint32_t nowMs) {
  if (wifiJoinAttempted) {
    WiFi.disconnect();
  }
  wifiJoinAttempted = true;
  wifiConnectState = WifiConnectState::Settling;
  wifiStateStartedMs = nowMs;
}

void serviceWifi(uint32_t nowMs) {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected) {
    wifiConnectState = WifiConnectState::Idle;
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      wifiConnectionCount++;
      consecutiveWifiConnectTimeouts = 0;
      onWifiConnected();
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    onWifiDisconnected();
    wifiConnectState = WifiConnectState::Idle;
    nextWifiAttemptMs = nowMs;
  }

  switch (wifiConnectState) {
    case WifiConnectState::Idle:
      if (static_cast<int32_t>(nowMs - nextWifiAttemptMs) >= 0) {
        startWifiReconnect(nowMs);
      }
      break;
    case WifiConnectState::Settling:
      if ((nowMs - wifiStateStartedMs) >= WIFI_SETTLE_MS) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiConnectState = WifiConnectState::Connecting;
        wifiStateStartedMs = millis();
      }
      break;
    case WifiConnectState::Connecting:
      if ((nowMs - wifiStateStartedMs) >= WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect();
        wifiConnectTimeoutCount++;
        consecutiveWifiConnectTimeouts++;
        if (consecutiveWifiConnectTimeouts >= WIFI_TIMEOUTS_BEFORE_MODULE_RESET) {
          networkClient.stop();
          SpiDrv::begin(true);
          WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
          wifiModuleResetCount++;
          consecutiveWifiConnectTimeouts = 0;
          wifiConnectState = WifiConnectState::Settling;
          wifiStateStartedMs = millis();
          recordTestStep("wifi_module_reset");
        } else {
          wifiConnectState = WifiConnectState::Idle;
          nextWifiAttemptMs = nowMs + WIFI_RETRY_INTERVAL_MS;
          recordTestStep("wifi_connect_timeout");
        }
      }
      break;
  }
}

void serviceOta() {
  if (!otaInitialized || WiFi.status() != WL_CONNECTED) {
    return;
  }
  const uint32_t beforePollMs = millis();
  if ((beforePollMs - lastOtaPollMs) > OTA_MAX_POLL_GAP_MS) {
    otaPollGapViolations++;
  }
  ArduinoOTA.poll();
  lastOtaPollMs = millis();
}

void serviceMqtt(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqttInitialized && !beginMqttConnection(nowMs)) {
    return;
  }
  mqtt.loop();
  if (retainedEntityCleanup.service(
          mqtt, MQTT_DISCOVERY_PREFIX, DEVICE_ID, MQTT_DATA_PREFIX)) {
    recordTestStep("retained_entity_cleanup_complete");
  }
  const bool connected = mqtt.isConnected();
  if (connected && !mqttWasConnected) {
    mqttWasConnected = true;
    cleanupRetiredStatusTopic();
    publishHaBootIdentity();
    publishHaState(nowMs, true);
    recordTestStep("ha_mqtt_connected");
    if (!diagnosticsReady) {
      diagnosticsReady = true;
      recordTestStep("passive_scans_enabled_after_boot_publish");
    }
  } else if (!connected && mqttWasConnected) {
    recordTestStep("ha_mqtt_disconnected");
    mqttWasConnected = false;
  }
  cleanupRetiredStatusTopic();
  publishHaBootIdentity();
  publishHaState(nowMs, false);
  flushPendingSteps();
}

void printStatus(uint32_t nowMs) {
  if (!serialAvailable() || (nowMs - lastStatusPrintMs) < STATUS_PRINT_INTERVAL_MS) {
    return;
  }
  lastStatusPrintMs = nowMs;
  Serial.print(F("[Status] uptime="));
  Serial.print(nowMs / 1000UL);
  Serial.print(F(" scan="));
  Serial.print(scanSequence);
  Serial.print(F(" wifi="));
  Serial.print(WiFi.status() == WL_CONNECTED ? F("UP") : F("DOWN"));
  Serial.print(F(" mqtt="));
  Serial.print(mqtt.isConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(" sda="));
  Serial.print(sdaHigh ? F("HIGH") : F("LOW"));
  Serial.print(F(" scl="));
  Serial.println(sclHigh ? F("HIGH") : F("LOW"));
}

}  // namespace

void setup() {
  configurePinsForSafeState();
  enforceSafeOutputs();

  Serial.begin(115200);
  Wire.begin();
  enableI2cLineInputBuffers();
  recordTestStep("boot_safe_outputs_verified");
  char resetStep[64];
  snprintf(resetStep,
           sizeof(resetStep),
           "boot_reset_cause=%s",
           RuntimeWatchdog::resetCauseText(bootResetCause));
  recordTestStep(resetStep);

  configureHomeAssistant();
  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  nextWifiAttemptMs = millis();

  if (serialAvailable()) {
    Serial.println();
    Serial.print(F("Grow Controller I2C Passive Baseline Test v"));
    Serial.println(SKETCH_VERSION);
    Serial.println(F("Passive probes only; no device initialization, register access, or ISRs."));
  }
}

void loop() {
  serviceOta();

  uint32_t nowMs = millis();
  serviceWifi(nowMs);
  serviceOta();

  nowMs = millis();
  serviceMqtt(nowMs);
  serviceOta();

  nowMs = millis();
  probeI2cInventory(nowMs, false);
  serviceOta();

  printStatus(millis());
}
