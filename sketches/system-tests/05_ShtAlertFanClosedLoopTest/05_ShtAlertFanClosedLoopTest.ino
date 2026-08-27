#define MQTT_SOCKET_TIMEOUT 1

#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <spi_drv.h>
#include <ArduinoOTA.h>
#include <InternalStorage.h>
#include <ArduinoHA.h>
#include <JC_EEPROM.h>
#include <RTClib.h>
#include <SensirionI2cSht3x.h>

#include "Config.h"
#include "Credentials.h"
#include "SystemTestHaCleanup.h"
#include "FanController.h"

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

constexpr char TEST_ID[] = "sht_fan_closed_loop";
constexpr char SKETCH_NAME[] = "05_ShtAlertFanClosedLoopTest";
constexpr char SKETCH_VERSION[] = "1.1.5";
constexpr char DEVICE_ID[] = "grow_controller_tests_persistence_rtc";
constexpr char DEVICE_NAME[] = "Grow Controller Tests";
constexpr char MQTT_DATA_PREFIX[] = "smaeenhouse/test/persistence_rtc_baseline/ha";

constexpr uint8_t FAN_OFF_LEVEL = LOW;
constexpr uint8_t LIGHT_RELAY_OPEN_LEVEL = LOW;
constexpr uint8_t LIGHT_DIM_SHDN_ASSERTED_LEVEL = LOW;

constexpr uint8_t I2C_ADDRESS_SHT31 = SHT30_I2C_ADDR_45;
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
constexpr uint32_t SHT_COMMAND_GUARD_US = 1000UL;
constexpr uint32_t SHT_SAMPLE_INTERVAL_MS = 2000UL;
constexpr uint32_t SHT_STATUS_INTERVAL_MS = 5000UL;
constexpr uint32_t SHT_RECOVERY_BACKOFF_MS = 30000UL;
constexpr uint8_t SHT_RECOVERY_ERROR_THRESHOLD = 3;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 30000UL;
constexpr uint16_t HA_ENTITY_LIMIT = 48;
constexpr uint8_t EVENT_QUEUE_CAPACITY = 24;

struct RetiredEntity {
  const char* domain;
  const char* id;
};

const RetiredEntity RETIRED_ENTITIES[] = {
  {"sensor", "sketch_name"},
  {"sensor", "sketch_version"},
  {"sensor", "persistence_rtc_eeprom_reads"},
  {"sensor", "persistence_rtc_runtime_attempts"},
  {"sensor", "persistence_rtc_runtime_successes"},
  {"sensor", "persistence_rtc_runtime_failures"},
  {"sensor", "persistence_rtc_test_step_index"},
  {"sensor", "persistence_rtc_result"},
  {"sensor", "persistence_rtc_test_step"},
  {"binary_sensor", "persistence_rtc_eeprom_read_ok"},
  {"binary_sensor", "persistence_rtc_eeprom_write_ok"},
  {"binary_sensor", "persistence_rtc_eeprom_verify_ok"},
  {"button", "verify_persistence_record"}
};

const char* const RETIRED_DIRECT_TOPICS[] = {
  "smaeenhouse/test/sht_fan_closed_loop/status",
  "smaeenhouse/test/sht_fan_closed_loop/event",
  "smaeenhouse/test/persistence_rtc_baseline/status",
  "smaeenhouse/test/persistence_rtc_baseline/event",
  "smaeenhouse/test/persistence_rtc_baseline/result"
};

constexpr float TEMP_MIN_C = -40.0f;
constexpr float TEMP_MAX_C = 125.0f;
constexpr float HUM_MIN_PERCENT = 0.0f;
constexpr float HUM_MAX_PERCENT = 100.0f;
constexpr float TEMP_MIN_GAP_C = 0.5f;
constexpr float HUM_MIN_GAP_PERCENT = 1.0f;
constexpr float TEMP_READBACK_TOLERANCE_C = 0.4f;
constexpr float HUM_READBACK_TOLERANCE_PERCENT = 0.8f;

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


volatile bool shtAlertPending = false;
volatile bool rtcAlarmPending = false;

void onShtAlert() {
  shtAlertPending = true;
}

