#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <spi_drv.h>
#include <ArduinoOTA.h>
#include <InternalStorage.h>
#include <ArduinoHA.h>
#include <SensirionI2cSht3x.h>

#include "Credentials.h"

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

constexpr char TEST_ID[] = "sht_hardware_baseline";
constexpr char SKETCH_NAME[] = "03_ShtHardwareBaseline";
constexpr char SKETCH_VERSION[] = "1.1.1";
constexpr char DEVICE_ID[] = "grow_controller_tests_persistence_rtc";
constexpr char DEVICE_NAME[] = "Grow Controller Tests";
constexpr char MQTT_DISCOVERY_PREFIX[] = "homeassistant";
constexpr char MQTT_DATA_PREFIX[] = "smaeenhouse/test/persistence_rtc_baseline/ha";

constexpr uint8_t PIN_SHT_ALERT = A7;
constexpr uint8_t PIN_RTC_ALARM = 10;
constexpr uint8_t PIN_FAN_SWITCH = 2;
constexpr uint8_t PIN_FAN_TACH = A1;
constexpr uint8_t PIN_LIGHT_POWER_RELAY = 3;
constexpr uint8_t PIN_LIGHT_DIM_SHDN = 4;
constexpr uint8_t PIN_SOIL_MOISTURE = A0;
constexpr uint8_t PIN_LIGHT_SENSOR_INT = 9;

constexpr uint8_t FAN_OFF_LEVEL = LOW;
constexpr uint8_t LIGHT_RELAY_OPEN_LEVEL = LOW;
constexpr uint8_t LIGHT_DIM_SHDN_ASSERTED_LEVEL = LOW;

constexpr uint8_t I2C_ADDRESS_SHT31 = SHT30_I2C_ADDR_45;
constexpr uint8_t SHT_ADDRESS_LOW = SHT30_I2C_ADDR_44;
constexpr uint8_t SHT_ADDRESS_HIGH = SHT30_I2C_ADDR_45;

constexpr uint8_t SHT31_ALERT_READ_MSB = 0xE1;
constexpr uint8_t SHT31_ALERT_WRITE_MSB = 0x61;
constexpr uint8_t SHT31_ALERT_RHS = 0x1F;
constexpr uint8_t SHT31_ALERT_RHC = 0x14;
constexpr uint8_t SHT31_ALERT_RLC = 0x09;
constexpr uint8_t SHT31_ALERT_RLS = 0x02;
constexpr uint8_t SHT31_ALERT_WHS = 0x1D;
constexpr uint8_t SHT31_ALERT_WHC = 0x16;
constexpr uint8_t SHT31_ALERT_WLC = 0x0B;
constexpr uint8_t SHT31_ALERT_WLS = 0x00;

constexpr uint32_t NETWORK_OPERATION_TIMEOUT_MS = 1000UL;
constexpr uint32_t WIFI_SETTLE_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr uint8_t WIFI_TIMEOUTS_BEFORE_MODULE_RESET = 3;
constexpr uint32_t OTA_MAX_POLL_GAP_MS = 2000UL;
constexpr uint32_t SHT_COMMAND_GUARD_MS = 1UL;
constexpr uint32_t SHT_SAMPLE_INTERVAL_MS = 2000UL;
constexpr uint32_t SHT_LIMIT_INTERVAL_MS = 30000UL;
constexpr uint32_t SHT_RECOVERY_BACKOFF_MS = 30000UL;
constexpr uint8_t SHT_RECOVERY_ERROR_THRESHOLD = 3;
constexpr uint8_t SHT_REQUIRED_HEALTHY_SAMPLES = 5;
constexpr uint32_t HA_PUBLISH_INTERVAL_MS = 10000UL;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 10000UL;
constexpr uint16_t HA_ENTITY_LIMIT = 48;
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 1200;
constexpr uint8_t STEP_QUEUE_CAPACITY = 12;

const char* const RETIRED_IDENTITY_ENTITY_IDS[] = {"sketch_name", "sketch_version"};

volatile bool shtAlertPending = false;
volatile bool rtcAlarmPending = false;
volatile uint32_t fanTachPulseCount = 0;

void onShtAlert() {
  shtAlertPending = true;
}

void onRtcAlarm() {
  rtcAlarmPending = true;
}

void onFanTachPulse() {
  fanTachPulseCount++;
}

bool pinSupportsExternalInterrupt(uint8_t pin) {
  return pin < PINS_COUNT && g_APinDescription[pin].ulExtInt != EXTERNAL_INT_NONE;
}

enum class WifiConnectState : uint8_t {
  Idle,
  Settling,
  Connecting
};

struct LimitValue {
  bool ok = false;
  uint16_t raw = 0;
  float temperature = NAN;
  float humidity = NAN;
};

struct ShtState {
  bool address44Present = false;
  bool address45Present = false;
  bool primaryPresent = false;
  bool initialized = false;
  bool measurementRunning = false;
  bool measurementOk = false;
  bool statusOk = false;
  bool limitsOk = false;
  bool alertLineLow = false;
  bool alertInterruptAttached = false;
  bool alertInterruptSeen = false;
  bool recoveryPending = false;
  bool recoveryVerificationPending = false;
  uint16_t statusRegister = 0;
  int16_t lastMeasurementError = 0;
  int16_t lastStatusError = 0;
  uint16_t lastLimitError = 0;
  int16_t lastRecoveryError = 0;
  float temperature = NAN;
  float humidity = NAN;
  uint32_t sampleCount = 0;
  uint32_t measurementErrors = 0;
  uint32_t statusErrors = 0;
  uint32_t limitReadCount = 0;
  uint32_t limitErrors = 0;
  uint32_t addressProbeFailures = 0;
  uint32_t recoveryAttempts = 0;
  uint32_t recoveries = 0;
  uint32_t recoveryFailures = 0;
  uint32_t roundTripAttempts = 0;
  uint32_t roundTripSuccesses = 0;
  uint32_t roundTripFailures = 0;
  uint16_t consecutiveMeasurementErrors = 0;
  uint16_t consecutiveStatusErrors = 0;
  uint16_t consecutiveLimitErrors = 0;
  uint16_t consecutiveSampleFailures = 0;
  uint16_t maxConsecutiveSampleFailures = 0;
  uint16_t healthySampleStreak = 0;
  uint32_t nextRecoveryMs = 0;
  char lastError[112] = "none";
  char roundTripResult[112] = "not run";
  LimitValue highSet;
  LimitValue highClear;
  LimitValue lowSet;
  LimitValue lowClear;
};

