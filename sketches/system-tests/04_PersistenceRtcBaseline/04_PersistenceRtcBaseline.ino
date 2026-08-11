#define MQTT_SOCKET_TIMEOUT 1

#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <spi_drv.h>
#include <ArduinoOTA.h>
#include <InternalStorage.h>
#include <ArduinoHA.h>
#include <PubSubClient.h>
#include <JC_EEPROM.h>
#include <RTClib.h>

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
#ifndef MQTT_PREFIX
#error "Credentials.h must define MQTT_PREFIX."
#endif
#ifndef OTA_NAME
#error "Credentials.h must define OTA_NAME."
#endif
#ifndef OTA_PASSWORD
#error "Credentials.h must define OTA_PASSWORD."
#endif

namespace {

constexpr char TEST_ID[] = "persistence_rtc_baseline";
constexpr char SKETCH_NAME[] = "04_PersistenceRtcBaseline";
constexpr char SKETCH_VERSION[] = "1.1.0";
constexpr char DEVICE_ID[] = "grow_controller_tests_persistence_rtc";
constexpr char DEVICE_NAME[] = "Grow Controller Tests";
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

constexpr uint8_t I2C_ADDRESS_DS3231 = 0x68;
constexpr uint8_t I2C_ADDRESS_AT24C32 = 0x57;
constexpr uint16_t EEPROM_PAGE_SIZE = 32;
constexpr uint16_t EEPROM_SIZE_BYTES = 4096;
constexpr uint16_t EEPROM_TEST_BASE = 0;
constexpr uint32_t EEPROM_TEST_MAGIC = 0x50525442UL;  // PRTB
constexpr uint16_t EEPROM_TEST_VERSION = 1;

constexpr uint32_t NETWORK_OPERATION_TIMEOUT_MS = 1000UL;
constexpr uint32_t WIFI_SETTLE_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr uint8_t WIFI_TIMEOUTS_BEFORE_MODULE_RESET = 3;
constexpr uint32_t OTA_MAX_POLL_GAP_MS = 2000UL;
constexpr uint32_t HA_PUBLISH_INTERVAL_MS = 30000UL;
constexpr uint32_t RTC_SERVICE_INTERVAL_MS = 1000UL;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 30000UL;
constexpr uint16_t HA_ENTITY_LIMIT = 48;
constexpr uint8_t RETAINED_TOPIC_CLEANUP_COUNT = 6;
constexpr uint8_t STEP_QUEUE_CAPACITY = 16;

const char* const RETIRED_IDENTITY_ENTITY_IDS[] = {"sketch_name", "sketch_version"};

const char* const RETAINED_TOPICS_TO_CLEAR[RETAINED_TOPIC_CLEANUP_COUNT] = {
  "smaeenhouse/test/safe_installed_baseline/status",
  "smaeenhouse/test/i2c_passive_baseline/status",
  "smaeenhouse/test/sht_hardware_baseline/status",
  "smaeenhouse/test/persistence_rtc_baseline/status",
  "smaeenhouse/test/persistence_rtc_baseline/event",
  "smaeenhouse/test/persistence_rtc_baseline/result"
};

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

struct TestRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t sequence;
  uint32_t bootCount;
  uint8_t fanAutoMode;
  uint8_t lightAutoMode;
  uint8_t fallbackMode;
  uint16_t lightOnMinutes;
  uint16_t lightOffMinutes;
  uint16_t dimMinutes;
  float tempHighSet;
  float tempHighClear;
  float humHighSet;
  float humHighClear;
  int16_t soilAir;
  int16_t soilWater;
  int16_t soilDepthMm;
  uint16_t checksum;
};

struct PersistenceState {
  bool present = false;
  bool readOk = false;
  bool writeOk = false;
  bool verifyOk = false;
  bool magicOk = false;
  bool checksumOk = false;
  uint32_t reads = 0;
  uint32_t writes = 0;
  uint32_t skipped = 0;
  uint32_t runtimeAttempts = 0;
  uint32_t runtimeSuccesses = 0;
  uint32_t runtimeFailures = 0;
  uint32_t sequence = 0;
  uint32_t bootCount = 0;
  uint16_t checksum = 0;
  uint8_t lastError = 0;
  uint8_t lastReadError = 0;
  uint8_t lastWriteError = 0;
  uint8_t lastVerifyError = 0;
};

struct PendingStep {
  uint32_t index = 0;
  char value[96] = "";
};

struct RtcState {
  bool present = false;
  bool lostPower = false;
  bool alarm1Configured = false;
  bool alarm2Configured = false;
  uint32_t alarm1Seen = 0;
  uint32_t alarm2Seen = 0;
  uint32_t isrSeen = 0;
  uint32_t clears = 0;
  uint8_t lastError = 0;
  DateTime now;
};