void onRtcAlarm() {
  rtcAlarmPending = true;
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
  uint32_t sequence = 0;
  uint32_t bootCount = 0;
  uint16_t checksum = 0;
  uint8_t lastError = 0;
  uint8_t lastReadError = 0;
  uint8_t lastWriteError = 0;
  uint8_t lastVerifyError = 0;
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

struct ThresholdConfig {
  float tempHighSet = DEFAULT_TEMP_HIGH_SET;
  float tempHighClear = DEFAULT_TEMP_HIGH_CLEAR;
  float tempLowSet = DEFAULT_TEMP_LOW_SET;
  float tempLowClear = DEFAULT_TEMP_LOW_CLEAR;
  float humHighSet = DEFAULT_HUM_HIGH_SET;
  float humHighClear = DEFAULT_HUM_HIGH_CLEAR;
  float humLowSet = DEFAULT_HUM_LOW_SET;
  float humLowClear = DEFAULT_HUM_LOW_CLEAR;
};

struct ShtLimit {
  uint16_t raw = 0;
  float temperature = NAN;
  float humidity = NAN;
  bool ok = false;
};

struct ShtState {
  bool present = false;
  bool initialized = false;
  bool measurementRunning = false;
  bool alertInterruptAttached = false;
  bool measurementOk = false;
  bool statusOk = false;
  bool limitsApplied = false;
  bool limitsVerified = false;
  bool controlledMeasurementPause = false;
  bool transactionInProgress = false;
  bool recoveryPending = false;
  bool alertSummary = false;
  bool tempTrackingAlert = false;
  bool humTrackingAlert = false;
  bool resetDetected = false;
  bool commandError = false;
  bool crcError = false;
  bool alertLineActive = false;
  bool tempHighDemand = false;
  bool humHighDemand = false;
  bool tempLowObserved = false;
  bool humLowObserved = false;
  bool unexplainedSummary = false;
  float temperature = NAN;
  float humidity = NAN;
  uint16_t statusRegister = 0;
  int16_t lastMeasurementError = 0;
  int16_t lastStatusError = 0;
  int16_t lastLimitError = 0;
  int16_t lastRestartError = 0;
  uint16_t consecutiveMeasurementErrors = 0;
  uint16_t consecutiveStatusErrors = 0;
  uint16_t consecutiveLimitErrors = 0;
  uint32_t samples = 0;
  uint32_t irqCount = 0;
  uint32_t measurementErrors = 0;
  uint32_t statusErrors = 0;
  uint32_t limitErrors = 0;
  uint32_t restartErrors = 0;
  uint32_t applyCount = 0;
  uint32_t rejectedThresholdCommands = 0;
  uint32_t nextRecoveryMs = 0;
  ShtLimit highSet;
  ShtLimit highClear;
  ShtLimit lowSet;
  ShtLimit lowClear;
};

struct PendingEvent {
  char value[128] = "";
};

WiFiClient networkClient;
HADevice device(DEVICE_ID);
HAMqtt mqtt(networkClient, device, HA_ENTITY_LIMIT);
SystemTestHaCleanup::CleanupCursor retainedEntityCleanup(
    SystemTestHaCleanup::TEST_05);
JC_EEPROM eeprom(JC_EEPROM::kbits_32, 1, EEPROM_PAGE_SIZE, I2C_ADDRESS_AT24C32);
RTC_DS3231 rtc;
SensirionI2cSht3x shtSensor;
FanController fan;

HASensorNumber uptimeSensor("persistence_rtc_uptime_seconds");
HASensorNumber wifiJoinsSensor("persistence_rtc_wifi_joins");
HASensorNumber wifiTimeoutsSensor("persistence_rtc_wifi_timeouts");
HASensorNumber wifiModuleResetsSensor("persistence_rtc_wifi_module_resets");
HASensorNumber otaGapSensor("persistence_rtc_ota_gap_violations");
HASensorNumber eepromWritesSensor("persistence_rtc_eeprom_writes");
HASensorNumber eepromSkippedSensor("persistence_rtc_eeprom_skipped_writes");
HASensorNumber eepromSequenceSensor("persistence_rtc_eeprom_sequence");
HASensorNumber eepromBootCountSensor("persistence_rtc_eeprom_boot_count");
HASensorNumber eepromChecksumSensor("persistence_rtc_eeprom_checksum");
HASensorNumber rtcEpochSensor("persistence_rtc_epoch");
HASensorNumber rtcAlarm1SeenSensor("persistence_rtc_alarm1_seen");
HASensorNumber rtcAlarm2SeenSensor("persistence_rtc_alarm2_seen");
HASensorNumber rtcInterruptSeenSensor("persistence_rtc_alarm_isr_seen");
HASensorNumber rtcAlarmClearsSensor("persistence_rtc_alarm_clears");
HASensorNumber temperatureSensor("temperature");
HASensorNumber humiditySensor("humidity");
HASensorNumber fanRpmSensor("fan_rpm");
HASensorNumber fanTachPulsesSensor("fan_tach_pulses");
HASensorNumber shtAlertInterruptsSensor("sht_alert_interrupts");
HASensorNumber shtMeasurementErrorsSensor("sht_measurement_errors");
HASensorNumber shtStatusErrorsSensor("sht_status_errors");
HASensor sketchIdentitySensor("sketch_identity");
HASensor shtThresholdResultSensor("sht_threshold_result");
HASensor shtDiagnosticSensor("sht_diagnostic");
HASensor testEventSensor("test_event");

HABinarySensor eepromFaultSensor("eeprom_fault");
HABinarySensor rtcFaultSensor("rtc_fault");
HABinarySensor fanFaultSensor("fan_fault");
HABinarySensor shtFaultSensor("sht_fault");
HABinarySensor shtAlertLineSensor("sht_alert_line");
HABinarySensor shtInterruptAttachedSensor("sht_interrupt_attached");
HABinarySensor rtcLostPowerSensor("persistence_rtc_lost_power");
HABinarySensor alarm1ConfiguredSensor("persistence_rtc_alarm1_configured");
HABinarySensor alarm2ConfiguredSensor("persistence_rtc_alarm2_configured");
HABinarySensor fanSafeSensor("persistence_rtc_fan_safe");
HABinarySensor relaySafeSensor("persistence_rtc_relay_safe");
HABinarySensor shdnSafeSensor("persistence_rtc_shdn_safe");
HASwitch fanSwitch("fan");
HASwitch fanAutoModeSwitch("fan_auto_mode");
HANumber tempHighSetNumber("temp_high_set", HANumber::PrecisionP1);
HANumber tempHighClearNumber("temp_high_clear", HANumber::PrecisionP1);
HANumber tempLowSetNumber("temp_low_set", HANumber::PrecisionP1);
HANumber tempLowClearNumber("temp_low_clear", HANumber::PrecisionP1);
HANumber humHighSetNumber("hum_high_set", HANumber::PrecisionP1);
HANumber humHighClearNumber("hum_high_clear", HANumber::PrecisionP1);
HANumber humLowSetNumber("hum_low_set", HANumber::PrecisionP1);
HANumber humLowClearNumber("hum_low_clear", HANumber::PrecisionP1);

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
uint32_t lastShtSampleMs = 0;
uint32_t lastShtStatusMs = 0;
uint32_t lastShtCommandUs = 0;
bool shtCommandSeen = false;
uint32_t lastStatusPrintMs = 0;
uint32_t safeStateEnforceCount = 0;
uint32_t thresholdCommandCount = 0;
char thresholdFeedback[112] = "boot: SHT initialization pending";
char lastTestEvent[128] = "0 boot_pending";
uint32_t testEventSequence = 0;
bool haSketchIdentityPublished = false;
bool retiredTopicsCleared = false;
PendingEvent pendingEvents[EVENT_QUEUE_CAPACITY];
uint8_t pendingEventHead = 0;
uint8_t pendingEventCount = 0;
bool lastReportedFanOn = false;
bool lastReportedAutoDemand = false;
bool lastReportedFanFault = false;
bool lastReportedTempLow = false;
bool lastReportedHumLow = false;
bool lastReportedUnexplainedSummary = false;

PersistenceState persistenceState;
RtcState rtcState;
ShtState shtState;
ThresholdConfig activeThresholds;
TestRecord activeRecord;

void publishTestEvent(const char* eventName);
void forceSafeAutoDemand();
bool readShtStatus(bool handleReset);
void flushOnePendingEvent();
void publishThresholdStates(bool force = false);
void publishShtDiagnostic();
bool thresholdConfigValid(const ThresholdConfig& config);

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
  record.tempHighSet = DEFAULT_TEMP_HIGH_SET;
  record.tempHighClear = DEFAULT_TEMP_HIGH_CLEAR;
  record.humHighSet = DEFAULT_HUM_HIGH_SET;
  record.humHighClear = DEFAULT_HUM_HIGH_CLEAR;
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
void waitForShtCommandGuard() {
  if (!shtCommandSeen) {
    return;
  }
  const uint32_t elapsedUs = micros() - lastShtCommandUs;
  if (elapsedUs < SHT_COMMAND_GUARD_US) {
    delayMicroseconds(static_cast<unsigned int>(SHT_COMMAND_GUARD_US - elapsedUs));
  }
}

void markShtCommandComplete() {
  lastShtCommandUs = micros();
  shtCommandSeen = true;
}


void configurePinsForSafeState() {
  pinMode(PIN_FAN_SWITCH, OUTPUT);
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);

  pinMode(PIN_LIGHT_POWER, OUTPUT);
  digitalWrite(PIN_LIGHT_POWER, LIGHT_RELAY_OPEN_LEVEL);

  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);

  pinMode(PIN_SHT_ALERT, INPUT);
  pinMode(PIN_RTC_ALARM, INPUT_PULLUP);
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  pinMode(PIN_SOIL_SENSOR, INPUT);
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);
}

void enforceSafeOutputs() {
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  digitalWrite(PIN_LIGHT_POWER, LIGHT_RELAY_OPEN_LEVEL);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  safeStateEnforceCount++;
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

void publishPersistencePhase(const char* context, const char* phase) {
  char eventName[80];
  snprintf(eventName, sizeof(eventName), "%s_%s", context, phase);
  publishTestEvent(eventName);
}

bool persistAndVerify(const TestRecord& expected, const char* context) {
  TestRecord current{};
  if (!readRecord(current)) {
    persistenceState.verifyOk = false;
    persistenceState.lastVerifyError = persistenceState.lastReadError;
    publishPersistencePhase(context, "pre_read_failed");
    return false;
  }
  publishPersistencePhase(context, "pre_read_ok");

  if (memcmp(&current, &expected, sizeof(expected)) == 0) {
    persistenceState.skipped++;
    persistenceState.writeOk = true;
    persistenceState.lastWriteError = 0;
    publishPersistencePhase(context, "write_skipped_unchanged");
  } else {
    if (!writeRecordBytes(expected)) {
      persistenceState.verifyOk = false;
      persistenceState.lastVerifyError = persistenceState.lastWriteError;
      publishPersistencePhase(context, "write_failed");
      return false;
    }
    publishPersistencePhase(context, "write_ok");
  }

  TestRecord verified{};
  if (!readRecord(verified)) {
    persistenceState.verifyOk = false;
    persistenceState.lastVerifyError = persistenceState.lastReadError;
    publishPersistencePhase(context, "verify_read_failed");
    return false;
  }

  persistenceState.magicOk = recordShapeValid(verified);
  persistenceState.checksumOk = recordValid(verified);
  if (!persistenceState.checksumOk || memcmp(&verified, &expected, sizeof(expected)) != 0) {
    persistenceState.verifyOk = false;
    persistenceState.lastVerifyError = 1;
    persistenceState.lastError = 1;
    publishPersistencePhase(context, "verify_mismatch");
    return false;
  }

  persistenceState.lastError = 0;
  persistenceState.lastVerifyError = 0;
  acceptVerifiedRecord(verified);
  publishPersistencePhase(context, "verify_ok");
  return true;
}

bool eepromHasFault() {
  return !(persistenceState.present &&
      persistenceState.readOk &&
      persistenceState.writeOk &&
      persistenceState.verifyOk &&
      persistenceState.magicOk &&
      persistenceState.checksumOk);
}

void initializePersistence() {
  persistenceState.present = (eeprom.begin(JC_EEPROM::twiClock100kHz) == 0) &&
      probeI2cAddress(I2C_ADDRESS_AT24C32);
  if (!persistenceState.present) {
    persistenceState.lastError = 1;
    activeRecord = makeDefaultRecord(1, 1);
    publishTestEvent("eeprom_boot_probe_failed");
    return;
  }
  publishTestEvent("eeprom_boot_probe_ok");

  TestRecord stored{};
  if (!readRecord(stored)) {
    activeRecord = makeDefaultRecord(1, 1);
    publishTestEvent("eeprom_boot_read_failed");
    return;
  }

  TestRecord bootRecord{};
  if (recordValid(stored)) {
    acceptVerifiedRecord(stored);
    bootRecord = stored;
    bootRecord.sequence++;
    bootRecord.bootCount++;
    bootRecord.checksum = checksumRecord(bootRecord);
    publishTestEvent("eeprom_boot_record_valid");
  } else {
    bootRecord = makeDefaultRecord(1, 1);
    activeRecord = bootRecord;
    publishTestEvent("eeprom_boot_defaults_selected");
  }

  if (!persistAndVerify(bootRecord, "eeprom_boot")) {
    publishTestEvent("eeprom_boot_transaction_failed");
  }
}

void loadActiveThresholds() {
  activeThresholds.tempHighSet = activeRecord.tempHighSet;
  activeThresholds.tempHighClear = activeRecord.tempHighClear;
  activeThresholds.tempLowSet = DEFAULT_TEMP_LOW_SET;
  activeThresholds.tempLowClear = DEFAULT_TEMP_LOW_CLEAR;
  activeThresholds.humHighSet = activeRecord.humHighSet;
  activeThresholds.humHighClear = activeRecord.humHighClear;
  activeThresholds.humLowSet = DEFAULT_HUM_LOW_SET;
  activeThresholds.humLowClear = DEFAULT_HUM_LOW_CLEAR;
  if (!thresholdConfigValid(activeThresholds)) {
    activeThresholds = ThresholdConfig{};
  }
}
bool persistFanAutoMode(bool enabled) {
  if (!persistenceState.present) {
    publishTestEvent("fan_auto_eeprom_unavailable");
    return false;
  }

  TestRecord updated = activeRecord;
  if (updated.fanAutoMode != static_cast<uint8_t>(enabled)) {
    updated.sequence++;
    updated.fanAutoMode = enabled ? 1U : 0U;
    updated.checksum = checksumRecord(updated);
  }

  const char* context = enabled ? "fan_auto_on" : "fan_auto_off";
  if (!persistAndVerify(updated, context)) {
    return false;
  }
  return true;
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

uint8_t shtCrc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1) ^ 0x31U) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