struct PendingStep {
  uint32_t index = 0;
  char value[88] = "";
};

WiFiClient networkClient;
HADevice device(DEVICE_ID);
HAMqtt mqtt(networkClient, device, HA_ENTITY_LIMIT);
SensirionI2cSht3x shtSensor;

HASensorNumber uptimeSensor("persistence_rtc_uptime_seconds");
HASensorNumber wifiJoinsSensor("persistence_rtc_wifi_joins");
HASensorNumber wifiTimeoutsSensor("persistence_rtc_wifi_timeouts");
HASensorNumber wifiModuleResetsSensor("persistence_rtc_wifi_module_resets");
HASensorNumber otaGapSensor("persistence_rtc_ota_gap_violations");
HASensorNumber temperatureSensor("temperature", HASensorNumber::PrecisionP2);
HASensorNumber humiditySensor("humidity", HASensorNumber::PrecisionP1);
HASensorNumber measurementErrorsSensor("sht_measurement_errors");
HASensorNumber statusErrorsSensor("sht_status_errors");
HASensorNumber limitErrorsSensor("sht_limit_errors");
HASensorNumber addressProbeFailuresSensor("sht_address_probe_failures");
HASensorNumber consecutiveFailuresSensor("sht_consecutive_failures");
HASensorNumber maxConsecutiveFailuresSensor("sht_max_consecutive_failures");
HASensorNumber recoveryAttemptsSensor("sht_recovery_attempts");
HASensorNumber recoveriesSensor("sht_recoveries");
HASensorNumber recoveryFailuresSensor("sht_recovery_failures");
HASensorNumber roundTripAttemptsSensor("sht_round_trip_attempts");
HASensorNumber roundTripSuccessesSensor("sht_round_trip_successes");
HASensorNumber roundTripFailuresSensor("sht_round_trip_failures");
HASensorNumber testStepIndexSensor("sht_test_step_index");
HASensorNumber statusRegisterSensor("sht_status_register");
HASensorNumber highSetRawSensor("sht_limit_high_set_raw");
HASensorNumber highClearRawSensor("sht_limit_high_clear_raw");
HASensorNumber lowSetRawSensor("sht_limit_low_set_raw");
HASensorNumber lowClearRawSensor("sht_limit_low_clear_raw");
HASensor sketchIdentitySensor("sketch_identity");
HASensor testStepSensor("sht_test_step");
HASensor lastErrorSensor("sht_last_error");
HASensor roundTripResultSensor("sht_round_trip_result");
HASensor statusFlagsSensor("sht_status_flags");
HABinarySensor shtFaultSensor("sht_fault");
HABinarySensor address45Sensor("sht_address_45");
HABinarySensor measurementOkSensor("sht_measurement_ok");
HABinarySensor statusOkSensor("sht_status_ok");
HABinarySensor limitsOkSensor("sht_limits_ok");
HABinarySensor alertLineSensor("sht_alert_line");
HABinarySensor interruptAttachedSensor("sht_interrupt_attached");
HABinarySensor fanSafeSensor("persistence_rtc_fan_safe");
HABinarySensor relaySafeSensor("persistence_rtc_relay_safe");
HABinarySensor shdnSafeSensor("persistence_rtc_shdn_safe");
HAButton roundTripButton("run_sht_limit_round_trip");

WifiConnectState wifiConnectState = WifiConnectState::Idle;
ShtState shtState;
PendingStep pendingSteps[STEP_QUEUE_CAPACITY];
bool wifiWasConnected = false;
bool mqttWasConnected = false;
bool mqttInitialized = false;
bool otaInitialized = false;
bool identityPublished = false;
bool retiredIdentityTopicsCleared = false;
bool roundTripRequested = false;
bool roundTripInProgress = false;
bool roundTripMeasurementPause = false;
uint8_t pendingStepHead = 0;
uint8_t pendingStepCount = 0;
uint32_t droppedStepCount = 0;
uint32_t testStepIndex = 0;
uint32_t wifiStateStartedMs = 0;
uint32_t nextWifiAttemptMs = 0;
uint32_t wifiConnectionCount = 0;
uint32_t wifiConnectTimeoutCount = 0;
uint32_t wifiModuleResetCount = 0;
uint8_t consecutiveWifiConnectTimeouts = 0;
uint32_t lastOtaPollMs = 0;
uint32_t otaPollGapViolations = 0;
uint32_t lastSampleMs = 0;
uint32_t lastLimitReadMs = 0;
uint32_t lastHaPublishMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t safeStateEnforceCount = 0;
char errorMessage[96];

void publishHaState(uint32_t nowMs, bool force);
void flushPendingSteps();
bool readShtStatus();

bool serialAvailable() {
  return static_cast<bool>(Serial);
}

void recordTestStep(const char* step) {
  testStepIndex++;
  PendingStep entry;
  entry.index = testStepIndex;
  snprintf(entry.value,
           sizeof(entry.value),
           "%lu %s uptime=%lu",
           static_cast<unsigned long>(entry.index),
           step,
           static_cast<unsigned long>(millis() / 1000UL));

  if (pendingStepCount == STEP_QUEUE_CAPACITY) {
    pendingStepHead = (pendingStepHead + 1U) % STEP_QUEUE_CAPACITY;
    pendingStepCount--;
    droppedStepCount++;
  }
  const uint8_t tail = (pendingStepHead + pendingStepCount) % STEP_QUEUE_CAPACITY;
  pendingSteps[tail] = entry;
  pendingStepCount++;
  flushPendingSteps();
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

  pinMode(PIN_SHT_ALERT, INPUT_PULLUP);
  pinMode(PIN_RTC_ALARM, INPUT_PULLUP);
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);
  pinMode(PIN_SOIL_MOISTURE, INPUT);

  const int shtInterruptId = digitalPinToInterrupt(PIN_SHT_ALERT);
  shtState.alertInterruptAttached = pinSupportsExternalInterrupt(PIN_SHT_ALERT);
  if (shtState.alertInterruptAttached) {
    attachInterrupt(shtInterruptId, onShtAlert, FALLING);
  }
  if (pinSupportsExternalInterrupt(PIN_RTC_ALARM)) {
    attachInterrupt(digitalPinToInterrupt(PIN_RTC_ALARM), onRtcAlarm, FALLING);
  }
  if (pinSupportsExternalInterrupt(PIN_FAN_TACH)) {
    attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), onFanTachPulse, FALLING);
  }
}