WiFiClient networkClient;
WiFiClient cleanupNetworkClient;
PubSubClient cleanupMqtt(cleanupNetworkClient);
HADevice device(DEVICE_ID);
HAMqtt mqtt(networkClient, device, HA_ENTITY_LIMIT);
JC_EEPROM eeprom(JC_EEPROM::kbits_32, 1, EEPROM_PAGE_SIZE, I2C_ADDRESS_AT24C32);
RTC_DS3231 rtc;

HASensorNumber uptimeSensor("persistence_rtc_uptime_seconds");
HASensorNumber wifiJoinsSensor("persistence_rtc_wifi_joins");
HASensorNumber wifiTimeoutsSensor("persistence_rtc_wifi_timeouts");
HASensorNumber wifiModuleResetsSensor("persistence_rtc_wifi_module_resets");
HASensorNumber otaGapSensor("persistence_rtc_ota_gap_violations");
HASensorNumber eepromReadsSensor("persistence_rtc_eeprom_reads");
HASensorNumber eepromWritesSensor("persistence_rtc_eeprom_writes");
HASensorNumber eepromSkippedSensor("persistence_rtc_eeprom_skipped_writes");
HASensorNumber eepromSequenceSensor("persistence_rtc_eeprom_sequence");
HASensorNumber eepromBootCountSensor("persistence_rtc_eeprom_boot_count");
HASensorNumber eepromChecksumSensor("persistence_rtc_eeprom_checksum");
HASensorNumber persistenceAttemptsSensor("persistence_rtc_runtime_attempts");
HASensorNumber persistenceSuccessesSensor("persistence_rtc_runtime_successes");
HASensorNumber persistenceFailuresSensor("persistence_rtc_runtime_failures");
HASensorNumber testStepIndexSensor("persistence_rtc_test_step_index");
HASensorNumber rtcEpochSensor("persistence_rtc_epoch");
HASensorNumber rtcAlarm1SeenSensor("persistence_rtc_alarm1_seen");
HASensorNumber rtcAlarm2SeenSensor("persistence_rtc_alarm2_seen");
HASensorNumber rtcInterruptSeenSensor("persistence_rtc_alarm_isr_seen");
HASensorNumber rtcAlarmClearsSensor("persistence_rtc_alarm_clears");
HASensor sketchIdentitySensor("sketch_identity");
HASensor persistenceResultSensor("persistence_rtc_result");
HASensor testStepSensor("persistence_rtc_test_step");

HABinarySensor eepromFaultSensor("eeprom_fault");
HABinarySensor eepromReadOkSensor("persistence_rtc_eeprom_read_ok");
HABinarySensor eepromWriteOkSensor("persistence_rtc_eeprom_write_ok");
HABinarySensor eepromVerifyOkSensor("persistence_rtc_eeprom_verify_ok");
HABinarySensor rtcFaultSensor("rtc_fault");
HABinarySensor rtcLostPowerSensor("persistence_rtc_lost_power");
HABinarySensor alarm1ConfiguredSensor("persistence_rtc_alarm1_configured");
HABinarySensor alarm2ConfiguredSensor("persistence_rtc_alarm2_configured");
HABinarySensor fanSafeSensor("persistence_rtc_fan_safe");
HABinarySensor relaySafeSensor("persistence_rtc_relay_safe");
HABinarySensor shdnSafeSensor("persistence_rtc_shdn_safe");
HASwitch fanAutoModeSwitch("fan_auto_mode");
HAButton verifyPersistenceButton("verify_persistence_record");

WifiConnectState wifiConnectState = WifiConnectState::Idle;
bool wifiWasConnected = false;
bool mqttWasConnected = false;
bool mqttInitialized = false;
bool otaInitialized = false;
uint32_t wifiStateStartedMs = 0;
uint32_t nextWifiAttemptMs = 0;
uint32_t wifiConnectionCount = 0;
uint32_t wifiConnectTimeoutCount = 0;
uint32_t wifiModuleResetCount = 0;
uint8_t consecutiveWifiConnectTimeouts = 0;
uint32_t lastOtaPollMs = 0;
uint32_t otaPollGapViolations = 0;
uint32_t lastHaPublishMs = 0;
uint32_t lastRtcServiceMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t safeStateEnforceCount = 0;
bool retainedTopicsCleared = false;
bool identityPublished = false;
PendingStep pendingSteps[STEP_QUEUE_CAPACITY];
uint8_t pendingStepHead = 0;
uint8_t pendingStepCount = 0;
uint32_t testStepIndex = 0;
char persistenceResult[112] = "boot: persistence initialization pending";