uint16_t encodeShtLimit(float temperature, float humidity) {
  const float boundedTemperature = constrain(temperature, -45.0f, 130.0f);
  const float boundedHumidity = constrain(humidity, 0.0f, 100.0f);
  const uint16_t temperatureRaw = static_cast<uint16_t>((boundedTemperature + 45.0f) * 65535.0f / 175.0f);
  const uint16_t humidityRaw = static_cast<uint16_t>(boundedHumidity * 65535.0f / 100.0f);
  return static_cast<uint16_t>(((humidityRaw >> 9) << 9) | (temperatureRaw >> 7));
}

float decodeShtLimitTemperature(uint16_t raw) {
  return (static_cast<float>(raw & 0x01FFU) * 175.0f / 511.0f) - 45.0f;
}

float decodeShtLimitHumidity(uint16_t raw) {
  return static_cast<float>((raw >> 9) & 0x7FU) * 100.0f / 127.0f;
}

bool thresholdConfigValid(const ThresholdConfig& config) {
  const bool temperatureRange = config.tempLowSet >= TEMP_MIN_C && config.tempHighSet <= TEMP_MAX_C;
  const bool humidityRange = config.humLowSet >= HUM_MIN_PERCENT && config.humHighSet <= HUM_MAX_PERCENT;
  const bool temperatureOrder =
      (config.tempLowClear - config.tempLowSet) >= TEMP_MIN_GAP_C &&
      (config.tempHighClear - config.tempLowClear) >= TEMP_MIN_GAP_C &&
      (config.tempHighSet - config.tempHighClear) >= TEMP_MIN_GAP_C;
  const bool humidityOrder =
      (config.humLowClear - config.humLowSet) >= HUM_MIN_GAP_PERCENT &&
      (config.humHighClear - config.humLowClear) >= HUM_MIN_GAP_PERCENT &&
      (config.humHighSet - config.humHighClear) >= HUM_MIN_GAP_PERCENT;
  return temperatureRange && humidityRange && temperatureOrder && humidityOrder;
}

void publishShtPhase(const char* context, const char* phase) {
  char eventName[80];
  snprintf(eventName, sizeof(eventName), "%s_%s", context, phase);
  publishTestEvent(eventName);
}

void scheduleShtRecovery(bool restartBackoff = false) {
  const bool wasPending = shtState.recoveryPending;
  shtState.recoveryPending = true;
  if (!wasPending || restartBackoff) {
    shtState.nextRecoveryMs = millis() + SHT_RECOVERY_BACKOFF_MS;
  }
  if (!wasPending) {
    publishTestEvent("sht_recovery_scheduled");
  }
}

void recordLimitFailure(const char* context, int16_t error) {
  shtState.lastLimitError = error;
  shtState.limitErrors++;
  if (shtState.consecutiveLimitErrors < UINT16_MAX) {
    shtState.consecutiveLimitErrors++;
  }
  publishShtPhase(context, "failed");
}

bool clearShtStatus(const char* context) {
  waitForShtCommandGuard();
  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(0x30);
  Wire.write(0x41);
  const uint8_t error = Wire.endTransmission();
  markShtCommandComplete();
  if (error != 0) {
    recordLimitFailure(context, error);
    return false;
  }
  return true;
}

bool writeShtLimit(uint8_t commandLsb,
                   float temperature,
                   float humidity,
                   const char* context) {
  const uint16_t raw = encodeShtLimit(temperature, humidity);
  const uint8_t data[2] = {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFFU)};

  waitForShtCommandGuard();
  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(SHT31_ALERT_WRITE_MSB);
  Wire.write(commandLsb);
  Wire.write(data[0]);
  Wire.write(data[1]);
  Wire.write(shtCrc8(data, sizeof(data)));
  const uint8_t error = Wire.endTransmission();
  markShtCommandComplete();
  if (error != 0) {
    recordLimitFailure(context, error);
    return false;
  }
  return true;
}

bool readShtLimit(uint8_t commandLsb, ShtLimit& limit, const char* context) {
  limit.ok = false;
  waitForShtCommandGuard();
  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(SHT31_ALERT_READ_MSB);
  Wire.write(commandLsb);
  const uint8_t transmissionError = Wire.endTransmission();
  markShtCommandComplete();
  if (transmissionError != 0) {
    recordLimitFailure(context, transmissionError);
    return false;
  }

  waitForShtCommandGuard();
  const uint8_t bytesRead = Wire.requestFrom(I2C_ADDRESS_SHT31, static_cast<uint8_t>(3));
  markShtCommandComplete();
  if (bytesRead != 3) {
    while (Wire.available()) {
      Wire.read();
    }
    recordLimitFailure(context, 0x00FF);
    return false;
  }

  uint8_t data[2];
  data[0] = Wire.read();
  data[1] = Wire.read();
  const uint8_t receivedCrc = Wire.read();
  if (shtCrc8(data, sizeof(data)) != receivedCrc) {
    recordLimitFailure(context, 0x00FE);
    return false;
  }

  limit.raw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  limit.temperature = decodeShtLimitTemperature(limit.raw);
  limit.humidity = decodeShtLimitHumidity(limit.raw);
  limit.ok = true;
  return true;
}

bool readAllShtLimits(ShtLimit& highSet,
                      ShtLimit& highClear,
                      ShtLimit& lowSet,
                      ShtLimit& lowClear,
                      const char* context) {
  char phase[64];
  snprintf(phase, sizeof(phase), "%s_read_high_set", context);
  bool ok = readShtLimit(SHT31_ALERT_RHS, highSet, phase);
  snprintf(phase, sizeof(phase), "%s_read_high_clear", context);
  if (ok) ok = readShtLimit(SHT31_ALERT_RHC, highClear, phase);
  snprintf(phase, sizeof(phase), "%s_read_low_set", context);
  if (ok) ok = readShtLimit(SHT31_ALERT_RLS, lowSet, phase);
  snprintf(phase, sizeof(phase), "%s_read_low_clear", context);
  if (ok) ok = readShtLimit(SHT31_ALERT_RLC, lowClear, phase);
  return ok;
}

bool limitMatches(const ShtLimit& limit, float temperature, float humidity) {
  return limit.ok && fabs(limit.temperature - temperature) <= TEMP_READBACK_TOLERANCE_C &&
         fabs(limit.humidity - humidity) <= HUM_READBACK_TOLERANCE_PERCENT;
}