void setLastError(const char* context, int32_t error) {
  snprintf(shtState.lastError,
           sizeof(shtState.lastError),
           "%s error=%ld",
           context,
           static_cast<long>(error));
}

bool probeI2cAddress(uint8_t address, const char* context, bool countFailure) {
  Wire.beginTransmission(address);
  const uint8_t error = Wire.endTransmission();
  if (error == 0) {
    return true;
  }
  if (countFailure) {
    shtState.addressProbeFailures++;
    setLastError(context, error);
  }
  return false;
}

uint8_t crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1) ^ 0x31U) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

float decodeAlertTemp(uint16_t raw) {
  const uint16_t temp9 = raw & 0x01FFU;
  return (static_cast<float>(temp9) * 175.0f / 511.0f) - 45.0f;
}

float decodeAlertHum(uint16_t raw) {
  return static_cast<float>((raw >> 9) & 0x7FU) * 100.0f / 127.0f;
}

void recordLimitFailure(const char* context, uint16_t error) {
  shtState.limitsOk = false;
  shtState.limitErrors++;
  if (shtState.consecutiveLimitErrors < UINT16_MAX) {
    shtState.consecutiveLimitErrors++;
  }
  shtState.lastLimitError = error;
  setLastError(context, error);
}

bool readLimit(uint8_t commandLsb, LimitValue& limit, const char* context) {
  limit.ok = false;
  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(SHT31_ALERT_READ_MSB);
  Wire.write(commandLsb);
  const uint8_t transmissionError = Wire.endTransmission();
  if (transmissionError != 0) {
    recordLimitFailure(context, transmissionError);
    return false;
  }

  delay(SHT_COMMAND_GUARD_MS);
  if (Wire.requestFrom(I2C_ADDRESS_SHT31, static_cast<uint8_t>(3)) != 3) {
    while (Wire.available()) {
      Wire.read();
    }
    recordLimitFailure(context, 0x00FFU);
    return false;
  }

  uint8_t data[2] = {static_cast<uint8_t>(Wire.read()), static_cast<uint8_t>(Wire.read())};
  const uint8_t receivedCrc = Wire.read();
  if (crc8(data, sizeof(data)) != receivedCrc) {
    recordLimitFailure(context, 0x00FEU);
    return false;
  }

  limit.raw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  limit.temperature = decodeAlertTemp(limit.raw);
  limit.humidity = decodeAlertHum(limit.raw);
  limit.ok = true;
  return true;
}

bool writeLimitRaw(uint8_t commandLsb, uint16_t raw, const char* context) {
  const uint8_t data[2] = {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFFU)};
  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(SHT31_ALERT_WRITE_MSB);
  Wire.write(commandLsb);
  Wire.write(data[0]);
  Wire.write(data[1]);
  Wire.write(crc8(data, sizeof(data)));
  const uint8_t error = Wire.endTransmission();
  if (error != 0) {
    recordLimitFailure(context, error);
    return false;
  }
  delay(SHT_COMMAND_GUARD_MS);
  return true;
}

bool readAllLimits(bool countRead) {
  if (countRead) {
    shtState.limitReadCount++;
  }
  bool ok = readLimit(SHT31_ALERT_RHS, shtState.highSet, "read_high_set");
  if (ok) ok = readLimit(SHT31_ALERT_RHC, shtState.highClear, "read_high_clear");
  if (ok) ok = readLimit(SHT31_ALERT_RLS, shtState.lowSet, "read_low_set");
  if (ok) ok = readLimit(SHT31_ALERT_RLC, shtState.lowClear, "read_low_clear");
  shtState.limitsOk = ok;
  if (ok) {
    shtState.consecutiveLimitErrors = 0;
    shtState.lastLimitError = 0;
  }
  return ok;
}

void printError(const __FlashStringHelper* context, int16_t error) {
  if (!serialAvailable() || error == NO_ERROR) {
    return;
  }

  errorToString(error, errorMessage, sizeof(errorMessage));
  Serial.print(context);
  Serial.print(F(" failed: "));
  Serial.println(errorMessage);
}

void scheduleRecovery(uint32_t nowMs) {
  if (!shtState.recoveryPending) {
    shtState.recoveryPending = true;
    shtState.nextRecoveryMs = nowMs + SHT_RECOVERY_BACKOFF_MS;
    recordTestStep("sht_recovery_scheduled");
  }
}

void initializeSht() {
  shtState.address44Present = probeI2cAddress(SHT_ADDRESS_LOW, "probe_0x44", false);
  shtState.address45Present = probeI2cAddress(SHT_ADDRESS_HIGH, "probe_0x45", true);
  shtState.primaryPresent = shtState.address45Present;
  recordTestStep(shtState.primaryPresent ? "sht_address_verified" : "sht_address_missing");

  shtSensor.begin(Wire, I2C_ADDRESS_SHT31);
  const int16_t stopError = shtSensor.stopMeasurement();
  if (stopError != NO_ERROR) {
    setLastError("boot_stop_measurement", stopError);
  }
  delay(1);
  const int16_t resetError = shtSensor.softReset();
  printError(F("[SHT] softReset"), resetError);
  if (resetError != NO_ERROR) {
    setLastError("boot_soft_reset", resetError);
    recordTestStep("sht_boot_reset_failed");
    scheduleRecovery(millis());
    return;
  }
  delay(10);

  const bool bootStatusOk = readShtStatus();
  recordTestStep(bootStatusOk ? "sht_status_read" : "sht_status_read_failed");
  const bool bootLimitsOk = readAllLimits(true);
  recordTestStep(bootLimitsOk ? "sht_limits_read" : "sht_limits_read_failed");

  const int16_t startError = shtSensor.startPeriodicMeasurement(REPEATABILITY_MEDIUM, MPS_ONE_PER_SECOND);
  printError(F("[SHT] startPeriodicMeasurement"), startError);
  shtState.measurementRunning = startError == NO_ERROR;
  shtState.initialized = shtState.primaryPresent && shtState.measurementRunning;
  if (!shtState.initialized) {
    setLastError("boot_start_periodic", startError);
    recordTestStep("sht_boot_start_failed");
    scheduleRecovery(millis());
    return;
  }
  lastSampleMs = millis();
  recordTestStep("sht_periodic_started");
}