PersistenceState persistenceState;
RtcState rtcState;
TestRecord activeRecord;

bool serialAvailable() {
  return static_cast<bool>(Serial);
}

uint16_t checksumRecord(const TestRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  const size_t checksumOffset = offsetof(TestRecord, checksum);
  uint16_t checksum = 0x5A5A;
  for (size_t index = 0; index < checksumOffset; ++index) {
    checksum = static_cast<uint16_t>((checksum << 5) | (checksum >> 11));
    checksum = static_cast<uint16_t>(checksum + bytes[index]);
  }
  return checksum;
}

TestRecord makeDefaultRecord(uint32_t sequence, uint32_t bootCount) {
  TestRecord record{};
  record.magic = EEPROM_TEST_MAGIC;
  record.version = EEPROM_TEST_VERSION;
  record.length = sizeof(TestRecord);
  record.sequence = sequence;
  record.bootCount = bootCount;
  record.fanAutoMode = 1;
  record.lightAutoMode = 1;
  record.fallbackMode = 1;
  record.lightOnMinutes = 8U * 60U;
  record.lightOffMinutes = 20U * 60U;
  record.dimMinutes = 30;
  record.tempHighSet = 30.0f;
  record.tempHighClear = 28.0f;
  record.humHighSet = 85.0f;
  record.humHighClear = 80.0f;
  record.soilAir = 1000;
  record.soilWater = 500;
  record.soilDepthMm = 50;
  record.checksum = checksumRecord(record);
  return record;
}

bool probeI2cAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void configurePinsForSafeState() {
  pinMode(PIN_FAN_SWITCH, OUTPUT);
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);

  pinMode(PIN_LIGHT_POWER_RELAY, OUTPUT);
  digitalWrite(PIN_LIGHT_POWER_RELAY, LIGHT_RELAY_OPEN_LEVEL);

  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);

  pinMode(PIN_SHT_ALERT, INPUT_PULLUP);
  pinMode(PIN_RTC_ALARM, INPUT_PULLUP);
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  pinMode(PIN_SOIL_MOISTURE, INPUT);
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);
}

void enforceSafeOutputs() {
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  digitalWrite(PIN_LIGHT_POWER_RELAY, LIGHT_RELAY_OPEN_LEVEL);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  safeStateEnforceCount++;
}