bool limitsMatchConfig(const ShtLimit& highSet,
                       const ShtLimit& highClear,
                       const ShtLimit& lowSet,
                       const ShtLimit& lowClear,
                       const ThresholdConfig& config) {
  return limitMatches(highSet, config.tempHighSet, config.humHighSet) &&
      limitMatches(highClear, config.tempHighClear, config.humHighClear) &&
      limitMatches(lowSet, config.tempLowSet, config.humLowSet) &&
      limitMatches(lowClear, config.tempLowClear, config.humLowClear);
}

void setThresholdFeedback(const char* message) {
  if (message != thresholdFeedback) {
    strncpy(thresholdFeedback, message, sizeof(thresholdFeedback) - 1);
    thresholdFeedback[sizeof(thresholdFeedback) - 1] = '\0';
  }
  if (mqtt.isConnected()) {
    shtThresholdResultSensor.setValue(thresholdFeedback);
    publishShtDiagnostic();
  }
}

bool applyShtThresholds(const ThresholdConfig& config,
                        const char* context,
                        bool measurementAlreadyStopped) {
  if (!thresholdConfigValid(config) || !shtState.present) {
    publishShtPhase(context, "invalid_order_or_sensor_missing");
    return false;
  }

  shtState.transactionInProgress = true;
  bool stopped = measurementAlreadyStopped;
  if (!measurementAlreadyStopped) {
    waitForShtCommandGuard();
    publishShtPhase(context, "stop_periodic");
    const int16_t stopError = shtSensor.stopMeasurement();
    markShtCommandComplete();
    if (stopError != NO_ERROR) {
      shtState.lastLimitError = stopError;
      shtState.limitErrors++;
      shtState.transactionInProgress = false;
      publishShtPhase(context, "stop_failed");
      forceSafeAutoDemand();
      scheduleShtRecovery();
      return false;
    }
    stopped = true;
  }

  shtState.measurementRunning = false;
  shtState.controlledMeasurementPause = stopped;
  waitForShtCommandGuard();

  ShtLimit verifiedHighSet;
  ShtLimit verifiedHighClear;
  ShtLimit verifiedLowSet;
  ShtLimit verifiedLowClear;
  bool transactionOk = clearShtStatus("threshold_clear_before");
  if (transactionOk) {
    publishShtPhase(context, "write_started");
    transactionOk = writeShtLimit(SHT31_ALERT_WHS, config.tempHighSet, config.humHighSet, "threshold_write_high_set");
  }
  if (transactionOk) {
    transactionOk = writeShtLimit(SHT31_ALERT_WHC, config.tempHighClear, config.humHighClear, "threshold_write_high_clear");
  }
  if (transactionOk) {
    transactionOk = writeShtLimit(SHT31_ALERT_WLS, config.tempLowSet, config.humLowSet, "threshold_write_low_set");
  }
  if (transactionOk) {
    transactionOk = writeShtLimit(SHT31_ALERT_WLC, config.tempLowClear, config.humLowClear, "threshold_write_low_clear");
  }
  if (transactionOk) {
    publishShtPhase(context, "write_complete");
    transactionOk = readAllShtLimits(
        verifiedHighSet, verifiedHighClear, verifiedLowSet, verifiedLowClear, "threshold_verify");
  }
  if (transactionOk && !limitsMatchConfig(
          verifiedHighSet, verifiedHighClear, verifiedLowSet, verifiedLowClear, config)) {
    recordLimitFailure("threshold_verify_mismatch", 0x00FD);
    transactionOk = false;
  }
  if (transactionOk) {
    publishShtPhase(context, "readback_verified");
    transactionOk = clearShtStatus("threshold_clear_after");
  }

  waitForShtCommandGuard();
  publishShtPhase(context, "restart_periodic");
  const int16_t startError = shtSensor.startPeriodicMeasurement(REPEATABILITY_MEDIUM, MPS_ONE_PER_SECOND);
  markShtCommandComplete();
  shtState.lastRestartError = startError;
  shtState.measurementRunning = startError == NO_ERROR;
  shtState.initialized = shtState.present && shtState.measurementRunning;
  if (startError != NO_ERROR) {
    shtState.restartErrors++;
    publishShtPhase(context, "restart_failed");
  } else {
    lastShtSampleMs = millis();
    publishShtPhase(context, "restart_ok");
  }

  shtState.controlledMeasurementPause = false;
  shtState.transactionInProgress = false;
  const bool success = transactionOk && shtState.measurementRunning;
  if (success) {
    shtState.highSet = verifiedHighSet;
    shtState.highClear = verifiedHighClear;
    shtState.lowSet = verifiedLowSet;
    shtState.lowClear = verifiedLowClear;
    shtState.limitsApplied = true;
    shtState.limitsVerified = true;
    shtState.commandError = false;
    shtState.crcError = false;
    shtState.consecutiveLimitErrors = 0;
    shtState.recoveryPending = false;
    shtState.applyCount++;
    publishShtPhase(context, "passed");
    return true;
  }

  shtState.limitsApplied = false;
  shtState.limitsVerified = false;
  forceSafeAutoDemand();
  scheduleShtRecovery();
  publishShtPhase(context, "failed");
  return false;
}
bool shtHasFault() {
  const bool measurementStoppedUnexpectedly =
      !shtState.measurementRunning && !shtState.controlledMeasurementPause;
  return !shtState.alertInterruptAttached || !shtState.present || !shtState.initialized ||
      measurementStoppedUnexpectedly || !shtState.measurementOk || !shtState.statusOk ||
      !shtState.limitsApplied || !shtState.limitsVerified || shtState.recoveryPending ||
      shtState.commandError || shtState.crcError;
}

void forceSafeAutoDemand() {
  shtState.tempHighDemand = false;
  shtState.humHighDemand = false;
  shtState.tempLowObserved = false;
  shtState.humLowObserved = false;
  fan.setAutoDemand(false);
}

bool readShtMeasurement() {
  float temperature = NAN;
  float humidity = NAN;
  waitForShtCommandGuard();
  const int16_t error = shtSensor.blockingReadMeasurement(temperature, humidity);
  markShtCommandComplete();
  shtState.lastMeasurementError = error;
  shtState.samples++;
  shtState.measurementOk = error == NO_ERROR;
  if (shtState.measurementOk) {
    if (shtState.consecutiveMeasurementErrors > 0) {
      publishTestEvent("sht_measurement_recovered");
    }
    shtState.consecutiveMeasurementErrors = 0;
    shtState.temperature = temperature;
    shtState.humidity = humidity;
  } else {
    shtState.measurementErrors++;
    if (shtState.consecutiveMeasurementErrors < UINT16_MAX) {
      shtState.consecutiveMeasurementErrors++;
    }
    forceSafeAutoDemand();
    if (shtState.consecutiveMeasurementErrors == 1) {
      publishTestEvent("sht_measurement_error");
    }
    if (shtState.consecutiveMeasurementErrors >= SHT_RECOVERY_ERROR_THRESHOLD) {
      scheduleShtRecovery();
    }
  }
  return shtState.measurementOk;
}

void evaluateShtDemand() {
  if (!shtState.alertInterruptAttached || !shtState.measurementOk || !shtState.statusOk ||
      !shtState.limitsVerified || shtState.recoveryPending || shtState.transactionInProgress ||
      shtState.commandError || shtState.crcError) {
    forceSafeAutoDemand();
    return;
  }

  if (shtState.tempHighDemand) {
    if (shtState.temperature <= activeThresholds.tempHighClear) {
      shtState.tempHighDemand = false;
    }
  } else if (shtState.tempTrackingAlert && shtState.temperature >= activeThresholds.tempHighSet) {
    shtState.tempHighDemand = true;
  }

  if (shtState.humHighDemand) {
    if (shtState.humidity <= activeThresholds.humHighClear) {
      shtState.humHighDemand = false;
    }
  } else if (shtState.humTrackingAlert && shtState.humidity >= activeThresholds.humHighSet) {
    shtState.humHighDemand = true;
  }

  if (shtState.tempLowObserved) {
    if (shtState.temperature >= activeThresholds.tempLowClear) {
      shtState.tempLowObserved = false;
    }
  } else if (shtState.tempTrackingAlert && shtState.temperature <= activeThresholds.tempLowSet) {
    shtState.tempLowObserved = true;
  }

  if (shtState.humLowObserved) {
    if (shtState.humidity >= activeThresholds.humLowClear) {
      shtState.humLowObserved = false;
    }
  } else if (shtState.humTrackingAlert && shtState.humidity <= activeThresholds.humLowSet) {
    shtState.humLowObserved = true;
  }
  fan.setAutoDemand(shtState.tempHighDemand || shtState.humHighDemand);
}