bool readShtStatus() {
  uint16_t status = 0;
  const bool wasOk = shtState.statusOk;
  const int16_t error = shtSensor.readStatusRegister(status);
  shtState.lastStatusError = error;
  if (error != NO_ERROR) {
    shtState.statusOk = false;
    shtState.statusErrors++;
    if (shtState.consecutiveStatusErrors < UINT16_MAX) {
      shtState.consecutiveStatusErrors++;
    }
    setLastError("status_read", error);
    printError(F("[SHT] readStatusRegister"), error);
    if (wasOk) {
      recordTestStep("sht_status_error");
    }
    return false;
  }

  shtState.statusRegister = status;
  shtState.statusOk = true;
  shtState.consecutiveStatusErrors = 0;
  if (!wasOk && shtState.sampleCount > 1) {
    recordTestStep("sht_status_recovered");
  }
  return true;
}

void readMeasurementAndStatus(uint32_t nowMs, bool force) {
  if (!force && (nowMs - lastSampleMs) < SHT_SAMPLE_INTERVAL_MS) {
    return;
  }
  if (!shtState.measurementRunning) {
    lastSampleMs = nowMs;
    return;
  }

  lastSampleMs = nowMs;
  shtState.sampleCount++;
  shtState.alertLineLow = digitalRead(PIN_SHT_ALERT) == LOW;

  if (shtAlertPending) {
    noInterrupts();
    shtAlertPending = false;
    interrupts();
    shtState.alertInterruptSeen = true;
    recordTestStep("sht_alert_interrupt_seen");
  }

  const bool measurementWasOk = shtState.measurementOk;
  float temperature = NAN;
  float humidity = NAN;
  const int16_t measurementError = shtSensor.blockingReadMeasurement(temperature, humidity);
  shtState.lastMeasurementError = measurementError;
  if (measurementError == NO_ERROR) {
    shtState.temperature = temperature;
    shtState.humidity = humidity;
    shtState.measurementOk = true;
    shtState.consecutiveMeasurementErrors = 0;
    if (!measurementWasOk && shtState.sampleCount > 1) {
      recordTestStep("sht_measurement_recovered");
    }
  } else {
    shtState.measurementOk = false;
    shtState.measurementErrors++;
    if (shtState.consecutiveMeasurementErrors < UINT16_MAX) {
      shtState.consecutiveMeasurementErrors++;
    }
    setLastError("measurement_read", measurementError);
    printError(F("[SHT] blockingReadMeasurement"), measurementError);
    if (measurementWasOk) {
      recordTestStep("sht_measurement_error");
    }
  }

  delay(SHT_COMMAND_GUARD_MS);
  const bool statusOk = readShtStatus();
  if (shtState.measurementOk && statusOk) {
    shtState.consecutiveSampleFailures = 0;
    if (shtState.healthySampleStreak < UINT16_MAX) {
      shtState.healthySampleStreak++;
    }
  } else {
    shtState.healthySampleStreak = 0;
    if (shtState.consecutiveSampleFailures < UINT16_MAX) {
      shtState.consecutiveSampleFailures++;
    }
    if (shtState.consecutiveSampleFailures > shtState.maxConsecutiveSampleFailures) {
      shtState.maxConsecutiveSampleFailures = shtState.consecutiveSampleFailures;
    }
    if (shtState.consecutiveSampleFailures >= SHT_RECOVERY_ERROR_THRESHOLD) {
      scheduleRecovery(nowMs);
    }
  }
}

void serviceLimitReads(uint32_t nowMs) {
  if (!shtState.initialized || !shtState.measurementRunning || roundTripInProgress ||
      shtState.recoveryVerificationPending ||
      (nowMs - lastLimitReadMs) < SHT_LIMIT_INTERVAL_MS) {
    return;
  }
  lastLimitReadMs = nowMs;
  const bool wasOk = shtState.limitsOk;
  const bool ok = readAllLimits(true);
  if (!ok) {
    if (wasOk) {
      recordTestStep("sht_limit_read_error");
    }
    if (shtState.consecutiveLimitErrors >= SHT_RECOVERY_ERROR_THRESHOLD) {
      scheduleRecovery(nowMs);
    }
  } else if (!wasOk) {
    recordTestStep("sht_limits_recovered");
  }
}

bool limitRawValuesMatch(const LimitValue& expected, const LimitValue& actual) {
  return expected.ok && actual.ok && expected.raw == actual.raw;
}

void finishRoundTripFailure(const char* result, bool recover = true) {
  shtState.roundTripFailures++;
  strncpy(shtState.roundTripResult, result, sizeof(shtState.roundTripResult) - 1);
  shtState.roundTripResult[sizeof(shtState.roundTripResult) - 1] = '\0';
  recordTestStep(result);
  if (recover) {
    scheduleRecovery(millis());
  }
}