void recordTestStep(const char* value) {
  testStepIndex++;
  if (pendingStepCount >= STEP_QUEUE_CAPACITY) {
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

void setPersistenceResult(const char* context, const char* result, uint8_t error = 0) {
  snprintf(persistenceResult,
           sizeof(persistenceResult),
           "%s: %s error=%u read=%u write=%u verify=%u",
           context,
           result,
           error,
           persistenceState.readOk ? 1U : 0U,
           persistenceState.writeOk ? 1U : 0U,
           persistenceState.verifyOk ? 1U : 0U);
}

bool recordShapeValid(const TestRecord& record) {
  return record.magic == EEPROM_TEST_MAGIC &&
      record.version == EEPROM_TEST_VERSION &&
      record.length == sizeof(TestRecord);
}

bool recordValid(const TestRecord& record) {
  return recordShapeValid(record) && record.checksum == checksumRecord(record);
}

void acceptVerifiedRecord(const TestRecord& record) {
  activeRecord = record;
  persistenceState.magicOk = true;
  persistenceState.checksumOk = true;
  persistenceState.verifyOk = true;
  persistenceState.sequence = record.sequence;
  persistenceState.bootCount = record.bootCount;
  persistenceState.checksum = record.checksum;
}

bool readRecord(TestRecord& record) {
  persistenceState.reads++;
  const uint8_t result = eeprom.read(EEPROM_TEST_BASE, reinterpret_cast<uint8_t*>(&record), sizeof(record));
  if (result != 0) {
    persistenceState.lastError = result;
    persistenceState.lastReadError = result;
    persistenceState.readOk = false;
    return false;
  }
  persistenceState.readOk = true;
  persistenceState.lastReadError = 0;
  return true;
}

bool writeRecordBytes(const TestRecord& record) {
  for (uint16_t index = 0; index < sizeof(record); ++index) {
    const uint8_t value = reinterpret_cast<const uint8_t*>(&record)[index];
    const uint8_t result = eeprom.update(EEPROM_TEST_BASE + index, value);
    if (result != 0) {
      persistenceState.lastError = result;
      persistenceState.lastWriteError = result;
      persistenceState.writeOk = false;
      return false;
    }
  }

  persistenceState.writes++;
  persistenceState.writeOk = true;
  persistenceState.lastWriteError = 0;
  return true;
}

bool persistAndVerify(const TestRecord& expected, const char* context) {
  char step[64];
  TestRecord current{};
  if (!readRecord(current)) {
    persistenceState.verifyOk = false;
    persistenceState.lastVerifyError = persistenceState.lastReadError;
    snprintf(step, sizeof(step), "%s_pre_read_failed", context);
    recordTestStep(step);
    setPersistenceResult(context, "pre-read failed", persistenceState.lastReadError);
    return false;
  }
  snprintf(step, sizeof(step), "%s_pre_read_ok", context);
  recordTestStep(step);

  if (memcmp(&current, &expected, sizeof(expected)) == 0) {
    persistenceState.skipped++;
    persistenceState.writeOk = true;
    persistenceState.lastWriteError = 0;
    snprintf(step, sizeof(step), "%s_write_skipped_unchanged", context);
    recordTestStep(step);
  } else {
    if (!writeRecordBytes(expected)) {
      persistenceState.verifyOk = false;
      persistenceState.lastVerifyError = persistenceState.lastWriteError;
      snprintf(step, sizeof(step), "%s_write_failed", context);
      recordTestStep(step);
      setPersistenceResult(context, "write failed", persistenceState.lastWriteError);
      return false;
    }
    snprintf(step, sizeof(step), "%s_write_ok", context);
    recordTestStep(step);
  }

  TestRecord verified{};
  if (!readRecord(verified)) {
    persistenceState.verifyOk = false;
    persistenceState.lastVerifyError = persistenceState.lastReadError;
    snprintf(step, sizeof(step), "%s_verify_read_failed", context);
    recordTestStep(step);
    setPersistenceResult(context, "verify read failed", persistenceState.lastVerifyError);
    return false;
  }

  persistenceState.magicOk = recordShapeValid(verified);
  persistenceState.checksumOk = recordValid(verified);
  if (!persistenceState.checksumOk || memcmp(&verified, &expected, sizeof(expected)) != 0) {
    persistenceState.verifyOk = false;
    persistenceState.lastVerifyError = 1;
    persistenceState.lastError = 1;
    snprintf(step, sizeof(step), "%s_verify_mismatch", context);
    recordTestStep(step);
    setPersistenceResult(context, "verify mismatch", persistenceState.lastVerifyError);
    return false;
  }

  persistenceState.lastError = 0;
  persistenceState.lastVerifyError = 0;
  acceptVerifiedRecord(verified);
  snprintf(step, sizeof(step), "%s_verify_ok", context);
  recordTestStep(step);
  setPersistenceResult(context, "verified");
  return true;
}

void initializePersistence() {
  persistenceState.present = (eeprom.begin(JC_EEPROM::twiClock100kHz) == 0) && probeI2cAddress(I2C_ADDRESS_AT24C32);
  if (!persistenceState.present) {
    persistenceState.lastError = 1;
    activeRecord = makeDefaultRecord(1, 1);
    recordTestStep("boot_eeprom_probe_failed");
    setPersistenceResult("boot", "EEPROM probe failed", 1);
    return;
  }
  recordTestStep("boot_eeprom_probe_ok");

  TestRecord stored{};
  if (!readRecord(stored)) {
    activeRecord = makeDefaultRecord(1, 1);
    recordTestStep("boot_record_read_failed");
    setPersistenceResult("boot", "record read failed", persistenceState.lastReadError);
    return;
  }

  if (recordValid(stored)) {
    activeRecord = stored;
    activeRecord.sequence++;
    activeRecord.bootCount++;
    activeRecord.checksum = checksumRecord(activeRecord);
    recordTestStep("boot_record_valid");
  } else {
    activeRecord = makeDefaultRecord(1, 1);
    recordTestStep("boot_record_invalid_defaults_selected");
  }

  persistAndVerify(activeRecord, "boot_record");
}

bool runRuntimePersistenceAttempt(const TestRecord& expected, const char* context) {
  persistenceState.runtimeAttempts++;
  char step[64];
  snprintf(step, sizeof(step), "%s_requested", context);
  recordTestStep(step);

  if (!persistenceState.present) {
    persistenceState.readOk = false;
    persistenceState.writeOk = false;
    persistenceState.verifyOk = false;
    persistenceState.lastError = 1;
    persistenceState.runtimeFailures++;
    snprintf(step, sizeof(step), "%s_failed_no_eeprom", context);
    recordTestStep(step);
    setPersistenceResult(context, "EEPROM unavailable", 1);
    return false;
  }

  if (!persistAndVerify(expected, context)) {
    persistenceState.runtimeFailures++;
    snprintf(step, sizeof(step), "%s_failed", context);
    recordTestStep(step);
    return false;
  }

  persistenceState.runtimeSuccesses++;
  snprintf(step, sizeof(step), "%s_passed", context);
  recordTestStep(step);
  return true;
}

void onFanAutoModeCommand(bool state, HASwitch*) {
  TestRecord updated = activeRecord;
  if (updated.fanAutoMode != static_cast<uint8_t>(state)) {
    updated.sequence++;
    updated.fanAutoMode = state ? 1U : 0U;
    updated.checksum = checksumRecord(updated);
  }

  const char* context = state ? "fan_auto_on" : "fan_auto_off";
  runRuntimePersistenceAttempt(updated, context);
  fanAutoModeSwitch.setState(activeRecord.fanAutoMode != 0U, true);
}

void onVerifyPersistenceCommand(HAButton*) {
  runRuntimePersistenceAttempt(activeRecord, "record_verify");
  fanAutoModeSwitch.setState(activeRecord.fanAutoMode != 0U, true);
}

bool eepromHasFault() {
  return !(persistenceState.present &&
      persistenceState.readOk &&
      persistenceState.writeOk &&
      persistenceState.verifyOk &&
      persistenceState.magicOk &&
      persistenceState.checksumOk);
}

void configureRtcAlarms() {
  if (!rtcState.present) {
    return;
  }

  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.disableAlarm(1);
  rtc.disableAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);

  rtcState.now = rtc.now();
  rtcState.lostPower = rtc.lostPower();

  const DateTime alarm1Time = rtcState.now + TimeSpan(0, 0, 1, 0);
  const DateTime alarm2Time = rtcState.now + TimeSpan(0, 0, 2, 0);
  rtcState.alarm1Configured = rtc.setAlarm1(alarm1Time, DS3231_A1_Date);
  rtcState.alarm2Configured = rtc.setAlarm2(alarm2Time, DS3231_A2_Date);
  rtcState.lastError = (rtcState.alarm1Configured && rtcState.alarm2Configured) ? 0 : 2;
}

void initializeRtc() {
  rtcState.present = rtc.begin() && probeI2cAddress(I2C_ADDRESS_DS3231);
  if (!rtcState.present) {
    rtcState.lastError = 1;
    return;
  }

  configureRtcAlarms();
}

void serviceRtc(uint32_t nowMs) {
  if (!rtcState.present || (nowMs - lastRtcServiceMs) < RTC_SERVICE_INTERVAL_MS) {
    return;
  }
  lastRtcServiceMs = nowMs;

  rtcState.now = rtc.now();
  rtcState.lostPower = rtc.lostPower();

  noInterrupts();
  const bool alarmIsrPending = rtcAlarmPending;
  rtcAlarmPending = false;
  interrupts();

  if (alarmIsrPending) {
    rtcState.isrSeen++;
  }

  const bool alarm1Fired = rtc.alarmFired(1);
  const bool alarm2Fired = rtc.alarmFired(2);

  if (alarm1Fired) {
    rtcState.alarm1Seen++;
    rtc.clearAlarm(1);
    rtcState.clears++;
  }

  if (alarm2Fired) {
    rtcState.alarm2Seen++;
    rtc.clearAlarm(2);
    rtcState.clears++;
  }
}

void clearKnownRetainedTopics() {
  if (retainedTopicsCleared || WiFi.status() != WL_CONNECTED) {
    return;
  }

  cleanupMqtt.setServer(MQTT_HOST, MQTT_PORT);
  cleanupMqtt.setSocketTimeout(NETWORK_OPERATION_TIMEOUT_MS / 1000UL);
  if (!cleanupMqtt.connect("persistence_rtc_cleanup", MQTT_USERNAME, MQTT_PASSWORD)) {
    if (serialAvailable()) {
      Serial.print(F("[MQTT] Retained-topic cleanup connect failed, state="));
      Serial.println(cleanupMqtt.state());
    }
    return;
  }

  bool cleanupOk = true;
  for (uint8_t index = 0; index < RETAINED_TOPIC_CLEANUP_COUNT; ++index) {
    cleanupOk = cleanupMqtt.publish(RETAINED_TOPICS_TO_CLEAR[index], "", true) && cleanupOk;
  }
  for (uint8_t index = 0;
       index < sizeof(RETIRED_IDENTITY_ENTITY_IDS) / sizeof(RETIRED_IDENTITY_ENTITY_IDS[0]);
       ++index) {
    char topic[192];
    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", MQTT_PREFIX, DEVICE_ID, RETIRED_IDENTITY_ENTITY_IDS[index]);
    cleanupOk = cleanupMqtt.publish(topic, "", true) && cleanupOk;
    snprintf(topic, sizeof(topic), "%s/%s/%s/stat_t", MQTT_DATA_PREFIX, DEVICE_ID, RETIRED_IDENTITY_ENTITY_IDS[index]);
    cleanupOk = cleanupMqtt.publish(topic, "", true) && cleanupOk;
  }
  cleanupMqtt.disconnect();
  cleanupNetworkClient.stop();
  retainedTopicsCleared = cleanupOk;

  if (serialAvailable()) {
    Serial.println(F("[MQTT] Cleared known retained direct-test topics from previous revisions."));
  }
}

void configureHomeAssistant() {
  device.setName(DEVICE_NAME);
  device.setManufacturer("Smaeenhouse");
  device.setModel("Arduino Nano 33 IoT Persistence RTC Baseline Test");
  device.setSoftwareVersion(SKETCH_VERSION);
  device.enableExtendedUniqueIds();
  device.enableSharedAvailability();
  device.enableLastWill();

  mqtt.setDiscoveryPrefix(MQTT_PREFIX);
  mqtt.setDataPrefix(MQTT_DATA_PREFIX);

  uptimeSensor.setName("Persistence RTC Uptime");
  uptimeSensor.setDeviceClass("duration");
  uptimeSensor.setStateClass("total_increasing");
  uptimeSensor.setUnitOfMeasurement("s");
  uptimeSensor.setExpireAfter(90);

  wifiJoinsSensor.setName("Persistence RTC WiFi Joins");
  wifiJoinsSensor.setStateClass("total_increasing");
  wifiTimeoutsSensor.setName("Persistence RTC WiFi Timeouts");
  wifiTimeoutsSensor.setStateClass("total_increasing");
  wifiModuleResetsSensor.setName("Persistence RTC WiFi Module Resets");
  wifiModuleResetsSensor.setStateClass("total_increasing");
  otaGapSensor.setName("Persistence RTC OTA Gap Violations");
  otaGapSensor.setStateClass("total_increasing");

  eepromReadsSensor.setName("Persistence RTC EEPROM Reads");
  eepromReadsSensor.setStateClass("total_increasing");
  eepromWritesSensor.setName("Persistence RTC EEPROM Writes");
  eepromWritesSensor.setStateClass("total_increasing");
  eepromSkippedSensor.setName("Persistence RTC EEPROM Skipped Writes");
  eepromSkippedSensor.setStateClass("total_increasing");
  eepromSequenceSensor.setName("Persistence RTC EEPROM Sequence");
  eepromBootCountSensor.setName("Persistence RTC EEPROM Boot Count");
  eepromChecksumSensor.setName("Persistence RTC EEPROM Checksum");
  persistenceAttemptsSensor.setName("Persistence RTC Runtime Attempts");
  persistenceAttemptsSensor.setStateClass("total_increasing");
  persistenceSuccessesSensor.setName("Persistence RTC Runtime Successes");
  persistenceSuccessesSensor.setStateClass("total_increasing");
  persistenceFailuresSensor.setName("Persistence RTC Runtime Failures");
  persistenceFailuresSensor.setStateClass("total_increasing");
  testStepIndexSensor.setName("Persistence RTC Test Step Index");
  testStepIndexSensor.setStateClass("total_increasing");
  sketchIdentitySensor.setName("Sketch Identity");
  persistenceResultSensor.setName("Persistence RTC Result");
  testStepSensor.setName("Persistence RTC Test Step");

  rtcEpochSensor.setName("Persistence RTC Epoch");
  rtcAlarm1SeenSensor.setName("Persistence RTC Alarm1 Seen");
  rtcAlarm1SeenSensor.setStateClass("total_increasing");
  rtcAlarm2SeenSensor.setName("Persistence RTC Alarm2 Seen");
  rtcAlarm2SeenSensor.setStateClass("total_increasing");
  rtcInterruptSeenSensor.setName("Persistence RTC Alarm ISR Seen");
  rtcInterruptSeenSensor.setStateClass("total_increasing");
  rtcAlarmClearsSensor.setName("Persistence RTC Alarm Clears");
  rtcAlarmClearsSensor.setStateClass("total_increasing");

  eepromFaultSensor.setName("EEPROM Fault");
  eepromReadOkSensor.setName("Persistence RTC EEPROM Read OK");
  eepromWriteOkSensor.setName("Persistence RTC EEPROM Write OK");
  eepromVerifyOkSensor.setName("Persistence RTC EEPROM Verify OK");
  rtcFaultSensor.setName("RTC Fault");
  rtcLostPowerSensor.setName("Persistence RTC Lost Power");
  alarm1ConfiguredSensor.setName("Persistence RTC Alarm1 Configured");
  alarm2ConfiguredSensor.setName("Persistence RTC Alarm2 Configured");
  fanSafeSensor.setName("Persistence RTC Fan Safe");
  relaySafeSensor.setName("Persistence RTC Relay Safe");
  shdnSafeSensor.setName("Persistence RTC SHDN Safe");
  fanAutoModeSwitch.setName("Fan Auto Mode");
  fanAutoModeSwitch.onCommand(onFanAutoModeCommand);
  verifyPersistenceButton.setName("Verify Persistence Record");
  verifyPersistenceButton.onCommand(onVerifyPersistenceCommand);
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

void beginMqttOnce() {
  if (mqttInitialized) {
    return;
  }
  mqtt.begin(MQTT_HOST, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD);
  mqttInitialized = true;

  if (serialAvailable()) {
    Serial.print(F("[MQTT] Configured broker "));
    Serial.print(MQTT_HOST);
    Serial.print(':');
    Serial.println(MQTT_PORT);
  }
}

void publishHaBootIdentity() {
  if (!mqtt.isConnected() || identityPublished) {
    return;
  }
  char identity[64];
  snprintf(identity, sizeof(identity), "%s v%s", SKETCH_NAME, SKETCH_VERSION);
  identityPublished = sketchIdentitySensor.setValue(identity);
}

void publishHaState(uint32_t nowMs, bool force) {
  if (!mqtt.isConnected()) {
    return;
  }
  if (!force && (nowMs - lastHaPublishMs) < HA_PUBLISH_INTERVAL_MS) {
    return;
  }
  lastHaPublishMs = nowMs;

  uptimeSensor.setValue(static_cast<uint32_t>(nowMs / 1000UL), true);
  wifiJoinsSensor.setValue(static_cast<uint32_t>(wifiConnectionCount), true);
  wifiTimeoutsSensor.setValue(static_cast<uint32_t>(wifiConnectTimeoutCount), true);
  wifiModuleResetsSensor.setValue(static_cast<uint32_t>(wifiModuleResetCount), true);
  otaGapSensor.setValue(static_cast<uint32_t>(otaPollGapViolations), true);

  eepromReadsSensor.setValue(static_cast<uint32_t>(persistenceState.reads), true);
  eepromWritesSensor.setValue(static_cast<uint32_t>(persistenceState.writes), true);
  eepromSkippedSensor.setValue(static_cast<uint32_t>(persistenceState.skipped), true);
  eepromSequenceSensor.setValue(static_cast<uint32_t>(persistenceState.sequence), true);
  eepromBootCountSensor.setValue(static_cast<uint32_t>(persistenceState.bootCount), true);
  eepromChecksumSensor.setValue(static_cast<uint32_t>(persistenceState.checksum), true);
  persistenceAttemptsSensor.setValue(static_cast<uint32_t>(persistenceState.runtimeAttempts), true);
  persistenceSuccessesSensor.setValue(static_cast<uint32_t>(persistenceState.runtimeSuccesses), true);
  persistenceFailuresSensor.setValue(static_cast<uint32_t>(persistenceState.runtimeFailures), true);
  if (pendingStepCount == 0) {
    testStepIndexSensor.setValue(static_cast<uint32_t>(testStepIndex), true);
  }
  persistenceResultSensor.setValue(persistenceResult);

  rtcEpochSensor.setValue(static_cast<uint32_t>(rtcState.present ? rtcState.now.unixtime() : 0UL), true);
  rtcAlarm1SeenSensor.setValue(static_cast<uint32_t>(rtcState.alarm1Seen), true);
  rtcAlarm2SeenSensor.setValue(static_cast<uint32_t>(rtcState.alarm2Seen), true);
  rtcInterruptSeenSensor.setValue(static_cast<uint32_t>(rtcState.isrSeen), true);
  rtcAlarmClearsSensor.setValue(static_cast<uint32_t>(rtcState.clears), true);

  eepromFaultSensor.setState(eepromHasFault(), true);
  eepromReadOkSensor.setState(persistenceState.readOk, true);
  eepromWriteOkSensor.setState(persistenceState.writeOk, true);
  eepromVerifyOkSensor.setState(persistenceState.verifyOk, true);
  rtcFaultSensor.setState(!(rtcState.present && rtcState.alarm1Configured && rtcState.alarm2Configured), true);
  rtcLostPowerSensor.setState(rtcState.lostPower, true);
  alarm1ConfiguredSensor.setState(rtcState.alarm1Configured, true);
  alarm2ConfiguredSensor.setState(rtcState.alarm2Configured, true);
  fanSafeSensor.setState(digitalRead(PIN_FAN_SWITCH) == FAN_OFF_LEVEL, true);
  relaySafeSensor.setState(digitalRead(PIN_LIGHT_POWER_RELAY) == LIGHT_RELAY_OPEN_LEVEL, true);
  shdnSafeSensor.setState(digitalRead(PIN_LIGHT_DIM_SHDN) == LIGHT_DIM_SHDN_ASSERTED_LEVEL, true);
  fanAutoModeSwitch.setState(activeRecord.fanAutoMode != 0U, true);
  publishHaBootIdentity();

  if (serialAvailable()) {
    Serial.println(F("[MQTT] Published HA persistence/RTC states."));
  }
}

void flushPendingSteps() {
  if (!mqtt.isConnected()) {
    return;
  }
  while (pendingStepCount > 0) {
    const PendingStep& step = pendingSteps[pendingStepHead];
    testStepIndexSensor.setValue(step.index, true);
    if (!testStepSensor.setValue(step.value)) {
      return;
    }
    pendingStepHead = (pendingStepHead + 1U) % STEP_QUEUE_CAPACITY;
    pendingStepCount--;
  }
}

void onWifiConnected() {
  if (serialAvailable()) {
    Serial.print(F("[WiFi] Connected to "));
    Serial.println(WiFi.SSID());
    Serial.print(F("[WiFi] IP: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[WiFi] RSSI: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
  }
  beginOta();
  clearKnownRetainedTopics();
  beginMqttOnce();
}

void onWifiDisconnected() {
  otaInitialized = false;
  if (mqttInitialized && mqtt.isConnected()) {
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
  const bool mqttConnected = mqtt.isConnected();
  if (mqttConnected && !mqttWasConnected) {
    mqttWasConnected = true;
    if (serialAvailable()) {
      Serial.println(F("[MQTT] Connected."));
    }
    publishHaState(nowMs, true);
  } else if (!mqttConnected && mqttWasConnected) {
    mqttWasConnected = false;
    if (serialAvailable()) {
      Serial.print(F("[MQTT] Disconnected, state="));
      Serial.println(static_cast<int>(mqtt.getState()));
    }
  }
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
  Serial.print(F(" s, wifi="));
  Serial.print(WiFi.status() == WL_CONNECTED ? F("UP") : F("DOWN"));
  Serial.print(F(", mqtt="));
  Serial.print(mqtt.isConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", ota="));
  Serial.print(otaInitialized ? F("READY") : F("WAITING_FOR_WIFI"));
  Serial.print(F(", eeprom_present="));
  Serial.print(persistenceState.present ? F("YES") : F("NO"));
  Serial.print(F(", eeprom_seq="));
  Serial.print(persistenceState.sequence);
  Serial.print(F(", eeprom_writes="));
  Serial.print(persistenceState.writes);
  Serial.print(F(", eeprom_skipped="));
  Serial.print(persistenceState.skipped);
  Serial.print(F(", rtc_present="));
  Serial.print(rtcState.present ? F("YES") : F("NO"));
  Serial.print(F(", alarm1_seen="));
  Serial.print(rtcState.alarm1Seen);
  Serial.print(F(", alarm2_seen="));
  Serial.print(rtcState.alarm2Seen);
  Serial.print(F(", rtc_isr="));
  Serial.print(rtcState.isrSeen);
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
  initializePersistence();
  initializeRtc();

  if (pinSupportsExternalInterrupt(PIN_SHT_ALERT)) {
    attachInterrupt(digitalPinToInterrupt(PIN_SHT_ALERT), onShtAlert, FALLING);
  }
  if (pinSupportsExternalInterrupt(PIN_RTC_ALARM)) {
    attachInterrupt(digitalPinToInterrupt(PIN_RTC_ALARM), onRtcAlarm, FALLING);
  }
  if (pinSupportsExternalInterrupt(PIN_FAN_TACH)) {
    attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), onFanTachPulse, FALLING);
  }

  configureHomeAssistant();
  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  nextWifiAttemptMs = millis();

  if (serialAvailable()) {
    Serial.println();
    Serial.println(F("Grow Controller Persistence RTC Baseline Test"));
    Serial.println(F("Runtime order: safe outputs -> EEPROM/RTC init -> WiFi -> OTA -> ArduinoHA telemetry"));
  }
}

void loop() {
  serviceOta();
  uint32_t nowMs = millis();
  serviceRtc(nowMs);
  serviceWifi(nowMs);
  serviceOta();
  nowMs = millis();
  serviceMqtt(nowMs);
  serviceOta();
  printStatus(millis());
}