bool readShtStatus(bool handleReset) {
  uint16_t status = 0;
  const bool previousCommandError = shtState.commandError;
  const bool previousCrcError = shtState.crcError;
  waitForShtCommandGuard();
  const int16_t error = shtSensor.readStatusRegister(status);
  markShtCommandComplete();
  shtState.lastStatusError = error;
  shtState.statusOk = error == NO_ERROR;
  shtState.alertLineActive = digitalRead(PIN_SHT_ALERT) == HIGH;
  if (!shtState.statusOk) {
    shtState.statusErrors++;
    if (shtState.consecutiveStatusErrors < UINT16_MAX) {
      shtState.consecutiveStatusErrors++;
    }
    forceSafeAutoDemand();
    if (shtState.consecutiveStatusErrors == 1) {
      publishTestEvent("sht_status_error");
    }
    if (shtState.consecutiveStatusErrors >= SHT_RECOVERY_ERROR_THRESHOLD) {
      scheduleShtRecovery();
    }
    return false;
  }

  if (shtState.consecutiveStatusErrors > 0) {
    publishTestEvent("sht_status_recovered");
  }
  shtState.consecutiveStatusErrors = 0;

  shtState.statusRegister = status;
  shtState.alertSummary = (status & (1U << 15)) != 0;
  shtState.humTrackingAlert = (status & (1U << 11)) != 0;
  shtState.tempTrackingAlert = (status & (1U << 10)) != 0;
  shtState.resetDetected = (status & (1U << 4)) != 0;
  shtState.commandError = (status & (1U << 1)) != 0;
  shtState.crcError = (status & (1U << 0)) != 0;
  shtState.unexplainedSummary = shtState.alertSummary && !shtState.tempTrackingAlert &&
      !shtState.humTrackingAlert && !shtState.alertLineActive;

  if (shtState.commandError && !previousCommandError) {
    shtState.statusErrors++;
    publishTestEvent("sht_status_command_error");
  }
  if (shtState.crcError && !previousCrcError) {
    shtState.statusErrors++;
    publishTestEvent("sht_status_crc_error");
  }
  if (shtState.commandError || shtState.crcError) {
    forceSafeAutoDemand();
    scheduleShtRecovery();
    return false;
  }

  if (shtState.resetDetected && handleReset) {
    publishTestEvent("sht_reset_detected");
    forceSafeAutoDemand();
    waitForShtCommandGuard();
    if (applyShtThresholds(activeThresholds, "sht_reset_reapply", false)) {
      shtState.resetDetected = false;
      publishTestEvent("sht_reset_recovered");
      waitForShtCommandGuard();
      return readShtStatus(false);
    }
    shtState.statusOk = false;
    return false;
  }

  evaluateShtDemand();
  return true;
}

void initializeSht() {
  waitForShtCommandGuard();
  shtState.present = probeI2cAddress(I2C_ADDRESS_SHT31);
  markShtCommandComplete();
  if (!shtState.present) {
    setThresholdFeedback("boot failed: SHT31 not present at 0x45");
    publishTestEvent("sht_boot_probe_failed");
    forceSafeAutoDemand();
    scheduleShtRecovery();
    return;
  }
  publishTestEvent("sht_boot_probe_ok");

  shtSensor.begin(Wire, I2C_ADDRESS_SHT31);
  waitForShtCommandGuard();
  const int16_t stopError = shtSensor.stopMeasurement();
  markShtCommandComplete();
  if (stopError != NO_ERROR) {
    shtState.lastLimitError = stopError;
    shtState.limitErrors++;
    setThresholdFeedback("boot failed: SHT stop measurement");
    publishTestEvent("sht_boot_stop_failed");
    forceSafeAutoDemand();
    scheduleShtRecovery();
    return;
  }
  shtState.measurementRunning = false;
  shtState.controlledMeasurementPause = true;
  publishTestEvent("sht_boot_stop_ok");
  waitForShtCommandGuard();

  const int16_t resetError = shtSensor.softReset();
  markShtCommandComplete();
  delay(10);
  if (resetError != NO_ERROR) {
    shtState.initialized = false;
    shtState.controlledMeasurementPause = false;
    setThresholdFeedback("boot failed: SHT soft reset");
    publishTestEvent("sht_boot_reset_failed");
    forceSafeAutoDemand();
    scheduleShtRecovery();
    return;
  }
  publishTestEvent("sht_boot_reset_ok");

  const bool statusCaptureOk = readShtStatus(false);
  waitForShtCommandGuard();
  publishTestEvent(statusCaptureOk ? "sht_boot_status_captured" : "sht_boot_status_capture_failed");
  ShtLimit capturedHighSet;
  ShtLimit capturedHighClear;
  ShtLimit capturedLowSet;
  ShtLimit capturedLowClear;
  const bool limitsCaptureOk = statusCaptureOk && readAllShtLimits(
      capturedHighSet, capturedHighClear, capturedLowSet, capturedLowClear, "sht_boot_capture");
  publishTestEvent(limitsCaptureOk ? "sht_boot_limits_captured" : "sht_boot_limits_capture_failed");
  if (!limitsCaptureOk) {
    shtState.initialized = false;
    shtState.controlledMeasurementPause = false;
    setThresholdFeedback("boot failed: status or limit capture");
    forceSafeAutoDemand();
    scheduleShtRecovery();
    return;
  }

  if (!applyShtThresholds(activeThresholds, "sht_boot_limits", true)) {
    setThresholdFeedback("boot failed: limit write/readback or periodic restart");
    forceSafeAutoDemand();
    return;
  }

  shtState.measurementOk = false;
  shtState.statusOk = false;
  setThresholdFeedback("boot: limits verified; measurement pending");
  publishTestEvent("sht_boot_measurement_pending");
}

void serviceShtRecovery(uint32_t nowMs) {
  if (!shtState.recoveryPending || shtState.transactionInProgress ||
      static_cast<int32_t>(nowMs - shtState.nextRecoveryMs) < 0) {
    return;
  }

  publishTestEvent("sht_recovery_started");
  waitForShtCommandGuard();
  shtState.present = probeI2cAddress(I2C_ADDRESS_SHT31);
  markShtCommandComplete();
  if (!shtState.present) {
    publishTestEvent("sht_recovery_probe_failed");
    scheduleShtRecovery(true);
    return;
  }

  shtSensor.begin(Wire, I2C_ADDRESS_SHT31);
  waitForShtCommandGuard();
  const int16_t stopError = shtSensor.stopMeasurement();
  markShtCommandComplete();
  if (stopError != NO_ERROR) {
    shtState.lastLimitError = stopError;
    shtState.limitErrors++;
    publishTestEvent("sht_recovery_stop_failed");
    scheduleShtRecovery(true);
    return;
  }
  shtState.measurementRunning = false;
  shtState.controlledMeasurementPause = true;
  waitForShtCommandGuard();

  const int16_t resetError = shtSensor.softReset();
  markShtCommandComplete();
  delay(10);
  if (resetError != NO_ERROR) {
    shtState.controlledMeasurementPause = false;
    shtState.initialized = false;
    publishTestEvent("sht_recovery_reset_failed");
    scheduleShtRecovery(true);
    return;
  }

  if (!applyShtThresholds(activeThresholds, "sht_recovery_limits", true)) {
    setThresholdFeedback("recovery failed: limit transaction");
    scheduleShtRecovery(true);
    return;
  }

  shtState.measurementOk = false;
  shtState.statusOk = false;
  setThresholdFeedback("recovery: limits verified; measurement pending");
  publishTestEvent("sht_recovery_restart_complete");
}