void runUnchangedLimitRoundTrip() {
  roundTripInProgress = true;
  roundTripMeasurementPause = false;
  shtState.roundTripAttempts++;
  recordTestStep("round_trip_started");

  if (!shtState.primaryPresent || !shtState.initialized || !shtState.measurementRunning ||
      !shtState.measurementOk || !shtState.statusOk ||
      shtState.healthySampleStreak < SHT_REQUIRED_HEALTHY_SAMPLES) {
    const bool sensorFault = !shtState.primaryPresent || !shtState.initialized ||
        !shtState.measurementRunning || !shtState.measurementOk || !shtState.statusOk ||
        shtState.recoveryPending;
    finishRoundTripFailure("round_trip_precondition_failed", sensorFault);
    roundTripInProgress = false;
    return;
  }

  recordTestStep("round_trip_stop_periodic");
  const int16_t stopError = shtSensor.stopMeasurement();
  if (stopError != NO_ERROR) {
    setLastError("round_trip_stop", stopError);
    finishRoundTripFailure("round_trip_stop_failed");
    roundTripInProgress = false;
    return;
  }
  shtState.measurementRunning = false;
  roundTripMeasurementPause = true;
  delay(SHT_COMMAND_GUARD_MS);

  bool failureRecorded = false;
  LimitValue expectedHighSet;
  LimitValue expectedHighClear;
  LimitValue expectedLowSet;
  LimitValue expectedLowClear;
  LimitValue actualHighSet;
  LimitValue actualHighClear;
  LimitValue actualLowSet;
  LimitValue actualLowClear;
  bool writeOk = false;
  bool readbackOk = false;

  recordTestStep("round_trip_capture_limits");
  if (!readAllLimits(true)) {
    finishRoundTripFailure("round_trip_capture_failed");
    failureRecorded = true;
  } else {
    expectedHighSet = shtState.highSet;
    expectedHighClear = shtState.highClear;
    expectedLowSet = shtState.lowSet;
    expectedLowClear = shtState.lowClear;

    writeOk = writeLimitRaw(SHT31_ALERT_WHS, expectedHighSet.raw, "write_high_set");
    if (writeOk) writeOk = writeLimitRaw(SHT31_ALERT_WHC, expectedHighClear.raw, "write_high_clear");
    if (writeOk) writeOk = writeLimitRaw(SHT31_ALERT_WLS, expectedLowSet.raw, "write_low_set");
    if (writeOk) writeOk = writeLimitRaw(SHT31_ALERT_WLC, expectedLowClear.raw, "write_low_clear");
    if (!writeOk) {
      finishRoundTripFailure("round_trip_write_failed");
      failureRecorded = true;
    } else {
      recordTestStep("round_trip_write_complete");
      readbackOk = readLimit(SHT31_ALERT_RHS, actualHighSet, "verify_high_set");
      if (readbackOk) readbackOk = readLimit(SHT31_ALERT_RHC, actualHighClear, "verify_high_clear");
      if (readbackOk) readbackOk = readLimit(SHT31_ALERT_RLS, actualLowSet, "verify_low_set");
      if (readbackOk) readbackOk = readLimit(SHT31_ALERT_RLC, actualLowClear, "verify_low_clear");
      readbackOk = readbackOk &&
          limitRawValuesMatch(expectedHighSet, actualHighSet) &&
          limitRawValuesMatch(expectedHighClear, actualHighClear) &&
          limitRawValuesMatch(expectedLowSet, actualLowSet) &&
          limitRawValuesMatch(expectedLowClear, actualLowClear);
      if (!readbackOk) {
        shtState.limitsOk = false;
        finishRoundTripFailure("round_trip_readback_failed");
        failureRecorded = true;
      } else {
        recordTestStep("round_trip_readback_verified");
      }
    }
  }

  const int16_t startError = shtSensor.startPeriodicMeasurement(REPEATABILITY_MEDIUM, MPS_ONE_PER_SECOND);
  shtState.measurementRunning = startError == NO_ERROR;
  shtState.initialized = shtState.primaryPresent && shtState.measurementRunning;
  if (shtState.measurementRunning) {
    lastSampleMs = millis();
  }
  roundTripMeasurementPause = false;
  if (startError != NO_ERROR) {
    setLastError("round_trip_restart", startError);
    if (!failureRecorded) {
      finishRoundTripFailure("round_trip_restart_failed");
      failureRecorded = true;
    } else {
      recordTestStep("round_trip_restart_failed");
    }
  } else {
    recordTestStep("round_trip_periodic_restarted");
  }

  if (!failureRecorded && writeOk && readbackOk && shtState.measurementRunning) {
    shtState.highSet = actualHighSet;
    shtState.highClear = actualHighClear;
    shtState.lowSet = actualLowSet;
    shtState.lowClear = actualLowClear;
    shtState.limitsOk = true;
    shtState.consecutiveLimitErrors = 0;
    shtState.roundTripSuccesses++;
    strncpy(shtState.roundTripResult, "verified: unchanged raw limits", sizeof(shtState.roundTripResult) - 1);
    shtState.roundTripResult[sizeof(shtState.roundTripResult) - 1] = '\0';
    recordTestStep("round_trip_passed");
  }
  roundTripInProgress = false;
}

void serviceRoundTrip() {
  if (!roundTripRequested || roundTripInProgress) {
    return;
  }
  roundTripRequested = false;
  runUnchangedLimitRoundTrip();
}

void serviceRecoveryVerification(uint32_t nowMs) {
  if (!shtState.recoveryVerificationPending || !shtState.measurementOk || !shtState.statusOk) {
    return;
  }
  if (readAllLimits(true)) {
    shtState.recoveryVerificationPending = false;
    shtState.recoveryPending = false;
    shtState.recoveries++;
    shtState.consecutiveSampleFailures = 0;
    shtState.consecutiveLimitErrors = 0;
    strncpy(shtState.lastError, "none", sizeof(shtState.lastError) - 1);
    shtState.lastError[sizeof(shtState.lastError) - 1] = '\0';
    recordTestStep("sht_recovery_verified");
    return;
  }

  shtState.recoveryVerificationPending = false;
  shtState.recoveryFailures++;
  shtState.nextRecoveryMs = nowMs + SHT_RECOVERY_BACKOFF_MS;
  recordTestStep("sht_recovery_verification_failed");
}