void serviceSht(uint32_t nowMs) {
  bool irqPending = false;
  noInterrupts();
  irqPending = shtAlertPending;
  shtAlertPending = false;
  interrupts();
  if (irqPending) {
    shtState.irqCount++;
    publishTestEvent("sht_alert_interrupt_seen");
  }

  serviceShtRecovery(nowMs);
  if (!shtState.initialized) {
    forceSafeAutoDemand();
    return;
  }

  const bool sampleDue = (nowMs - lastShtSampleMs) >= SHT_SAMPLE_INTERVAL_MS;
  if (sampleDue) {
    lastShtSampleMs = nowMs;
    readShtMeasurement();
    waitForShtCommandGuard();
  }

  if (irqPending || sampleDue || (nowMs - lastShtStatusMs) >= SHT_STATUS_INTERVAL_MS) {
    lastShtStatusMs = nowMs;
    readShtStatus(true);
    if (shtState.samples == 1 && shtState.measurementOk && shtState.statusOk &&
        shtState.limitsVerified && !shtState.recoveryPending) {
      setThresholdFeedback("running: limits and first SHT sample verified");
      publishTestEvent("sht_first_sample_verified");
    }
    if (mqtt.isConnected()) {
      shtAlertLineSensor.setState(shtState.alertLineActive);
    }
  }
}
void publishTestEvent(const char* eventName) {
  testEventSequence++;
  snprintf(lastTestEvent,
           sizeof(lastTestEvent),
           "%lu %s uptime=%lu",
           static_cast<unsigned long>(testEventSequence),
           eventName,
           static_cast<unsigned long>(millis() / 1000UL));

  if (serialAvailable()) {
    Serial.print(F("[Event] "));
    Serial.println(lastTestEvent);
  }
  if (pendingEventCount >= EVENT_QUEUE_CAPACITY) {
    pendingEventHead = (pendingEventHead + 1U) % EVENT_QUEUE_CAPACITY;
    pendingEventCount--;
  }
  const uint8_t tail = (pendingEventHead + pendingEventCount) % EVENT_QUEUE_CAPACITY;
  strncpy(pendingEvents[tail].value, lastTestEvent, sizeof(pendingEvents[tail].value) - 1);
  pendingEvents[tail].value[sizeof(pendingEvents[tail].value) - 1] = '\0';
  pendingEventCount++;
}

void flushOnePendingEvent() {
  if (!mqtt.isConnected() || pendingEventCount == 0) {
    return;
  }
  if (!testEventSensor.setValue(pendingEvents[pendingEventHead].value)) {
    return;
  }
  pendingEventHead = (pendingEventHead + 1U) % EVENT_QUEUE_CAPACITY;
  pendingEventCount--;
}

void reportStateTransitions() {
  const bool autoDemand = shtState.tempHighDemand || shtState.humHighDemand;
  if (autoDemand != lastReportedAutoDemand) {
    publishTestEvent(autoDemand ? "fan_auto_demand_on" : "fan_auto_demand_off");
    lastReportedAutoDemand = autoDemand;
  }
  if (fan.isOn() != lastReportedFanOn) {
    publishTestEvent(fan.isOn() ? "fan_effective_on" : "fan_effective_off");
    lastReportedFanOn = fan.isOn();
  }
  if (fan.hasFault() != lastReportedFanFault) {
    publishTestEvent(fan.hasFault() ? "fan_fault_on" : "fan_fault_off");
    lastReportedFanFault = fan.hasFault();
  }
  if (shtState.tempLowObserved != lastReportedTempLow) {
    publishTestEvent(shtState.tempLowObserved ? "temperature_low_tracking_on" : "temperature_low_tracking_off");
    lastReportedTempLow = shtState.tempLowObserved;
  }
  if (shtState.humLowObserved != lastReportedHumLow) {
    publishTestEvent(shtState.humLowObserved ? "humidity_low_tracking_on" : "humidity_low_tracking_off");
    lastReportedHumLow = shtState.humLowObserved;
  }
  if (shtState.unexplainedSummary != lastReportedUnexplainedSummary) {
    publishTestEvent(shtState.unexplainedSummary ? "sht_unexplained_summary_on" : "sht_unexplained_summary_off");
    lastReportedUnexplainedSummary = shtState.unexplainedSummary;
  }
}

void publishThresholdStates(bool force) {
  tempHighSetNumber.setState(activeThresholds.tempHighSet, force);
  tempHighClearNumber.setState(activeThresholds.tempHighClear, force);
  tempLowSetNumber.setState(activeThresholds.tempLowSet, force);
  tempLowClearNumber.setState(activeThresholds.tempLowClear, force);
  humHighSetNumber.setState(activeThresholds.humHighSet, force);
  humHighClearNumber.setState(activeThresholds.humHighClear, force);
  humLowSetNumber.setState(activeThresholds.humLowSet, force);
  humLowClearNumber.setState(activeThresholds.humLowClear, force);
}

void onFanSwitchCommand(bool state, HASwitch*) {
  if (fan.isAutoMode()) {
    fan.setManualState(false);
    fanSwitch.setState(false, true);
    publishTestEvent("fan_manual_rejected_auto_mode");
    return;
  }
  fan.setManualState(state);
  fan.update(millis());
  fanSwitch.setState(fan.getManualState());
  publishTestEvent(state ? "fan_manual_on" : "fan_manual_off");
}

void onFanAutoModeCommand(bool state, HASwitch*) {
  if (!persistFanAutoMode(state)) {
    fanAutoModeSwitch.setState(fan.isAutoMode(), true);
    publishTestEvent("fan_auto_mode_persist_failed");
    return;
  }
  if (state) {
    fan.setManualState(false);
    fanSwitch.setState(false);
  }
  fan.setAutoMode(state);
  fan.update(millis());
  fanAutoModeSwitch.setState(fan.isAutoMode());
  publishTestEvent(state ? "fan_auto_mode_on" : "fan_auto_mode_off");
}

void onThresholdCommand(HANumeric number, HANumber* sender) {
  ThresholdConfig candidate = activeThresholds;
  const float value = number.toFloat();
  const char* target = "unknown";
  if (sender == &tempHighSetNumber) {
    target = "temp_high_set";
    candidate.tempHighSet = value;
  } else if (sender == &tempHighClearNumber) {
    target = "temp_high_clear";
    candidate.tempHighClear = value;
  } else if (sender == &tempLowSetNumber) {
    target = "temp_low_set";
    candidate.tempLowSet = value;
  } else if (sender == &tempLowClearNumber) {
    target = "temp_low_clear";
    candidate.tempLowClear = value;
  } else if (sender == &humHighSetNumber) {
    target = "hum_high_set";
    candidate.humHighSet = value;
  } else if (sender == &humHighClearNumber) {
    target = "hum_high_clear";
    candidate.humHighClear = value;
  } else if (sender == &humLowSetNumber) {
    target = "hum_low_set";
    candidate.humLowSet = value;
  } else if (sender == &humLowClearNumber) {
    target = "hum_low_clear";
    candidate.humLowClear = value;
  } else {
    return;
  }

  thresholdCommandCount++;
  if (!thresholdConfigValid(candidate)) {
    shtState.rejectedThresholdCommands++;
    snprintf(thresholdFeedback,
             sizeof(thresholdFeedback),
             "rejected #%lu: %s=%.1f violates complete order",
             static_cast<unsigned long>(thresholdCommandCount),
             target,
             static_cast<double>(value));
    setThresholdFeedback(thresholdFeedback);
    publishTestEvent("threshold_command_rejected_order");
    publishThresholdStates(true);
    return;
  }

  if (!applyShtThresholds(candidate, "threshold_command", false)) {
    shtState.rejectedThresholdCommands++;
    forceSafeAutoDemand();
    snprintf(thresholdFeedback,
             sizeof(thresholdFeedback),
             "failed #%lu: %s=%.1f; limit_err=%d restart_err=%d",
             static_cast<unsigned long>(thresholdCommandCount),
             target,
             static_cast<double>(value),
             shtState.lastLimitError,
             shtState.lastRestartError);
    setThresholdFeedback(thresholdFeedback);
    publishTestEvent("threshold_command_rejected_apply");
    publishThresholdStates(true);
    return;
  }

  activeThresholds = candidate;
  waitForShtCommandGuard();
  if (!readShtStatus(false)) {
    forceSafeAutoDemand();
    publishTestEvent("threshold_status_read_failed");
  }
  snprintf(thresholdFeedback,
           sizeof(thresholdFeedback),
           "applied #%lu: %s=%.1f; readback verified",
           static_cast<unsigned long>(thresholdCommandCount),
           target,
           static_cast<double>(value));
  setThresholdFeedback(thresholdFeedback);
  publishThresholdStates(true);
}

void configureThresholdNumber(HANumber& number,
                              const char* name,
                              const char* unit,
                              float minimum,
                              float maximum) {
  number.setName(name);
  number.setUnitOfMeasurement(unit);
  number.setMin(minimum);
  number.setMax(maximum);
  number.setStep(0.1f);
  number.onCommand(onThresholdCommand);
}

void configureHomeAssistant() {
  device.setName(DEVICE_NAME);
  device.setManufacturer("Smaeenhouse");
  device.setModel("Arduino Nano 33 IoT SHT Alert Fan Closed-Loop Test");
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

  eepromWritesSensor.setName("Persistence RTC EEPROM Writes");
  eepromWritesSensor.setStateClass("total_increasing");
  eepromSkippedSensor.setName("Persistence RTC EEPROM Skipped Writes");
  eepromSkippedSensor.setStateClass("total_increasing");
  eepromSequenceSensor.setName("Persistence RTC EEPROM Sequence");
  eepromBootCountSensor.setName("Persistence RTC EEPROM Boot Count");
  eepromChecksumSensor.setName("Persistence RTC EEPROM Checksum");

  rtcEpochSensor.setName("Persistence RTC Epoch");
  rtcAlarm1SeenSensor.setName("Persistence RTC Alarm1 Seen");
  rtcAlarm1SeenSensor.setStateClass("total_increasing");
  rtcAlarm2SeenSensor.setName("Persistence RTC Alarm2 Seen");
  rtcAlarm2SeenSensor.setStateClass("total_increasing");
  rtcInterruptSeenSensor.setName("Persistence RTC Alarm ISR Seen");
  rtcInterruptSeenSensor.setStateClass("total_increasing");
  rtcAlarmClearsSensor.setName("Persistence RTC Alarm Clears");
  rtcAlarmClearsSensor.setStateClass("total_increasing");

  temperatureSensor.setName("Temperature");
  temperatureSensor.setUnitOfMeasurement("C");
  temperatureSensor.setStateClass("measurement");
  humiditySensor.setName("Humidity");
  humiditySensor.setUnitOfMeasurement("%");
  humiditySensor.setStateClass("measurement");
  fanRpmSensor.setName("Fan RPM");
  fanRpmSensor.setUnitOfMeasurement("rpm");
  fanRpmSensor.setStateClass("measurement");
  fanTachPulsesSensor.setName("Fan Tach Pulses");
  fanTachPulsesSensor.setStateClass("total_increasing");
  shtAlertInterruptsSensor.setName("SHT Alert Interrupts");
  shtAlertInterruptsSensor.setStateClass("total_increasing");
  shtMeasurementErrorsSensor.setName("SHT Measurement Errors");
  shtMeasurementErrorsSensor.setStateClass("total_increasing");
  shtStatusErrorsSensor.setName("SHT Status Errors");
  shtStatusErrorsSensor.setStateClass("total_increasing");
  sketchIdentitySensor.setName("Sketch Identity");
  shtThresholdResultSensor.setName("SHT Threshold Result");
  shtDiagnosticSensor.setName("SHT Diagnostic");
  testEventSensor.setName("Test Step");

  eepromFaultSensor.setName("EEPROM Fault");
  rtcFaultSensor.setName("RTC Fault");
  fanFaultSensor.setName("Fan Fault");
  shtFaultSensor.setName("SHT Fault");
  shtAlertLineSensor.setName("SHT Alert Line");
  shtInterruptAttachedSensor.setName("SHT Interrupt Attached");
  rtcLostPowerSensor.setName("Persistence RTC Lost Power");
  alarm1ConfiguredSensor.setName("Persistence RTC Alarm1 Configured");
  alarm2ConfiguredSensor.setName("Persistence RTC Alarm2 Configured");
  fanSafeSensor.setName("Fan Output Off");
  relaySafeSensor.setName("Persistence RTC Relay Safe");
  shdnSafeSensor.setName("Persistence RTC SHDN Safe");

  fanSwitch.setName("Fan");
  fanAutoModeSwitch.setName("Fan Auto Mode");
  fanSwitch.onCommand(onFanSwitchCommand);
  fanAutoModeSwitch.onCommand(onFanAutoModeCommand);

  configureThresholdNumber(tempHighSetNumber, "Temp High Set", "C", TEMP_MIN_C, TEMP_MAX_C);
  configureThresholdNumber(tempHighClearNumber, "Temp High Clear", "C", TEMP_MIN_C, TEMP_MAX_C);
  configureThresholdNumber(tempLowSetNumber, "Temp Low Set", "C", TEMP_MIN_C, TEMP_MAX_C);
  configureThresholdNumber(tempLowClearNumber, "Temp Low Clear", "C", TEMP_MIN_C, TEMP_MAX_C);
  configureThresholdNumber(humHighSetNumber, "Hum High Set", "%", HUM_MIN_PERCENT, HUM_MAX_PERCENT);
  configureThresholdNumber(humHighClearNumber, "Hum High Clear", "%", HUM_MIN_PERCENT, HUM_MAX_PERCENT);
  configureThresholdNumber(humLowSetNumber, "Hum Low Set", "%", HUM_MIN_PERCENT, HUM_MAX_PERCENT);
  configureThresholdNumber(humLowClearNumber, "Hum Low Clear", "%", HUM_MIN_PERCENT, HUM_MAX_PERCENT);
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

void publishShtDiagnostic() {
  if (!mqtt.isConnected()) {
    return;
  }

  char diagnostic[144];
  if (!shtState.alertInterruptAttached) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: A7 interrupt not attached");
  } else if (!shtState.present) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: SHT31 not found at 0x45");
  } else if (shtState.recoveryPending) {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "fault: recovery pending limit_err=%d restart_err=%d",
             shtState.lastLimitError,
             shtState.lastRestartError);
  } else if (!shtState.measurementRunning && !shtState.controlledMeasurementPause) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: periodic measurement not running");
  } else if (!shtState.limitsApplied) {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "fault: alert-limit write failed error=%d total=%lu",
             shtState.lastLimitError,
             static_cast<unsigned long>(shtState.limitErrors));
  } else if (!shtState.limitsVerified) {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "fault: alert-limit readback failed error=%d total=%lu",
             shtState.lastLimitError,
             static_cast<unsigned long>(shtState.limitErrors));
  } else if (!shtState.measurementOk) {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "fault: measurement error=%d total=%lu consecutive=%u",
             shtState.lastMeasurementError,
             static_cast<unsigned long>(shtState.measurementErrors),
             shtState.consecutiveMeasurementErrors);
  } else if (!shtState.statusOk) {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "fault: status error=%d total=%lu consecutive=%u",
             shtState.lastStatusError,
             static_cast<unsigned long>(shtState.statusErrors),
             shtState.consecutiveStatusErrors);
  } else if (shtState.commandError || shtState.crcError) {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "fault: status command_error=%s crc_error=%s",
             shtState.commandError ? "yes" : "no",
             shtState.crcError ? "yes" : "no");
  } else {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "ok: T=%.2f RH=%.1f status=0x%04X line=%u irq=%lu m=%lu s=%lu l=%lu",
             static_cast<double>(shtState.temperature),
             static_cast<double>(shtState.humidity),
             shtState.statusRegister,
             shtState.alertLineActive ? 1U : 0U,
             static_cast<unsigned long>(shtState.irqCount),
             static_cast<unsigned long>(shtState.measurementErrors),
             static_cast<unsigned long>(shtState.statusErrors),
             static_cast<unsigned long>(shtState.limitErrors));
  }
  shtDiagnosticSensor.setValue(diagnostic);
}
void publishHaBootIdentity() {
  if (!mqtt.isConnected() || haSketchIdentityPublished) {
    return;
  }

  char identity[64];
  snprintf(identity, sizeof(identity), "%s v%s", SKETCH_NAME, SKETCH_VERSION);
  haSketchIdentityPublished = sketchIdentitySensor.setValue(identity);
}