void serviceShtRecovery(uint32_t nowMs) {
  if (!shtState.recoveryPending || roundTripInProgress ||
      static_cast<int32_t>(nowMs - shtState.nextRecoveryMs) < 0) {
    return;
  }

  if (shtState.recoveryVerificationPending) {
    shtState.recoveryVerificationPending = false;
    shtState.recoveryFailures++;
    recordTestStep("sht_recovery_verification_timeout");
  }

  roundTripMeasurementPause = false;
  shtState.recoveryAttempts++;
  recordTestStep("sht_recovery_started");
  shtState.primaryPresent = probeI2cAddress(I2C_ADDRESS_SHT31, "recovery_probe", true);
  shtState.address45Present = shtState.primaryPresent;
  if (!shtState.primaryPresent) {
    shtState.lastRecoveryError = -1;
  } else {
    const int16_t stopError = shtSensor.stopMeasurement();
    delay(1);
    const int16_t resetError = shtSensor.softReset();
    delay(10);
    const int16_t startError = resetError == NO_ERROR
        ? shtSensor.startPeriodicMeasurement(REPEATABILITY_MEDIUM, MPS_ONE_PER_SECOND)
        : resetError;
    shtState.lastRecoveryError = stopError != NO_ERROR ? stopError : (resetError != NO_ERROR ? resetError : startError);
    shtState.measurementRunning = resetError == NO_ERROR && startError == NO_ERROR;
    shtState.initialized = shtState.measurementRunning;
  }

  if (shtState.initialized) {
    lastSampleMs = millis();
    shtState.measurementOk = false;
    shtState.statusOk = false;
    shtState.recoveryVerificationPending = true;
    shtState.nextRecoveryMs = millis() + SHT_RECOVERY_BACKOFF_MS;
    recordTestStep("sht_recovery_restart_complete");
  } else {
    shtState.recoveryVerificationPending = false;
    shtState.recoveryFailures++;
    shtState.nextRecoveryMs = millis() + SHT_RECOVERY_BACKOFF_MS;
    setLastError("recovery_failed", shtState.lastRecoveryError);
    recordTestStep("sht_recovery_failed");
  }
}

bool shtHasFault() {
  const bool measurementStoppedUnexpectedly = !shtState.measurementRunning && !roundTripMeasurementPause;
  return !shtState.alertInterruptAttached || !shtState.primaryPresent || !shtState.initialized ||
      measurementStoppedUnexpectedly || !shtState.measurementOk || !shtState.statusOk ||
      !shtState.limitsOk || shtState.recoveryPending;
}

void onRunLimitRoundTripCommand(HAButton*) {
  if (roundTripRequested || roundTripInProgress) {
    recordTestStep("round_trip_request_ignored_busy");
    return;
  }
  roundTripRequested = true;
  recordTestStep("round_trip_requested");
}

bool statusBit(uint8_t bit) {
  return (shtState.statusRegister & (1U << bit)) != 0;
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

  if (serialAvailable()) {
    Serial.print(F("[OTA] Enabled as "));
    Serial.println(OTA_NAME);
  }
}

void configureHomeAssistant() {
  device.setName(DEVICE_NAME);
  device.setManufacturer("Smaeenhouse");
  device.setModel("Arduino Nano 33 IoT SHT Hardware Baseline Test");
  device.setSoftwareVersion(SKETCH_VERSION);
  device.enableExtendedUniqueIds();
  device.enableSharedAvailability();
  device.enableLastWill();
  mqtt.setDiscoveryPrefix(MQTT_DISCOVERY_PREFIX);
  mqtt.setDataPrefix(MQTT_DATA_PREFIX);
  mqtt.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

  uptimeSensor.setName("Persistence RTC Uptime");
  uptimeSensor.setDeviceClass("duration");
  uptimeSensor.setStateClass("total_increasing");
  uptimeSensor.setUnitOfMeasurement("s");
  uptimeSensor.setExpireAfter(90);
  wifiJoinsSensor.setName("Persistence RTC WiFi Joins");
  wifiTimeoutsSensor.setName("Persistence RTC WiFi Timeouts");
  wifiModuleResetsSensor.setName("Persistence RTC WiFi Module Resets");
  otaGapSensor.setName("Persistence RTC OTA Gap Violations");
  wifiJoinsSensor.setStateClass("total_increasing");
  wifiTimeoutsSensor.setStateClass("total_increasing");
  wifiModuleResetsSensor.setStateClass("total_increasing");
  otaGapSensor.setStateClass("total_increasing");

  temperatureSensor.setName("Temperature");
  temperatureSensor.setUnitOfMeasurement("C");
  temperatureSensor.setStateClass("measurement");
  humiditySensor.setName("Humidity");
  humiditySensor.setUnitOfMeasurement("%");
  humiditySensor.setStateClass("measurement");

  measurementErrorsSensor.setName("SHT Measurement Errors");
  statusErrorsSensor.setName("SHT Status Errors");
  limitErrorsSensor.setName("SHT Limit Errors");
  addressProbeFailuresSensor.setName("SHT Address Probe Failures");
  recoveryAttemptsSensor.setName("SHT Recovery Attempts");
  recoveriesSensor.setName("SHT Recoveries");
  recoveryFailuresSensor.setName("SHT Recovery Failures");
  roundTripAttemptsSensor.setName("SHT Round Trip Attempts");
  roundTripSuccessesSensor.setName("SHT Round Trip Successes");
  roundTripFailuresSensor.setName("SHT Round Trip Failures");
  measurementErrorsSensor.setStateClass("total_increasing");
  statusErrorsSensor.setStateClass("total_increasing");
  limitErrorsSensor.setStateClass("total_increasing");
  addressProbeFailuresSensor.setStateClass("total_increasing");
  recoveryAttemptsSensor.setStateClass("total_increasing");
  recoveriesSensor.setStateClass("total_increasing");
  recoveryFailuresSensor.setStateClass("total_increasing");
  roundTripAttemptsSensor.setStateClass("total_increasing");
  roundTripSuccessesSensor.setStateClass("total_increasing");
  roundTripFailuresSensor.setStateClass("total_increasing");
  consecutiveFailuresSensor.setName("SHT Consecutive Failures");
  maxConsecutiveFailuresSensor.setName("SHT Max Consecutive Failures");
  testStepIndexSensor.setName("SHT Test Step Index");
  testStepIndexSensor.setStateClass("total_increasing");
  statusRegisterSensor.setName("SHT Status Register");
  highSetRawSensor.setName("SHT Limit High Set Raw");
  highClearRawSensor.setName("SHT Limit High Clear Raw");
  lowSetRawSensor.setName("SHT Limit Low Set Raw");
  lowClearRawSensor.setName("SHT Limit Low Clear Raw");

  sketchIdentitySensor.setName("Sketch Identity");
  testStepSensor.setName("SHT Test Step");
  lastErrorSensor.setName("SHT Last Error");
  roundTripResultSensor.setName("SHT Round Trip Result");
  statusFlagsSensor.setName("SHT Status Flags");
  shtFaultSensor.setName("SHT Fault");
  address45Sensor.setName("SHT Address 0x45");
  measurementOkSensor.setName("SHT Measurement OK");
  statusOkSensor.setName("SHT Status OK");
  limitsOkSensor.setName("SHT Limits OK");
  alertLineSensor.setName("SHT Alert Line");
  interruptAttachedSensor.setName("SHT Interrupt Attached");
  fanSafeSensor.setName("Fan Output Off");
  relaySafeSensor.setName("Light Relay Open");
  shdnSafeSensor.setName("Light SHDN Asserted");
  roundTripButton.setName("Run SHT Limit Round Trip");
  roundTripButton.onCommand(onRunLimitRoundTripCommand);
}