void cleanupRetiredTopics() {
  if (!mqtt.isConnected() || retiredTopicsCleared) {
    return;
  }

  bool cleanupOk = true;
  for (uint8_t index = 0;
       index < sizeof(RETIRED_DIRECT_TOPICS) / sizeof(RETIRED_DIRECT_TOPICS[0]);
       ++index) {
    cleanupOk = mqtt.publish(RETIRED_DIRECT_TOPICS[index], "", true) && cleanupOk;
  }
  for (uint8_t index = 0; index < sizeof(RETIRED_ENTITIES) / sizeof(RETIRED_ENTITIES[0]); ++index) {
    char topic[192];
    snprintf(topic,
             sizeof(topic),
             "%s/%s/%s/%s/config",
             MQTT_PREFIX,
             RETIRED_ENTITIES[index].domain,
             DEVICE_ID,
             RETIRED_ENTITIES[index].id);
    cleanupOk = mqtt.publish(topic, "", true) && cleanupOk;
    snprintf(topic,
             sizeof(topic),
             "%s/%s/%s/stat_t",
             MQTT_DATA_PREFIX,
             DEVICE_ID,
             RETIRED_ENTITIES[index].id);
    cleanupOk = mqtt.publish(topic, "", true) && cleanupOk;
  }
  retiredTopicsCleared = cleanupOk;
  if (cleanupOk) {
    publishTestEvent("retained_topic_cleanup_complete");
  }
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
  wifiJoinsSensor.setValue(static_cast<uint32_t>(wifiConnectionCount), force);
  wifiTimeoutsSensor.setValue(static_cast<uint32_t>(wifiConnectTimeoutCount), force);
  wifiModuleResetsSensor.setValue(static_cast<uint32_t>(wifiModuleResetCount), force);
  otaGapSensor.setValue(static_cast<uint32_t>(otaPollGapViolations), force);

  eepromWritesSensor.setValue(static_cast<uint32_t>(persistenceState.writes), force);
  eepromSkippedSensor.setValue(static_cast<uint32_t>(persistenceState.skipped), force);
  eepromSequenceSensor.setValue(static_cast<uint32_t>(persistenceState.sequence), force);
  eepromBootCountSensor.setValue(static_cast<uint32_t>(persistenceState.bootCount), force);
  eepromChecksumSensor.setValue(static_cast<uint32_t>(persistenceState.checksum), force);

  rtcEpochSensor.setValue(static_cast<uint32_t>(rtcState.present ? rtcState.now.unixtime() : 0UL), force);
  rtcAlarm1SeenSensor.setValue(static_cast<uint32_t>(rtcState.alarm1Seen), force);
  rtcAlarm2SeenSensor.setValue(static_cast<uint32_t>(rtcState.alarm2Seen), force);
  rtcInterruptSeenSensor.setValue(static_cast<uint32_t>(rtcState.isrSeen), force);
  rtcAlarmClearsSensor.setValue(static_cast<uint32_t>(rtcState.clears), force);

  if (!isnan(shtState.temperature)) {
    temperatureSensor.setValue(shtState.temperature, force);
  }
  if (!isnan(shtState.humidity)) {
    humiditySensor.setValue(shtState.humidity, force);
  }
  fanRpmSensor.setValue(static_cast<int32_t>(fan.getRPM()), force);
  fanTachPulsesSensor.setValue(static_cast<uint32_t>(fan.getTotalTachPulses()), force);
  shtAlertInterruptsSensor.setValue(static_cast<uint32_t>(shtState.irqCount), force);
  shtMeasurementErrorsSensor.setValue(static_cast<uint32_t>(shtState.measurementErrors), force);
  shtStatusErrorsSensor.setValue(static_cast<uint32_t>(shtState.statusErrors), force);
  shtThresholdResultSensor.setValue(thresholdFeedback);
  if (pendingEventCount == 0) {
    testEventSensor.setValue(lastTestEvent);
  }
  publishShtDiagnostic();

  eepromFaultSensor.setState(eepromHasFault(), force);
  rtcFaultSensor.setState(!(rtcState.present && rtcState.alarm1Configured && rtcState.alarm2Configured), force);
  fanFaultSensor.setState(fan.hasFault(), force);
  shtFaultSensor.setState(shtHasFault(), force);
  shtAlertLineSensor.setState(shtState.alertLineActive, force);
  shtInterruptAttachedSensor.setState(shtState.alertInterruptAttached, force);
  rtcLostPowerSensor.setState(rtcState.lostPower, force);
  alarm1ConfiguredSensor.setState(rtcState.alarm1Configured, force);
  alarm2ConfiguredSensor.setState(rtcState.alarm2Configured, force);
  fanSafeSensor.setState(digitalRead(PIN_FAN_SWITCH) == FAN_OFF_LEVEL, force);
  relaySafeSensor.setState(digitalRead(PIN_LIGHT_POWER) == LIGHT_RELAY_OPEN_LEVEL, force);
  shdnSafeSensor.setState(digitalRead(PIN_LIGHT_DIM_SHDN) == LIGHT_DIM_SHDN_ASSERTED_LEVEL, force);
  fanSwitch.setState(fan.getManualState(), force);
  fanAutoModeSwitch.setState(fan.isAutoMode(), force);
  publishThresholdStates(force);

  if (serialAvailable()) {
    Serial.println(F("[MQTT] Published all HA states."));
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
  retainedEntityCleanup.service(
      mqtt, MQTT_PREFIX, DEVICE_ID, MQTT_DATA_PREFIX);
  const bool mqttConnected = mqtt.isConnected();
  if (mqttConnected && !mqttWasConnected) {
    mqttWasConnected = true;
    if (serialAvailable()) {
      Serial.println(F("[MQTT] Connected."));
    }
    publishHaState(nowMs, true);
    publishTestEvent("ha_mqtt_connected");
  } else if (!mqttConnected && mqttWasConnected) {
    mqttWasConnected = false;
    if (serialAvailable()) {
      Serial.print(F("[MQTT] Disconnected, state="));
      Serial.println(static_cast<int>(mqtt.getState()));
    }
  }
  cleanupRetiredTopics();
  publishHaBootIdentity();
  publishHaState(nowMs, false);
  flushOnePendingEvent();
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
  Serial.print(F(", ha="));
  Serial.print(mqtt.isConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", t="));
  Serial.print(shtState.temperature, 2);
  Serial.print(F(", rh="));
  Serial.print(shtState.humidity, 1);
  Serial.print(F(", sht_status=0x"));
  Serial.print(shtState.statusRegister, HEX);
  Serial.print(F(", auto_demand="));
  Serial.print((shtState.tempHighDemand || shtState.humHighDemand) ? F("ON") : F("OFF"));
  Serial.print(F(", fan="));
  Serial.print(fan.isOn() ? F("ON") : F("OFF"));
  Serial.print(F(", rpm="));
  Serial.print(fan.getRPM());
  Serial.print(F(", fan_fault="));
  Serial.print(fan.hasFault() ? F("YES") : F("NO"));
  Serial.print(F(", eeprom_seq="));
  Serial.print(persistenceState.sequence);
  Serial.print(F(", rtc_isr="));
  Serial.print(rtcState.isrSeen);
  Serial.print(F(", ota_gap_violations="));
  Serial.println(otaPollGapViolations);
}

}  // namespace

void setup() {
  // Secure every actuator before Serial, Wire, storage, sensor, or networking initialization.
  configurePinsForSafeState();
  enforceSafeOutputs();

  Serial.begin(115200);
  // Native USB Serial is optional. Never wait for a host to open the port.

  const int shtInterruptId = digitalPinToInterrupt(PIN_SHT_ALERT);
  shtState.alertInterruptAttached = pinSupportsExternalInterrupt(PIN_SHT_ALERT);
  if (shtState.alertInterruptAttached) {
    attachInterrupt(shtInterruptId, onShtAlert, RISING);
  } else if (serialAvailable()) {
    Serial.println(F("[SHT] A7 has no attachInterrupt mapping; interrupt validation cannot continue."));
  }

  Wire.begin();
  initializePersistence();
  loadActiveThresholds();
  initializeRtc();
  initializeSht();

  fan.begin();
  fan.setManualState(false);
  fan.setAutoMode(activeRecord.fanAutoMode != 0);
  fan.setAutoDemand(!shtHasFault() && (shtState.tempHighDemand || shtState.humHighDemand));
  fan.update(millis());

  const int rtcInterruptId = digitalPinToInterrupt(PIN_RTC_ALARM);
  if (pinSupportsExternalInterrupt(PIN_RTC_ALARM)) {
    attachInterrupt(rtcInterruptId, onRtcAlarm, FALLING);
  }
  // FanController owns the tach interrupt and only counts pulses in its ISR.

  configureHomeAssistant();
  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  publishTestEvent("boot_complete");
  nextWifiAttemptMs = millis();

  if (serialAvailable()) {
    Serial.println();
    Serial.println(F("Grow Controller SHT Alert Fan Closed-Loop Test"));
    Serial.println(F("Runtime order: safe outputs -> EEPROM/RTC -> SHT limits/status -> fan -> WiFi/OTA/MQTT"));
  }
}

void loop() {
  serviceOta();
  uint32_t nowMs = millis();
  serviceRtc(nowMs);
  serviceSht(nowMs);
  fan.update(nowMs);
  reportStateTransitions();

  serviceWifi(nowMs);
  serviceOta();
  nowMs = millis();
  serviceMqtt(nowMs);
  fan.update(millis());
  reportStateTransitions();
  serviceOta();
  printStatus(millis());
}