void publishHaBootIdentity() {
  if (!mqtt.isConnected() || identityPublished) return;
  char identity[64];
  snprintf(identity, sizeof(identity), "%s v%s", SKETCH_NAME, SKETCH_VERSION);
  identityPublished = sketchIdentitySensor.setValue(identity);
}

void cleanupRetiredTopics() {
  if (!mqtt.isConnected() || retiredIdentityTopicsCleared) return;
  bool cleanupOk = mqtt.publish("smaeenhouse/test/sht_hardware_baseline/status", "", true);
  for (uint8_t index = 0; index < sizeof(RETIRED_IDENTITY_ENTITY_IDS) / sizeof(RETIRED_IDENTITY_ENTITY_IDS[0]); ++index) {
    char topic[192];
    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", MQTT_DISCOVERY_PREFIX, DEVICE_ID, RETIRED_IDENTITY_ENTITY_IDS[index]);
    cleanupOk = mqtt.publish(topic, "", true) && cleanupOk;
    snprintf(topic, sizeof(topic), "%s/%s/%s/stat_t", MQTT_DATA_PREFIX, DEVICE_ID, RETIRED_IDENTITY_ENTITY_IDS[index]);
    cleanupOk = mqtt.publish(topic, "", true) && cleanupOk;
  }
  retiredIdentityTopicsCleared = cleanupOk;
}

void publishHaState(uint32_t nowMs, bool force) {
  if (!mqtt.isConnected()) return;
  if (!force && (nowMs - lastHaPublishMs) < HA_PUBLISH_INTERVAL_MS) return;
  lastHaPublishMs = nowMs;

  uptimeSensor.setValue(static_cast<uint32_t>(nowMs / 1000UL), force);
  wifiJoinsSensor.setValue(wifiConnectionCount, force);
  wifiTimeoutsSensor.setValue(wifiConnectTimeoutCount, force);
  wifiModuleResetsSensor.setValue(wifiModuleResetCount, force);
  otaGapSensor.setValue(otaPollGapViolations, force);
  if (shtState.measurementOk) {
    temperatureSensor.setValue(shtState.temperature, force);
    humiditySensor.setValue(shtState.humidity, force);
  }
  measurementErrorsSensor.setValue(shtState.measurementErrors, force);
  statusErrorsSensor.setValue(shtState.statusErrors, force);
  limitErrorsSensor.setValue(shtState.limitErrors, force);
  addressProbeFailuresSensor.setValue(shtState.addressProbeFailures, force);
  consecutiveFailuresSensor.setValue(shtState.consecutiveSampleFailures, force);
  maxConsecutiveFailuresSensor.setValue(shtState.maxConsecutiveSampleFailures, force);
  recoveryAttemptsSensor.setValue(shtState.recoveryAttempts, force);
  recoveriesSensor.setValue(shtState.recoveries, force);
  recoveryFailuresSensor.setValue(shtState.recoveryFailures, force);
  roundTripAttemptsSensor.setValue(shtState.roundTripAttempts, force);
  roundTripSuccessesSensor.setValue(shtState.roundTripSuccesses, force);
  roundTripFailuresSensor.setValue(shtState.roundTripFailures, force);
  statusRegisterSensor.setValue(shtState.statusRegister, force);
  if (shtState.highSet.ok) highSetRawSensor.setValue(shtState.highSet.raw, force);
  if (shtState.highClear.ok) highClearRawSensor.setValue(shtState.highClear.raw, force);
  if (shtState.lowSet.ok) lowSetRawSensor.setValue(shtState.lowSet.raw, force);
  if (shtState.lowClear.ok) lowClearRawSensor.setValue(shtState.lowClear.raw, force);
  lastErrorSensor.setValue(shtState.lastError);
  roundTripResultSensor.setValue(shtState.roundTripResult);
  char statusFlags[112];
  snprintf(statusFlags,
           sizeof(statusFlags),
           "alert=%u rh=%u temp=%u reset=%u cmd=%u crc=%u",
           statusBit(15), statusBit(11), statusBit(10),
           statusBit(4), statusBit(1), statusBit(0));
  statusFlagsSensor.setValue(statusFlags);

  shtFaultSensor.setState(shtHasFault(), force);
  address45Sensor.setState(shtState.address45Present, force);
  measurementOkSensor.setState(shtState.measurementOk, force);
  statusOkSensor.setState(shtState.statusOk, force);
  limitsOkSensor.setState(shtState.limitsOk, force);
  alertLineSensor.setState(shtState.alertLineLow, force);
  interruptAttachedSensor.setState(shtState.alertInterruptAttached, force);
  fanSafeSensor.setState(digitalRead(PIN_FAN_SWITCH) == FAN_OFF_LEVEL, force);
  relaySafeSensor.setState(digitalRead(PIN_LIGHT_POWER_RELAY) == LIGHT_RELAY_OPEN_LEVEL, force);
  shdnSafeSensor.setState(digitalRead(PIN_LIGHT_DIM_SHDN) == LIGHT_DIM_SHDN_ASSERTED_LEVEL, force);
}

void flushPendingSteps() {
  if (!mqtt.isConnected()) return;
  while (pendingStepCount > 0) {
    const PendingStep& step = pendingSteps[pendingStepHead];
    testStepIndexSensor.setValue(step.index, true);
    publishHaState(millis(), true);
    if (!testStepSensor.setValue(step.value)) return;
    pendingStepHead = (pendingStepHead + 1U) % STEP_QUEUE_CAPACITY;
    pendingStepCount--;
  }
}

void beginMqttOnce() {
  if (mqttInitialized) return;
  mqtt.begin(MQTT_HOST, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD);
  mqttInitialized = true;
}

void onWifiConnected() {
  printWifiConnected();
  beginOta();
  beginMqttOnce();
}

void onWifiDisconnected() {
  otaInitialized = false;
  if (mqttInitialized) {
    mqtt.disconnect();
  }
  mqttWasConnected = false;

  if (serialAvailable()) {
    Serial.println(F("[WiFi] Connection lost; reconnect scheduled."));
  }
}

void startWifiReconnect(uint32_t nowMs) {
  if (serialAvailable()) {
    Serial.print(F("[WiFi] Connecting to "));
    Serial.println(WIFI_SSID);
  }

  WiFi.disconnect();
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

          if (serialAvailable()) {
            Serial.println(F("[WiFi] Reinitialized NINA after repeated connect timeouts; retry scheduled."));
          }
        } else {
          wifiConnectState = WifiConnectState::Idle;
          nextWifiAttemptMs = nowMs + WIFI_RETRY_INTERVAL_MS;

          if (serialAvailable()) {
            Serial.println(F("[WiFi] Connect timeout; retry scheduled."));
          }
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
  const uint32_t pollGapMs = beforePollMs - lastOtaPollMs;
  if (pollGapMs > OTA_MAX_POLL_GAP_MS) {
    otaPollGapViolations++;
    if (serialAvailable()) {
      Serial.print(F("[OTA] WARNING: poll gap was "));
      Serial.print(pollGapMs);
      Serial.println(F(" ms."));
    }
  }

  ArduinoOTA.poll();
  lastOtaPollMs = millis();
}

void serviceMqtt(uint32_t nowMs) {
  if (!mqttInitialized || WiFi.status() != WL_CONNECTED) {
    return;
  }
  mqtt.loop();
  const bool connected = mqtt.isConnected();
  if (connected && !mqttWasConnected) {
    mqttWasConnected = true;
    cleanupRetiredTopics();
    publishHaBootIdentity();
    publishHaState(nowMs, true);
    recordTestStep("ha_mqtt_connected");
  } else if (!connected && mqttWasConnected) {
    mqttWasConnected = false;
  }
  cleanupRetiredTopics();
  publishHaBootIdentity();
  flushPendingSteps();
  publishHaState(nowMs, false);
}

void printStatus(uint32_t nowMs) {
  if (!serialAvailable() || (nowMs - lastStatusPrintMs) < STATUS_PRINT_INTERVAL_MS) {
    return;
  }

  lastStatusPrintMs = nowMs;

  Serial.print(F("[Status] uptime="));
  Serial.print(nowMs / 1000UL);
  Serial.print(F(" s, wifi="));
  Serial.print(WiFi.status() == WL_CONNECTED ? F("UP") : F("DOWN"));
  Serial.print(F(", wifi_joins="));
  Serial.print(wifiConnectionCount);
  Serial.print(F(", wifi_timeouts="));
  Serial.print(wifiConnectTimeoutCount);
  Serial.print(F(", wifi_module_resets="));
  Serial.print(wifiModuleResetCount);
  Serial.print(F(", ha="));
  Serial.print(mqtt.isConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", ota="));
  Serial.print(otaInitialized ? F("READY") : F("WAITING_FOR_WIFI"));
  Serial.print(F(", addr44="));
  Serial.print(shtState.address44Present ? F("YES") : F("NO"));
  Serial.print(F(", addr45="));
  Serial.print(shtState.address45Present ? F("YES") : F("NO"));
  Serial.print(F(", temp="));
  Serial.print(shtState.temperature, 2);
  Serial.print(F(", hum="));
  Serial.print(shtState.humidity, 1);
  Serial.print(F(", alert_line_low="));
  Serial.print(shtState.alertLineLow ? F("YES") : F("NO"));
  Serial.print(F(", irq_attached="));
  Serial.print(shtState.alertInterruptAttached ? F("YES") : F("NO"));
  Serial.print(F(", irq_seen="));
  Serial.print(shtState.alertInterruptSeen ? F("YES") : F("NO"));
  Serial.print(F(", status=0x"));
  Serial.print(shtState.statusRegister, HEX);
  Serial.print(F(", ota_gap_violations="));
  Serial.println(otaPollGapViolations);
}

}  // namespace

void setup() {
  configurePinsForSafeState();
  enforceSafeOutputs();

  Serial.begin(115200);
  // Native USB Serial is optional. Never wait for a host to open the port.

  Wire.begin();
  recordTestStep("boot_safe_outputs_verified");
  initializeSht();

  configureHomeAssistant();
  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  nextWifiAttemptMs = millis();

  if (serialAvailable()) {
    Serial.println();
    Serial.println(F("Grow Controller SHT Hardware Baseline Test"));
    Serial.println(F("Runtime order: safe outputs -> SHT diagnostics -> WiFi -> OTA -> Home Assistant"));
  }
}

void loop() {
  serviceOta();

  uint32_t nowMs = millis();
  readMeasurementAndStatus(nowMs, false);
  serviceRecoveryVerification(millis());
  serviceLimitReads(nowMs);
  serviceRoundTrip();
  serviceShtRecovery(millis());
  serviceWifi(nowMs);
  serviceOta();

  nowMs = millis();
  serviceMqtt(nowMs);
  serviceOta();

  printStatus(millis());
}
