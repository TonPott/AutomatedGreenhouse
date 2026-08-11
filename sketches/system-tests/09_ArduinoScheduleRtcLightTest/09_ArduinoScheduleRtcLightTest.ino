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
#include <SensirionI2cSht3x.h>

#include "Config.h"
#include "Ad5263SafeController.h"
#include "Credentials.h"
#include "FanController.h"
#include "MoistureSensor.h"

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

constexpr char TEST_ID[] = "arduino_schedule_rtc_light";
constexpr char SKETCH_NAME[] = "09_ArduinoScheduleRtcLightTest";
constexpr char SKETCH_VERSION[] = "1.0.1";
constexpr char DEVICE_ID[] = "grow_controller_tests_persistence_rtc";
constexpr char DEVICE_NAME[] = "Grow Controller Tests";
constexpr char MQTT_DATA_PREFIX[] = "smaeenhouse/test/persistence_rtc_baseline/ha";
constexpr char MQTT_STATUS_TOPIC[] = "smaeenhouse/test/arduino_schedule_rtc_light/status";
constexpr char MQTT_EVENT_TOPIC[] = "smaeenhouse/test/arduino_schedule_rtc_light/event";
constexpr char MQTT_COMMAND_TOPIC[] = "smaeenhouse/test/arduino_schedule_rtc_light/cmd";
constexpr char DIAGNOSTIC_MQTT_CLIENT_ID[] = "arduino_schedule_rtc_light_diag";

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
constexpr uint16_t EEPROM_TEST_VERSION = 2;
constexpr uint16_t EEPROM_LEGACY_VERSION = 1;

constexpr uint32_t NETWORK_OPERATION_TIMEOUT_MS = 1000UL;
constexpr uint32_t WIFI_SETTLE_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr uint8_t WIFI_TIMEOUTS_BEFORE_MODULE_RESET = 3;
constexpr uint32_t OTA_MAX_POLL_GAP_MS = 2000UL;
constexpr uint32_t HA_PUBLISH_INTERVAL_MS = 30000UL;
constexpr uint32_t RTC_SERVICE_INTERVAL_MS = 1000UL;
constexpr uint32_t SHT_SAMPLE_INTERVAL_MS = 2000UL;
constexpr uint32_t SHT_STATUS_INTERVAL_MS = 5000UL;
constexpr uint32_t DIAGNOSTIC_PUBLISH_INTERVAL_MS = 10000UL;
constexpr uint32_t DIAGNOSTIC_RECONNECT_INTERVAL_MS = 5000UL;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 30000UL;
constexpr uint16_t HA_ENTITY_LIMIT = 72;
constexpr size_t DIAGNOSTIC_PACKET_BUFFER_SIZE = 2560;
constexpr uint32_t LIGHT_ACTION_STEP_INTERVAL_MS = 250UL;
constexpr uint8_t RETAINED_TOPIC_CLEANUP_COUNT = 6;
constexpr uint8_t RETIRED_IDENTITY_ENTITY_COUNT = 2;

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

const char* const RETAINED_TOPICS_TO_CLEAR[RETAINED_TOPIC_CLEANUP_COUNT] = {
  "smaeenhouse/test/safe_installed_baseline/status",
  "smaeenhouse/test/i2c_passive_baseline/status",
  "smaeenhouse/test/sht_hardware_baseline/status",
  "smaeenhouse/test/persistence_rtc_baseline/status",
  "smaeenhouse/test/persistence_rtc_baseline/event",
  "smaeenhouse/test/persistence_rtc_baseline/result"
};

const char* const RETIRED_IDENTITY_ENTITY_IDS[RETIRED_IDENTITY_ENTITY_COUNT] = {
  "sketch_name",
  "sketch_version"
};

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

enum class LightActionState : uint8_t {
  Idle,
  OpenRelay,
  AssertShutdown,
  ApplyTarget,
  ReleaseShutdown,
  CloseRelay,
  HardOffOpenRelay,
  HardOffVerifyTarget,
  Complete,
  Failed
};

enum class PendingLightCommand : uint8_t {
  None,
  SetPower,
  SetBrightness,
  SetAutoMode,
  SetHardPowerOff
};
struct TestRecordHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t length;
};

struct LegacyTestRecordV1 {
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
  uint8_t lightOnTargetPercent;
  uint8_t lightOffTargetPercent;
  uint16_t checksum;
};

struct PersistenceState {
  bool present = false;
  bool readOk = false;
  bool writeOk = false;
  bool magicOk = false;
  bool checksumOk = false;
  uint32_t reads = 0;
  uint32_t writes = 0;
  uint32_t skipped = 0;
  uint32_t sequence = 0;
  uint32_t bootCount = 0;
  uint16_t checksum = 0;
  uint8_t lastError = 0;
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
  uint32_t nextAlarm1Epoch = 0;
  uint32_t nextAlarm2Epoch = 0;
  uint8_t lastError = 0;
  DateTime now;
};

struct ScheduledDimState {
  bool active = false;
  uint8_t startPercent = 0;
  uint8_t targetPercent = 0;
  uint8_t lastRequestedPercent = 0;
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
  uint8_t progressPercent = 0;
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
  bool alertSummary = false;
  bool tempTrackingAlert = false;
  bool humTrackingAlert = false;
  bool resetDetected = false;
  bool commandError = false;
  bool crcError = false;
  bool alertLineLow = false;
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
  uint32_t samples = 0;
  uint32_t irqCount = 0;
  uint32_t applyCount = 0;
  uint32_t rejectedThresholdCommands = 0;
  ShtLimit highSet;
  ShtLimit highClear;
  ShtLimit lowSet;
  ShtLimit lowClear;
};

WiFiClient networkClient;
WiFiClient diagnosticNetworkClient;
PubSubClient diagnosticMqtt(diagnosticNetworkClient);
HADevice device(DEVICE_ID);
HAMqtt mqtt(networkClient, device, HA_ENTITY_LIMIT);
JC_EEPROM eeprom(JC_EEPROM::kbits_32, 1, EEPROM_PAGE_SIZE, I2C_ADDRESS_AT24C32);
RTC_DS3231 rtc;
SensirionI2cSht3x shtSensor;
FanController fan;
MoistureSensor moisture(PIN_SOIL_SENSOR);
Ad5263SafeController ad5263;

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
HASensorNumber soilRawSensor("soil_moisture_raw");
HASensor soilPercentSensor("soil_moisture_percent");
HASensor sketchIdentitySensor("sketch_identity");
HASensor ad5263TestStepSensor("ad5263_test_step");
HASensorNumber ad5263StepIndexSensor("ad5263_step_index");
HASensorNumber ad5263BrightnessSensor("ad5263_brightness_percent");
HASensorNumber ad5263ExpectedW2Sensor("ad5263_expected_w2");
HASensorNumber ad5263ExpectedW1Sensor("ad5263_expected_w1");
HASensorNumber ad5263ReadbackW2Sensor("ad5263_readback_w2");
HASensorNumber ad5263ReadbackW1Sensor("ad5263_readback_w1");
HASensor scheduleEventSensor("light_schedule_event");
HASensor scheduleStateSensor("light_schedule_state");
HASensorNumber scheduleProgressSensor("light_schedule_progress_percent");
HASensorNumber rtcAlarm1NextEpochSensor("light_alarm1_next_epoch");
HASensorNumber rtcAlarm2NextEpochSensor("light_alarm2_next_epoch");
HASensor lightFaultReasonSensor("light_fault_reason");
HASensor shtThresholdResultSensor("sht_threshold_result");
HASensor shtDiagnosticSensor("sht_diagnostic");

HABinarySensor eepromFaultSensor("eeprom_fault");
HABinarySensor rtcFaultSensor("rtc_fault");
HABinarySensor fanFaultSensor("fan_fault");
HABinarySensor shtFaultSensor("sht_fault");
HABinarySensor lightFaultSensor("light_fault");
HABinarySensor rtcLostPowerSensor("persistence_rtc_lost_power");
HABinarySensor alarm1ConfiguredSensor("persistence_rtc_alarm1_configured");
HABinarySensor alarm2ConfiguredSensor("persistence_rtc_alarm2_configured");
HABinarySensor fanSafeSensor("persistence_rtc_fan_safe");
HABinarySensor relaySafeSensor("persistence_rtc_relay_safe");
HABinarySensor shdnSafeSensor("persistence_rtc_shdn_safe");
HASwitch fanSwitch("fan");
HASwitch fanAutoModeSwitch("fan_auto_mode");
HALight growLight("grow_light", HALight::BrightnessFeature);
HASwitch lightAutoModeSwitch("light_auto_mode");
HASwitch lightHardPowerOffSwitch("light_hard_power_off");
HANumber tempHighSetNumber("temp_high_set", HANumber::PrecisionP1);
HANumber tempHighClearNumber("temp_high_clear", HANumber::PrecisionP1);
HANumber tempLowSetNumber("temp_low_set", HANumber::PrecisionP1);
HANumber tempLowClearNumber("temp_low_clear", HANumber::PrecisionP1);
HANumber humHighSetNumber("hum_high_set", HANumber::PrecisionP1);
HANumber humHighClearNumber("hum_high_clear", HANumber::PrecisionP1);
HANumber humLowSetNumber("hum_low_set", HANumber::PrecisionP1);
HANumber humLowClearNumber("hum_low_clear", HANumber::PrecisionP1);
HANumber soilAirNumber("soil_air");
HANumber soilWaterNumber("soil_water");
HANumber soilDepthNumber("soil_depth_mm");
HANumber lightOnTimeNumber("light_on_time_minutes");
HANumber lightOffTimeNumber("light_off_time_minutes");
HANumber lightOnTargetNumber("light_on_target_percent");
HANumber lightOffTargetNumber("light_off_target_percent");
HANumber lightDimMinutesNumber("light_dim_minutes");
HAButton readSoilRawButton("read_soil_raw_value");

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
uint32_t lastSoilSampleMs = 0;
uint32_t lastDiagnosticPublishMs = 0;
uint32_t nextDiagnosticMqttAttemptMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t safeStateEnforceCount = 0;
uint32_t thresholdCommandCount = 0;
uint32_t soilSampleCount = 0;
uint32_t soilButtonReadCount = 0;
uint32_t soilCommandCount = 0;
uint32_t soilInvalidSampleCount = 0;
char thresholdFeedback[112] = "boot: SHT initialization pending";
bool retainedTopicsCleared = false;
bool diagnosticMqttWasConnected = false;
bool bootIdentityPublished = false;
bool haSketchIdentityPublished = false;
bool lastReportedFanOn = false;
bool lastReportedAutoDemand = false;
bool lastReportedFanFault = false;
bool lastReportedTempLow = false;
bool lastReportedHumLow = false;
bool lastReportedUnexplainedSummary = false;
bool lastReportedSoilValidityKnown = false;
bool lastReportedSoilValid = false;
bool lastReportedLightFault = false;
bool ad5263HaStateDirty = true;
uint32_t ad5263StepIndex = 0;
char ad5263LastStep[80] = "0:not_started";
ScheduledDimState scheduledDim;
bool scheduleHaStateDirty = true;
uint32_t scheduleEventIndex = 0;
char scheduleLastEvent[96] = "0:not_started";

LightActionState lightActionState = LightActionState::Idle;
PendingLightCommand pendingLightCommand = PendingLightCommand::None;
bool pendingLightBool = false;
uint8_t pendingLightBrightness = 0;
bool lightAutoMode = true;
bool lightHardPowerOff = false;
uint8_t lightActionTargetPercent = 0;
uint8_t lastNonZeroLightBrightness = 100;
uint32_t lightActionStageStartedMs = 0;
PersistenceState persistenceState;
RtcState rtcState;
ShtState shtState;
ThresholdConfig activeThresholds;
TestRecord activeRecord;

void publishDiagnosticEvent(const char* eventName);
void publishThresholdStates();
void publishShtDiagnostic();
void publishSoilStates(bool force);
void publishSoilConfigStates();
bool thresholdConfigValid(const ThresholdConfig& config);
void publishAd5263TestStep(const char* step);
void publishAd5263HaState(bool force);
void publishScheduleEvent(const char* eventName);
void publishScheduleStates(bool force);
void serviceScheduledDim(uint32_t nowMs);
void startLightBrightnessAction(uint8_t targetPercent, uint32_t nowMs);
void startScheduledDim(uint8_t targetPercent, uint32_t nowMs, const char* eventName);
void cancelScheduledDim(const char* reason);

void serviceLightControl(uint32_t nowMs);
void onGrowLightStateCommand(bool state, HALight* sender);
void onGrowLightBrightnessCommand(uint8_t brightness, HALight* sender);
void onLightAutoModeCommand(bool state, HASwitch* sender);
void onLightHardPowerOffCommand(bool state, HASwitch* sender);
bool persistLightAutoMode(bool enabled);
bool persistScheduleConfiguration(uint16_t onMinutes,
                                  uint16_t offMinutes,
                                  uint8_t onTargetPercent,
                                  uint8_t offTargetPercent,
                                  uint16_t dimMinutes);
bool serialAvailable() {
  return static_cast<bool>(Serial);
}

template <typename RecordType>
uint16_t checksumRecord(const RecordType& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  const size_t checksumOffset = offsetof(RecordType, checksum);
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
  record.soilAir = DEFAULT_SOIL_AIR;
  record.soilWater = DEFAULT_SOIL_WATER;
  record.soilDepthMm = DEFAULT_SOIL_DEPTH_MM;
  record.lightOnTargetPercent = DEFAULT_LIGHT_ON_TARGET_PERCENT;
  record.lightOffTargetPercent = DEFAULT_LIGHT_OFF_TARGET_PERCENT;
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

  pinMode(PIN_LIGHT_POWER, OUTPUT);
  digitalWrite(PIN_LIGHT_POWER, LIGHT_RELAY_OPEN_LEVEL);

  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);

  pinMode(PIN_SHT_ALERT, INPUT_PULLUP);
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

bool readRecord(TestRecord& record) {
  persistenceState.reads++;
  const uint8_t result = eeprom.read(EEPROM_TEST_BASE, reinterpret_cast<uint8_t*>(&record), sizeof(record));
  if (result != 0) {
    persistenceState.lastError = result;
    persistenceState.readOk = false;
    return false;
  }
  persistenceState.readOk = true;
  persistenceState.lastError = 0;
  return true;
}

bool writeRecordIfChanged(const TestRecord& record) {
  TestRecord current{};
  if (!readRecord(current)) {
    return false;
  }

  if (memcmp(&current, &record, sizeof(record)) == 0) {
    persistenceState.skipped++;
    persistenceState.writeOk = true;
    return true;
  }

  for (uint16_t index = 0; index < sizeof(record); ++index) {
    const uint8_t value = reinterpret_cast<const uint8_t*>(&record)[index];
    const uint8_t result = eeprom.update(EEPROM_TEST_BASE + index, value);
    if (result != 0) {
      persistenceState.lastError = result;
      persistenceState.writeOk = false;
      return false;
    }
  }

  persistenceState.writes++;
  persistenceState.writeOk = true;
  persistenceState.lastError = 0;
  return true;
}

void initializePersistence() {
  persistenceState.present = (eeprom.begin(JC_EEPROM::twiClock100kHz) == 0) && probeI2cAddress(I2C_ADDRESS_AT24C32);
  if (!persistenceState.present) {
    persistenceState.lastError = 1;
    activeRecord = makeDefaultRecord(1, 1);
    return;
  }

  TestRecordHeader header{};
  persistenceState.reads++;
  const bool headerReadOk = eeprom.read(EEPROM_TEST_BASE,
                                        reinterpret_cast<uint8_t*>(&header),
                                        sizeof(header)) == 0;
  bool sourceValid = false;

  if (headerReadOk && header.magic == EEPROM_TEST_MAGIC &&
      header.version == EEPROM_TEST_VERSION && header.length == sizeof(TestRecord)) {
    TestRecord stored{};
    const bool readOk = readRecord(stored);
    sourceValid = readOk && stored.checksum == checksumRecord(stored);
    if (sourceValid) {
      activeRecord = stored;
      activeRecord.sequence++;
      activeRecord.bootCount++;
      activeRecord.checksum = checksumRecord(activeRecord);
    }
  } else if (headerReadOk && header.magic == EEPROM_TEST_MAGIC &&
             header.version == EEPROM_LEGACY_VERSION && header.length == sizeof(LegacyTestRecordV1)) {
    LegacyTestRecordV1 legacy{};
    persistenceState.reads++;
    const bool readOk = eeprom.read(EEPROM_TEST_BASE,
                                    reinterpret_cast<uint8_t*>(&legacy),
                                    sizeof(legacy)) == 0;
    sourceValid = readOk && legacy.checksum == checksumRecord(legacy);
    if (sourceValid) {
      activeRecord.magic = EEPROM_TEST_MAGIC;
      activeRecord.version = EEPROM_TEST_VERSION;
      activeRecord.length = sizeof(TestRecord);
      activeRecord.sequence = legacy.sequence + 1U;
      activeRecord.bootCount = legacy.bootCount + 1U;
      activeRecord.fanAutoMode = legacy.fanAutoMode;
      activeRecord.lightAutoMode = legacy.lightAutoMode;
      activeRecord.fallbackMode = legacy.fallbackMode;
      activeRecord.lightOnMinutes = legacy.lightOnMinutes;
      activeRecord.lightOffMinutes = legacy.lightOffMinutes;
      activeRecord.dimMinutes = legacy.dimMinutes;
      activeRecord.tempHighSet = legacy.tempHighSet;
      activeRecord.tempHighClear = legacy.tempHighClear;
      activeRecord.humHighSet = legacy.humHighSet;
      activeRecord.humHighClear = legacy.humHighClear;
      activeRecord.soilAir = legacy.soilAir;
      activeRecord.soilWater = legacy.soilWater;
      activeRecord.soilDepthMm = legacy.soilDepthMm;
      activeRecord.lightOnTargetPercent = DEFAULT_LIGHT_ON_TARGET_PERCENT;
      activeRecord.lightOffTargetPercent = DEFAULT_LIGHT_OFF_TARGET_PERCENT;
      activeRecord.checksum = checksumRecord(activeRecord);
    }
  }

  persistenceState.magicOk = sourceValid;
  persistenceState.checksumOk = sourceValid;
  if (!sourceValid) {
    activeRecord = makeDefaultRecord(1, 1);
  }

  if (writeRecordIfChanged(activeRecord)) {
    TestRecord verified{};
    if (readRecord(verified)) {
      persistenceState.magicOk = verified.magic == EEPROM_TEST_MAGIC &&
                                 verified.version == EEPROM_TEST_VERSION &&
                                 verified.length == sizeof(TestRecord);
      persistenceState.checksum = verified.checksum;
      persistenceState.checksumOk = persistenceState.magicOk &&
                                    verified.checksum == checksumRecord(verified);
      persistenceState.sequence = verified.sequence;
      persistenceState.bootCount = verified.bootCount;
    }
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
  if (activeRecord.fanAutoMode == static_cast<uint8_t>(enabled)) {
    persistenceState.skipped++;
    return true;
  }
  if (!persistenceState.present) {
    return false;
  }

  TestRecord updated = activeRecord;
  updated.sequence++;
  updated.fanAutoMode = enabled ? 1U : 0U;
  updated.checksum = checksumRecord(updated);
  if (!writeRecordIfChanged(updated)) {
    return false;
  }

  TestRecord verified{};
  if (!readRecord(verified) || memcmp(&updated, &verified, sizeof(updated)) != 0 ||
      verified.checksum != checksumRecord(verified)) {
    persistenceState.checksumOk = false;
    return false;
  }

  activeRecord = verified;
  persistenceState.magicOk = true;
  persistenceState.checksumOk = true;
  persistenceState.sequence = verified.sequence;
  persistenceState.bootCount = verified.bootCount;
  persistenceState.checksum = verified.checksum;
  return true;
}
bool persistLightAutoMode(bool enabled) {
  if (activeRecord.lightAutoMode == static_cast<uint8_t>(enabled)) {
    persistenceState.skipped++;
    return true;
  }
  if (!persistenceState.present) {
    return false;
  }

  TestRecord updated = activeRecord;
  updated.sequence++;
  updated.lightAutoMode = enabled ? 1U : 0U;
  updated.checksum = checksumRecord(updated);
  if (!writeRecordIfChanged(updated)) {
    return false;
  }

  TestRecord verified{};
  if (!readRecord(verified) || memcmp(&updated, &verified, sizeof(updated)) != 0 ||
      verified.checksum != checksumRecord(verified)) {
    persistenceState.checksumOk = false;
    return false;
  }

  activeRecord = verified;
  persistenceState.magicOk = true;
  persistenceState.checksumOk = true;
  persistenceState.sequence = verified.sequence;
  persistenceState.bootCount = verified.bootCount;
  persistenceState.checksum = verified.checksum;
  return true;
}
bool persistScheduleConfiguration(uint16_t onMinutes,
                                  uint16_t offMinutes,
                                  uint8_t onTargetPercent,
                                  uint8_t offTargetPercent,
                                  uint16_t dimMinutes) {
  if (activeRecord.lightOnMinutes == onMinutes &&
      activeRecord.lightOffMinutes == offMinutes &&
      activeRecord.lightOnTargetPercent == onTargetPercent &&
      activeRecord.lightOffTargetPercent == offTargetPercent &&
      activeRecord.dimMinutes == dimMinutes) {
    persistenceState.skipped++;
    return true;
  }
  if (!persistenceState.present) {
    return false;
  }

  TestRecord updated = activeRecord;
  updated.sequence++;
  updated.lightOnMinutes = onMinutes;
  updated.lightOffMinutes = offMinutes;
  updated.lightOnTargetPercent = onTargetPercent;
  updated.lightOffTargetPercent = offTargetPercent;
  updated.dimMinutes = dimMinutes;
  updated.checksum = checksumRecord(updated);
  if (!writeRecordIfChanged(updated)) {
    return false;
  }

  TestRecord verified{};
  if (!readRecord(verified) || memcmp(&updated, &verified, sizeof(updated)) != 0 ||
      verified.checksum != checksumRecord(verified)) {
    persistenceState.checksumOk = false;
    return false;
  }

  activeRecord = verified;
  persistenceState.magicOk = true;
  persistenceState.checksumOk = true;
  persistenceState.sequence = verified.sequence;
  persistenceState.bootCount = verified.bootCount;
  persistenceState.checksum = verified.checksum;
  return true;
}

bool persistSoilCalibration(int16_t soilAir, int16_t soilWater, int16_t soilDepthMm) {
  if (activeRecord.soilAir == soilAir && activeRecord.soilWater == soilWater &&
      activeRecord.soilDepthMm == soilDepthMm) {
    persistenceState.skipped++;
    return true;
  }
  if (!persistenceState.present) {
    return false;
  }

  TestRecord updated = activeRecord;
  updated.sequence++;
  updated.soilAir = soilAir;
  updated.soilWater = soilWater;
  updated.soilDepthMm = soilDepthMm;
  updated.checksum = checksumRecord(updated);
  if (!writeRecordIfChanged(updated)) {
    return false;
  }

  TestRecord verified{};
  if (!readRecord(verified) || memcmp(&updated, &verified, sizeof(updated)) != 0 ||
      verified.checksum != checksumRecord(verified)) {
    persistenceState.checksumOk = false;
    return false;
  }

  activeRecord = verified;
  persistenceState.magicOk = true;
  persistenceState.checksumOk = true;
  persistenceState.sequence = verified.sequence;
  persistenceState.bootCount = verified.bootCount;
  persistenceState.checksum = verified.checksum;
  return true;
}

DateTime buildNextAlarmTime(uint16_t minutesSinceMidnight, const DateTime& current) {
  const uint8_t hour = static_cast<uint8_t>(minutesSinceMidnight / 60U);
  const uint8_t minute = static_cast<uint8_t>(minutesSinceMidnight % 60U);
  DateTime candidate(current.year(), current.month(), current.day(), hour, minute, 0);
  if (candidate <= current) {
    candidate = candidate + TimeSpan(1, 0, 0, 0);
  }
  return candidate;
}

bool configureAlarm1ForNextEvent() {
  const DateTime next = buildNextAlarmTime(activeRecord.lightOnMinutes, rtc.now());
  rtc.clearAlarm(1);
  rtc.disableAlarm(1);
  const bool configured = rtc.setAlarm1(next, DS3231_A1_Date);
  rtcState.nextAlarm1Epoch = configured ? next.unixtime() : 0UL;
  return configured;
}

bool configureAlarm2ForNextEvent() {
  const DateTime next = buildNextAlarmTime(activeRecord.lightOffMinutes, rtc.now());
  rtc.clearAlarm(2);
  rtc.disableAlarm(2);
  const bool configured = rtc.setAlarm2(next, DS3231_A2_Date);
  rtcState.nextAlarm2Epoch = configured ? next.unixtime() : 0UL;
  return configured;
}

void configureRtcAlarms() {
  if (!rtcState.present) {
    return;
  }

  rtc.disableAlarm(1);
  rtc.disableAlarm(2);
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);

  rtcState.now = rtc.now();
  rtcState.lostPower = rtc.lostPower();
  rtcState.alarm1Configured = configureAlarm1ForNextEvent();
  rtcState.alarm2Configured = configureAlarm2ForNextEvent();
  rtcState.lastError = (rtcState.alarm1Configured && rtcState.alarm2Configured) ? 0 : 2;
  scheduleHaStateDirty = true;
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
    publishScheduleEvent("rtc_alarm_interrupt_seen");
  }

  const bool alarm1Fired = rtc.alarmFired(1);
  const bool alarm2Fired = rtc.alarmFired(2);

  if (alarm1Fired) {
    rtcState.alarm1Seen++;
    rtc.clearAlarm(1);
    rtcState.clears++;
    rtcState.alarm1Configured = configureAlarm1ForNextEvent();
    if (lightAutoMode) {
      startScheduledDim(activeRecord.lightOnTargetPercent, nowMs, "alarm1_schedule_started");
    } else {
      publishScheduleEvent("alarm1_ignored_auto_mode_off");
    }
  }

  if (alarm2Fired) {
    rtcState.alarm2Seen++;
    rtc.clearAlarm(2);
    rtcState.clears++;
    rtcState.alarm2Configured = configureAlarm2ForNextEvent();
    if (lightAutoMode) {
      startScheduledDim(activeRecord.lightOffTargetPercent, nowMs, "alarm2_schedule_started");
    } else {
      publishScheduleEvent("alarm2_ignored_auto_mode_off");
    }
  }

  if (alarm1Fired || alarm2Fired) {
    rtcState.lastError = (rtcState.alarm1Configured && rtcState.alarm2Configured) ? 0 : 2;
    scheduleHaStateDirty = true;
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

bool clearShtStatus() {
  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(0x30);
  Wire.write(0x41);
  const bool ok = Wire.endTransmission() == 0;
  delayMicroseconds(50);
  return ok;
}

bool writeShtLimit(uint8_t commandLsb, float temperature, float humidity) {
  const uint16_t raw = encodeShtLimit(temperature, humidity);
  const uint8_t data[2] = {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFFU)};

  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(SHT31_ALERT_WRITE_MSB);
  Wire.write(commandLsb);
  Wire.write(data[0]);
  Wire.write(data[1]);
  Wire.write(shtCrc8(data, sizeof(data)));
  const bool ok = Wire.endTransmission() == 0;
  delayMicroseconds(50);
  return ok;
}

bool readShtLimit(uint8_t commandLsb, ShtLimit& limit) {
  limit.ok = false;
  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(SHT31_ALERT_READ_MSB);
  Wire.write(commandLsb);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delayMicroseconds(50);
  if (Wire.requestFrom(I2C_ADDRESS_SHT31, static_cast<uint8_t>(3)) != 3) {
    return false;
  }

  uint8_t data[2];
  data[0] = Wire.read();
  data[1] = Wire.read();
  const uint8_t receivedCrc = Wire.read();
  if (shtCrc8(data, sizeof(data)) != receivedCrc) {
    return false;
  }

  limit.raw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  limit.temperature = decodeShtLimitTemperature(limit.raw);
  limit.humidity = decodeShtLimitHumidity(limit.raw);
  limit.ok = true;
  return true;
}

bool limitMatches(const ShtLimit& limit, float temperature, float humidity) {
  return limit.ok && fabs(limit.temperature - temperature) <= TEMP_READBACK_TOLERANCE_C &&
         fabs(limit.humidity - humidity) <= HUM_READBACK_TOLERANCE_PERCENT;
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

bool applyShtThresholds(const ThresholdConfig& config, bool publishEvent) {
  shtState.limitsApplied = false;
  shtState.limitsVerified = false;
  if (!thresholdConfigValid(config) || !shtState.present) {
    if (publishEvent) {
      publishDiagnosticEvent("threshold_apply_invalid_order_or_sensor_missing");
    }
    return false;
  }

  // Alert-limit writes are performed while periodic acquisition is stopped.
  // Test 03 established periodic reads, but did not establish writes during acquisition.
  shtSensor.stopMeasurement();
  shtState.measurementRunning = false;
  delay(1);
  const bool clearBeforeOk = clearShtStatus();

  const bool highSetWriteOk = writeShtLimit(SHT31_ALERT_WHS, config.tempHighSet, config.humHighSet);
  const bool highClearWriteOk = writeShtLimit(SHT31_ALERT_WHC, config.tempHighClear, config.humHighClear);
  const bool lowSetWriteOk = writeShtLimit(SHT31_ALERT_WLS, config.tempLowSet, config.humLowSet);
  const bool lowClearWriteOk = writeShtLimit(SHT31_ALERT_WLC, config.tempLowClear, config.humLowClear);
  const bool writesOk = highSetWriteOk && highClearWriteOk && lowSetWriteOk && lowClearWriteOk;
  shtState.limitsApplied = writesOk;

  const bool highSetReadOk = readShtLimit(SHT31_ALERT_RHS, shtState.highSet);
  const bool highClearReadOk = readShtLimit(SHT31_ALERT_RHC, shtState.highClear);
  const bool lowSetReadOk = readShtLimit(SHT31_ALERT_RLS, shtState.lowSet);
  const bool lowClearReadOk = readShtLimit(SHT31_ALERT_RLC, shtState.lowClear);
  const bool readsOk = highSetReadOk && highClearReadOk && lowSetReadOk && lowClearReadOk;
  shtState.limitsVerified = writesOk && readsOk &&
      limitMatches(shtState.highSet, config.tempHighSet, config.humHighSet) &&
      limitMatches(shtState.highClear, config.tempHighClear, config.humHighClear) &&
      limitMatches(shtState.lowSet, config.tempLowSet, config.humLowSet) &&
      limitMatches(shtState.lowClear, config.tempLowClear, config.humLowClear);

  // Clear status after the write/readback transaction, then restore the Test 03
  // periodic measurement mode even when threshold verification failed.
  const bool clearAfterOk = clearShtStatus();
  const int16_t startError = shtSensor.startPeriodicMeasurement(REPEATABILITY_MEDIUM, MPS_ONE_PER_SECOND);
  shtState.measurementRunning = startError == NO_ERROR;
  shtState.initialized = shtState.present && shtState.measurementRunning;

  const bool success = clearBeforeOk && shtState.limitsVerified && clearAfterOk && shtState.measurementRunning;
  if (success) {
    shtState.applyCount++;
    shtState.commandError = false;
    shtState.crcError = false;
    if (publishEvent) {
      publishDiagnosticEvent("threshold_apply_verified");
    }
  } else if (publishEvent) {
    publishDiagnosticEvent(writesOk ? "threshold_readback_or_restart_failed" : "threshold_apply_write_failed");
  }
  return success;
}
bool shtHasFault() {
  return !(shtState.present && shtState.initialized && shtState.measurementRunning && shtState.measurementOk && shtState.statusOk &&
           shtState.limitsApplied && shtState.limitsVerified) || shtState.commandError || shtState.crcError;
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
  const int16_t error = shtSensor.blockingReadMeasurement(temperature, humidity);
  shtState.lastMeasurementError = error;
  shtState.samples++;
  shtState.measurementOk = error == NO_ERROR;
  if (shtState.measurementOk) {
    shtState.temperature = temperature;
    shtState.humidity = humidity;
  } else {
    forceSafeAutoDemand();
  }
  return shtState.measurementOk;
}

void evaluateShtDemand() {
  if (!shtState.measurementOk || !shtState.statusOk || !shtState.limitsVerified ||
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
  const int16_t error = shtSensor.readStatusRegister(status);
  shtState.lastStatusError = error;
  shtState.statusOk = error == NO_ERROR;
  shtState.alertLineLow = digitalRead(PIN_SHT_ALERT) == LOW;
  if (!shtState.statusOk) {
    forceSafeAutoDemand();
    return false;
  }

  shtState.statusRegister = status;
  shtState.alertSummary = (status & (1U << 15)) != 0;
  shtState.humTrackingAlert = (status & (1U << 11)) != 0;
  shtState.tempTrackingAlert = (status & (1U << 10)) != 0;
  shtState.resetDetected = (status & (1U << 4)) != 0;
  shtState.commandError = (status & (1U << 1)) != 0;
  shtState.crcError = (status & (1U << 0)) != 0;
  shtState.unexplainedSummary = shtState.alertSummary && !shtState.tempTrackingAlert &&
      !shtState.humTrackingAlert && !shtState.alertLineLow;

  if (shtState.resetDetected && handleReset) {
    publishDiagnosticEvent("sht_reset_detected");
    forceSafeAutoDemand();
    if (applyShtThresholds(activeThresholds, true)) {
      shtState.resetDetected = false;
      publishDiagnosticEvent("sht_reset_recovered");
      return readShtStatus(false);
    }
    shtState.statusOk = false;
    return false;
  }

  evaluateShtDemand();
  return true;
}

void initializeSht() {
  shtState.present = probeI2cAddress(I2C_ADDRESS_SHT31);
  if (!shtState.present) {
    setThresholdFeedback("boot failed: SHT31 not present at 0x45");
    forceSafeAutoDemand();
    return;
  }

  shtSensor.begin(Wire, I2C_ADDRESS_SHT31);
  shtSensor.stopMeasurement();
  delay(1);
  const int16_t resetError = shtSensor.softReset();
  delay(10);
  if (resetError != NO_ERROR) {
    shtState.initialized = false;
    setThresholdFeedback("boot failed: SHT soft reset");
    forceSafeAutoDemand();
    return;
  }

  if (!applyShtThresholds(activeThresholds, false)) {
    setThresholdFeedback("boot failed: limit write/readback or periodic restart");
    forceSafeAutoDemand();
    return;
  }

  setThresholdFeedback("boot: limits verified; measurement pending");
  delay(20);
  readShtMeasurement();
  readShtStatus(true);
  if (shtState.measurementOk && shtState.statusOk) {
    setThresholdFeedback("boot: limits and first SHT sample verified");
  }
}
void serviceSht(uint32_t nowMs) {
  bool irqPending = false;
  noInterrupts();
  irqPending = shtAlertPending;
  shtAlertPending = false;
  interrupts();
  if (irqPending) {
    shtState.irqCount++;
  }

  if (!shtState.initialized) {
    forceSafeAutoDemand();
    return;
  }

  const bool sampleDue = (nowMs - lastShtSampleMs) >= SHT_SAMPLE_INTERVAL_MS;
  if (sampleDue) {
    lastShtSampleMs = nowMs;
    readShtMeasurement();
  }

  if (irqPending || sampleDue || (nowMs - lastShtStatusMs) >= SHT_STATUS_INTERVAL_MS) {
    lastShtStatusMs = nowMs;
    readShtStatus(true);
  }
}
void serviceSoil(uint32_t nowMs) {
  if ((nowMs - lastSoilSampleMs) < SOIL_PUBLISH_INTERVAL_MS) {
    return;
  }

  lastSoilSampleMs = nowMs;
  moisture.sampleNow();
  soilSampleCount++;
  if (!moisture.isLastPercentValid()) {
    soilInvalidSampleCount++;
  }
  publishSoilStates(false);
}

void queueLightCommand(PendingLightCommand command, bool value, uint8_t brightness) {
  pendingLightCommand = command;
  pendingLightBool = value;
  pendingLightBrightness = brightness > 100 ? 100 : brightness;
}

void onGrowLightStateCommand(bool state, HALight*) {
  queueLightCommand(PendingLightCommand::SetPower, state, 0);
}

void onGrowLightBrightnessCommand(uint8_t brightness, HALight*) {
  queueLightCommand(PendingLightCommand::SetBrightness, false, brightness);
}

void onLightAutoModeCommand(bool state, HASwitch*) {
  queueLightCommand(PendingLightCommand::SetAutoMode, state, 0);
}

void onLightHardPowerOffCommand(bool state, HASwitch*) {
  queueLightCommand(PendingLightCommand::SetHardPowerOff, state, 0);
}

void publishScheduleEvent(const char* eventName) {
  scheduleEventIndex++;
  snprintf(scheduleLastEvent,
           sizeof(scheduleLastEvent),
           "%lu:%s",
           static_cast<unsigned long>(scheduleEventIndex),
           eventName != nullptr ? eventName : "unknown");
  scheduleHaStateDirty = true;
  publishAd5263TestStep(eventName != nullptr ? eventName : "schedule_event_unknown");
}

void cancelScheduledDim(const char* reason) {
  if (!scheduledDim.active) {
    return;
  }
  scheduledDim.active = false;
  publishScheduleEvent(reason != nullptr ? reason : "schedule_dim_cancelled");
}

void startScheduledDim(uint8_t targetPercent, uint32_t nowMs, const char* eventName) {
  if (scheduledDim.active) {
    cancelScheduledDim("schedule_dim_replaced");
  }

  scheduledDim.active = true;
  scheduledDim.startPercent = ad5263.getCurrentPercent();
  scheduledDim.targetPercent = targetPercent > 100 ? 100 : targetPercent;
  scheduledDim.lastRequestedPercent = scheduledDim.startPercent;
  scheduledDim.startMs = nowMs;
  scheduledDim.durationMs = static_cast<uint32_t>(activeRecord.dimMinutes) * 60000UL;
  scheduledDim.progressPercent = 0;
  publishScheduleEvent(eventName != nullptr ? eventName : "schedule_dim_started");
}

void serviceScheduledDim(uint32_t nowMs) {
  if (!scheduledDim.active) {
    return;
  }
  if (!lightAutoMode) {
    cancelScheduledDim("schedule_dim_cancelled_auto_mode_off");
    return;
  }
  if (lightActionState != LightActionState::Idle || pendingLightCommand != PendingLightCommand::None) {
    return;
  }

  if (scheduledDim.startPercent == scheduledDim.targetPercent) {
    scheduledDim.progressPercent = 100;
    scheduledDim.active = false;
    publishScheduleEvent("schedule_dim_complete_no_change");
    return;
  }

  const uint32_t elapsed = nowMs - scheduledDim.startMs;
  const bool durationComplete = scheduledDim.durationMs == 0 || elapsed >= scheduledDim.durationMs;
  uint8_t desiredPercent = scheduledDim.targetPercent;
  if (!durationComplete) {
    const int32_t delta = static_cast<int32_t>(scheduledDim.targetPercent) -
                          static_cast<int32_t>(scheduledDim.startPercent);
    const int64_t scaled = static_cast<int64_t>(delta) * static_cast<int64_t>(elapsed);
    const int32_t interpolated = static_cast<int32_t>(scheduledDim.startPercent) +
                                 static_cast<int32_t>(scaled / scheduledDim.durationMs);
    desiredPercent = static_cast<uint8_t>(constrain(interpolated, 0L, 100L));
    scheduledDim.progressPercent = static_cast<uint8_t>(constrain(
        static_cast<uint32_t>((static_cast<uint64_t>(elapsed) * 100ULL) / scheduledDim.durationMs),
        0UL,
        100UL));
  } else {
    scheduledDim.progressPercent = 100;
  }

  if (durationComplete && ad5263.getCurrentPercent() == scheduledDim.targetPercent) {
    scheduledDim.active = false;
    publishScheduleEvent("schedule_dim_complete");
    return;
  }

  // Timed jobs expose every one-percent step to HA even if a safe zero-crossing action temporarily
  // leaves the interpolation behind. Zero-duration jobs still jump directly to their target.
  if (scheduledDim.durationMs > 0) {
    if (desiredPercent > static_cast<uint8_t>(scheduledDim.lastRequestedPercent + 1U)) {
      desiredPercent = static_cast<uint8_t>(scheduledDim.lastRequestedPercent + 1U);
    } else if (scheduledDim.lastRequestedPercent > 0 &&
               desiredPercent + 1U < scheduledDim.lastRequestedPercent) {
      desiredPercent = static_cast<uint8_t>(scheduledDim.lastRequestedPercent - 1U);
    }
  }

  if (desiredPercent == scheduledDim.lastRequestedPercent) {
    return;
  }

  scheduledDim.lastRequestedPercent = desiredPercent;
  char eventName[48];
  if (desiredPercent > 0 && ad5263.getCurrentPercent() > 0 &&
      !ad5263.isRelayOpen() && ad5263.isShutdownReleased()) {
    if (!ad5263.applyBrightnessWhileOn(desiredPercent)) {
      scheduledDim.active = false;
      publishScheduleEvent("schedule_dim_live_apply_failed_safe");
      return;
    }
    lastNonZeroLightBrightness = desiredPercent;
    snprintf(eventName, sizeof(eventName), "schedule_dim_live_step_%u", desiredPercent);
  } else {
    startLightBrightnessAction(desiredPercent, nowMs);
    snprintf(eventName, sizeof(eventName), "schedule_dim_transition_step_%u", desiredPercent);
  }
  publishScheduleEvent(eventName);
}

void startLightBrightnessAction(uint8_t targetPercent, uint32_t nowMs) {
  lightActionTargetPercent = targetPercent > 100 ? 100 : targetPercent;
  lightActionState = LightActionState::OpenRelay;
  lightActionStageStartedMs = nowMs;
  publishAd5263TestStep(lightActionTargetPercent == 0
                            ? "light_off_requested"
                            : "light_brightness_requested");
}

void processPendingLightCommand(uint32_t nowMs) {
  if (pendingLightCommand == PendingLightCommand::None) {
    return;
  }

  const PendingLightCommand command = pendingLightCommand;
  const bool boolValue = pendingLightBool;
  const uint8_t brightness = pendingLightBrightness;
  pendingLightCommand = PendingLightCommand::None;

  if (command == PendingLightCommand::SetHardPowerOff && boolValue) {
    lightHardPowerOff = true;
    cancelScheduledDim("schedule_dim_cancelled_hard_power_off");
    lightActionState = LightActionState::Idle;
    ad5263.openRelay();
    publishAd5263TestStep("light_hard_off_immediate_relay_open_target_retained");
    return;
  }

  if (command == PendingLightCommand::SetAutoMode) {
    if (lightAutoMode == boolValue) {
      publishAd5263TestStep(lightAutoMode ? "light_auto_mode_already_on" : "light_auto_mode_already_off");
      return;
    }
    if (!persistLightAutoMode(boolValue)) {
      publishAd5263TestStep("light_auto_mode_persist_failed");
      return;
    }

    const bool actionWasActive = lightActionState != LightActionState::Idle;
    lightAutoMode = boolValue;
    cancelScheduledDim(lightAutoMode
                           ? "schedule_dim_cancelled_control_world_change"
                           : "schedule_dim_cancelled_auto_mode_off");
    if (actionWasActive) {
      lightActionState = LightActionState::Idle;
      ad5263.openRelay();
      ad5263.assertShutdown();
      publishAd5263TestStep("light_action_aborted_control_world_change_safe");
    }
    publishAd5263TestStep(lightAutoMode ? "light_auto_mode_on" : "light_auto_mode_off");
    return;
  }

  if (lightActionState != LightActionState::Idle) {
    publishAd5263TestStep("light_command_rejected_busy");
    return;
  }

  if (command == PendingLightCommand::SetHardPowerOff) {
    if (lightHardPowerOff == boolValue) {
      publishAd5263TestStep(boolValue ? "light_hard_off_already_on" : "light_hard_off_already_off");
      return;
    }

    lightHardPowerOff = false;
    lightActionStageStartedMs = nowMs;
    if (ad5263.getCurrentPercent() == 0) {
      lightActionState = LightActionState::Idle;
      publishAd5263TestStep("light_hard_off_released_light_off");
    } else {
      lightActionState = LightActionState::HardOffVerifyTarget;
      publishAd5263TestStep("light_hard_off_release_requested");
    }
    return;
  }

  if (lightAutoMode) {
    publishAd5263TestStep("light_manual_command_rejected_auto_mode");
    return;
  }

  if (command == PendingLightCommand::SetPower) {
    const uint8_t target = boolValue
                               ? (ad5263.getCurrentPercent() > 0
                                      ? ad5263.getCurrentPercent()
                                      : lastNonZeroLightBrightness)
                               : 0;
    startLightBrightnessAction(target, nowMs);
    return;
  }

  if (command == PendingLightCommand::SetBrightness) {
    startLightBrightnessAction(brightness, nowMs);
  }
}

void serviceLightControl(uint32_t nowMs) {
  processPendingLightCommand(nowMs);
  if (lightActionState == LightActionState::Idle ||
      (nowMs - lightActionStageStartedMs) < LIGHT_ACTION_STEP_INTERVAL_MS) {
    return;
  }
  lightActionStageStartedMs = nowMs;

  switch (lightActionState) {
    case LightActionState::OpenRelay:
      ad5263.openRelay();
      publishAd5263TestStep("light_relay_opened");
      lightActionState = LightActionState::AssertShutdown;
      break;

    case LightActionState::AssertShutdown:
      ad5263.assertShutdown();
      publishAd5263TestStep("light_shdn_asserted");
      lightActionState = LightActionState::ApplyTarget;
      break;

    case LightActionState::ApplyTarget:
      if (!ad5263.applyBrightness(lightActionTargetPercent)) {
        publishAd5263TestStep("light_target_apply_failed");
        lightActionState = LightActionState::Failed;
        break;
      }
      if (lightActionTargetPercent > 0) {
        lastNonZeroLightBrightness = lightActionTargetPercent;
      }
      publishAd5263TestStep("light_target_write_readback_ok");
      lightActionState = lightActionTargetPercent == 0
                             ? LightActionState::Complete
                             : LightActionState::ReleaseShutdown;
      break;

    case LightActionState::ReleaseShutdown:
      if (!ad5263.releaseShutdown()) {
        publishAd5263TestStep("light_shdn_release_failed");
        lightActionState = LightActionState::Failed;
        break;
      }
      publishAd5263TestStep(lightHardPowerOff
                                ? "light_shdn_released_hard_off_relay_open"
                                : "light_shdn_released_relay_open");
      lightActionState = lightHardPowerOff
                             ? LightActionState::Complete
                             : LightActionState::CloseRelay;
      break;

    case LightActionState::CloseRelay:
      if (!ad5263.closeRelay()) {
        publishAd5263TestStep("light_relay_close_failed");
        lightActionState = LightActionState::Failed;
        break;
      }
      publishAd5263TestStep("light_relay_closed_after_verified_target");
      lightActionState = LightActionState::Complete;
      break;

    case LightActionState::HardOffOpenRelay:
      ad5263.openRelay();
      publishAd5263TestStep("light_hard_off_relay_open_target_retained");
      lightActionState = LightActionState::Complete;
      break;

    case LightActionState::HardOffVerifyTarget:
      if (!ad5263.verifyCurrentTarget()) {
        publishAd5263TestStep("light_hard_off_release_readback_failed");
        lightActionState = LightActionState::Failed;
        break;
      }
      publishAd5263TestStep("light_hard_off_release_target_verified");
      lightActionState = LightActionState::ReleaseShutdown;
      break;

    case LightActionState::Complete:
      publishAd5263TestStep("light_action_complete");
      lightActionState = LightActionState::Idle;
      break;

    case LightActionState::Failed:
      ad5263.openRelay();
      ad5263.assertShutdown();
      publishAd5263TestStep("light_action_failed_safe");
      lightActionState = LightActionState::Idle;
      break;

    case LightActionState::Idle:
      break;
  }
}
void publishAd5263TestStep(const char* step) {
  ad5263StepIndex++;
  snprintf(ad5263LastStep,
           sizeof(ad5263LastStep),
           "%lu:%s",
           static_cast<unsigned long>(ad5263StepIndex),
           step != nullptr ? step : "unknown");
  ad5263HaStateDirty = true;
  publishDiagnosticEvent(step != nullptr ? step : "light_step_unknown");
}

void onDiagnosticMqttMessage(char*, byte*, unsigned int) {
  // Test 09 actuator and schedule commands are accepted only through Home Assistant entities.
}
bool publishBootIdentity() {
  if (bootIdentityPublished || !diagnosticMqtt.connected()) {
    return bootIdentityPublished;
  }

  char payload[240];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"event\":\"boot_identity\",\"sketch\":\"%s\",\"version\":\"%s\",\"boot\":%lu,\"sequence\":%lu,\"uptime_s\":%lu}",
           TEST_ID,
           SKETCH_NAME,
           SKETCH_VERSION,
           static_cast<unsigned long>(activeRecord.bootCount),
           static_cast<unsigned long>(activeRecord.sequence),
           static_cast<unsigned long>(millis() / 1000UL));
  bootIdentityPublished = diagnosticMqtt.publish(MQTT_EVENT_TOPIC, payload, false);
  if (bootIdentityPublished && serialAvailable()) {
    Serial.print(F("[MQTT] Published boot identity: "));
    Serial.print(SKETCH_NAME);
    Serial.print(F(" version "));
    Serial.println(SKETCH_VERSION);
  }
  return bootIdentityPublished;
}
void publishDiagnosticEvent(const char* eventName) {
  if (serialAvailable()) {
    Serial.print(F("[Event] "));
    Serial.println(eventName);
  }
  if (!diagnosticMqtt.connected()) {
    return;
  }

  char payload[180];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"uptime_s\":%lu,\"event\":\"%s\"}",
           TEST_ID,
           static_cast<unsigned long>(millis() / 1000UL),
           eventName);
  diagnosticMqtt.publish(MQTT_EVENT_TOPIC, payload, false);
}

void appendLimitJson(char* payload, size_t payloadSize, const char* name, const ShtLimit& limit) {
  char part[88];
  snprintf(part,
           sizeof(part),
           ",\"%s\":{\"ok\":%s,\"raw\":%u,\"t\":%.2f,\"rh\":%.1f}",
           name,
           limit.ok ? "true" : "false",
           limit.raw,
           static_cast<double>(limit.temperature),
           static_cast<double>(limit.humidity));
  strncat(payload, part, payloadSize - strlen(payload) - 1);
}

void publishDiagnosticStatus(uint32_t nowMs, bool force) {
  if (!diagnosticMqtt.connected()) {
    return;
  }
  if (!force && (nowMs - lastDiagnosticPublishMs) < DIAGNOSTIC_PUBLISH_INTERVAL_MS) {
    return;
  }
  lastDiagnosticPublishMs = nowMs;

  char payload[DIAGNOSTIC_PACKET_BUFFER_SIZE];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"uptime_s\":%lu,\"network\":{\"wifi\":%s,\"ha\":%s,\"joins\":%lu,\"timeouts\":%lu,\"module_resets\":%lu,\"ota_gap\":%lu},\"sht\":{\"present\":%s,\"running\":%s,\"measurement_ok\":%s,\"measurement_err\":%d,\"t\":%.2f,\"rh\":%.1f,\"status_ok\":%s,\"status_err\":%d,\"raw\":%u,\"summary\":%s,\"temp_tracking\":%s,\"rh_tracking\":%s,\"line_low\":%s,\"irq_attached\":%s,\"irq\":%lu,\"reset\":%s,\"cmd_err\":%s,\"crc_err\":%s,\"unexplained_summary\":%s,\"temp_reason\":\"%s\",\"rh_reason\":\"%s\"},\"fan\":{\"auto_mode\":%s,\"manual\":%s,\"auto_demand\":%s,\"effective\":%s,\"rpm\":%u,\"fault\":%s,\"tach_irq_attached\":%s,\"tach_level\":%d,\"tach_total\":%lu,\"tach_window\":%lu},\"limits\":{\"applied\":%s,\"verified\":%s,\"apply_count\":%lu,\"rejected\":%lu,\"feedback\":\"%s\"",
           TEST_ID,
           static_cast<unsigned long>(nowMs / 1000UL),
           WiFi.status() == WL_CONNECTED ? "true" : "false",
           mqtt.isConnected() ? "true" : "false",
           static_cast<unsigned long>(wifiConnectionCount),
           static_cast<unsigned long>(wifiConnectTimeoutCount),
           static_cast<unsigned long>(wifiModuleResetCount),
           static_cast<unsigned long>(otaPollGapViolations),
           shtState.present ? "true" : "false",
           shtState.measurementRunning ? "true" : "false",
           shtState.measurementOk ? "true" : "false",
           shtState.lastMeasurementError,
           static_cast<double>(shtState.temperature),
           static_cast<double>(shtState.humidity),
           shtState.statusOk ? "true" : "false",
           shtState.lastStatusError,
           shtState.statusRegister,
           shtState.alertSummary ? "true" : "false",
           shtState.tempTrackingAlert ? "true" : "false",
           shtState.humTrackingAlert ? "true" : "false",
           shtState.alertLineLow ? "true" : "false",
           shtState.alertInterruptAttached ? "true" : "false",
           static_cast<unsigned long>(shtState.irqCount),
           shtState.resetDetected ? "true" : "false",
           shtState.commandError ? "true" : "false",
           shtState.crcError ? "true" : "false",
           shtState.unexplainedSummary ? "true" : "false",
           shtState.tempHighDemand ? "high" : (shtState.tempLowObserved ? "low" : "none"),
           shtState.humHighDemand ? "high" : (shtState.humLowObserved ? "low" : "none"),
           fan.isAutoMode() ? "true" : "false",
           fan.getManualState() ? "true" : "false",
           (shtState.tempHighDemand || shtState.humHighDemand) ? "true" : "false",
           fan.isOn() ? "true" : "false",
           fan.getRPM(),
           fan.hasFault() ? "true" : "false",
           fan.isTachInterruptAttached() ? "true" : "false",
           fan.getTachInputLevel(),
           static_cast<unsigned long>(fan.getTotalTachPulses()),
           static_cast<unsigned long>(fan.getLastWindowTachPulses()),
           shtState.limitsApplied ? "true" : "false",
           shtState.limitsVerified ? "true" : "false",
           static_cast<unsigned long>(shtState.applyCount),
           static_cast<unsigned long>(shtState.rejectedThresholdCommands),
           thresholdFeedback);
  appendLimitJson(payload, sizeof(payload), "high_set", shtState.highSet);
  appendLimitJson(payload, sizeof(payload), "high_clear", shtState.highClear);
  appendLimitJson(payload, sizeof(payload), "low_set", shtState.lowSet);
  appendLimitJson(payload, sizeof(payload), "low_clear", shtState.lowClear);

  const Ad5263SafeController::RdacTarget expectedTarget = ad5263.getExpectedTarget();
  const Ad5263SafeController::RdacTarget readbackTarget = ad5263.getReadbackTarget();
  char suffix[1400];
  snprintf(suffix,
           sizeof(suffix),
           "},\"soil\":{\"raw\":%d,\"percent\":%u,\"valid\":%s,\"air\":%d,\"water\":%d,\"depth_mm\":%d,\"samples\":%lu,\"button_reads\":%lu,\"commands\":%lu,\"invalid_samples\":%lu},\"ad5263\":{\"present\":%s,\"brightness\":%u,\"expected_w2\":%u,\"expected_w1\":%u,\"readback_w2\":%u,\"readback_w1\":%u,\"relay_open\":%s,\"shdn_released\":%s,\"fault\":%s,\"reason\":\"%s\",\"auto_mode\":%s,\"hard_off\":%s,\"effective_on\":%s,\"applies\":%lu,\"verifies\":%lu,\"retries\":%lu,\"injected_faults\":%lu,\"step_index\":%lu,\"last_step\":\"%s\"},\"schedule\":{\"active\":%s,\"start\":%u,\"target\":%u,\"progress\":%u,\"on_min\":%u,\"off_min\":%u,\"alarm1_target\":%u,\"alarm2_target\":%u,\"dim_min\":%u,\"next_alarm1\":%lu,\"next_alarm2\":%lu,\"last_event\":\"%s\"},\"rtc\":{\"isr\":%lu,\"alarm1\":%lu,\"alarm2\":%lu},\"eeprom\":{\"sequence\":%lu,\"boot\":%lu,\"writes\":%lu,\"skipped\":%lu}}",
           moisture.getLastRaw(),
           moisture.getLastPercent(),
           moisture.isLastPercentValid() ? "true" : "false",
           moisture.getSoilAir(),
           moisture.getSoilWater(),
           moisture.getSoilDepthMm(),
           static_cast<unsigned long>(soilSampleCount),
           static_cast<unsigned long>(soilButtonReadCount),
           static_cast<unsigned long>(soilCommandCount),
           static_cast<unsigned long>(soilInvalidSampleCount),
           ad5263.isPresent() ? "true" : "false",
           ad5263.getCurrentPercent(),
           expectedTarget.w2,
           expectedTarget.w1,
           readbackTarget.w2,
           readbackTarget.w1,
           ad5263.isRelayOpen() ? "true" : "false",
           ad5263.isShutdownReleased() ? "true" : "false",
           ad5263.hasFault() ? "true" : "false",
           ad5263.getFaultReason(),
           lightAutoMode ? "true" : "false",
           lightHardPowerOff ? "true" : "false",
           (!ad5263.isRelayOpen() && !ad5263.hasFault()) ? "true" : "false",
           static_cast<unsigned long>(ad5263.getApplyCount()),
           static_cast<unsigned long>(ad5263.getVerifyCount()),
           static_cast<unsigned long>(ad5263.getRetryCount()),
           static_cast<unsigned long>(ad5263.getInjectedFaultCount()),
           static_cast<unsigned long>(ad5263StepIndex),
           ad5263LastStep,
           scheduledDim.active ? "true" : "false",
           scheduledDim.startPercent,
           scheduledDim.targetPercent,
           scheduledDim.progressPercent,
           activeRecord.lightOnMinutes,
           activeRecord.lightOffMinutes,
           activeRecord.lightOnTargetPercent,
           activeRecord.lightOffTargetPercent,
           activeRecord.dimMinutes,
           static_cast<unsigned long>(rtcState.nextAlarm1Epoch),
           static_cast<unsigned long>(rtcState.nextAlarm2Epoch),
           scheduleLastEvent,
           static_cast<unsigned long>(rtcState.isrSeen),
           static_cast<unsigned long>(rtcState.alarm1Seen),
           static_cast<unsigned long>(rtcState.alarm2Seen),
           static_cast<unsigned long>(persistenceState.sequence),
           static_cast<unsigned long>(persistenceState.bootCount),
           static_cast<unsigned long>(persistenceState.writes),
           static_cast<unsigned long>(persistenceState.skipped));
  strncat(payload, suffix, sizeof(payload) - strlen(payload) - 1);
  diagnosticMqtt.publish(MQTT_STATUS_TOPIC, payload, false);
}
void reportStateTransitions() {
  const bool autoDemand = shtState.tempHighDemand || shtState.humHighDemand;
  if (autoDemand != lastReportedAutoDemand) {
    publishDiagnosticEvent(autoDemand ? "fan_auto_demand_on" : "fan_auto_demand_off");
    lastReportedAutoDemand = autoDemand;
  }
  if (fan.isOn() != lastReportedFanOn) {
    publishDiagnosticEvent(fan.isOn() ? "fan_effective_on" : "fan_effective_off");
    lastReportedFanOn = fan.isOn();
  }
  if (fan.hasFault() != lastReportedFanFault) {
    publishDiagnosticEvent(fan.hasFault() ? "fan_fault_on" : "fan_fault_off");
    lastReportedFanFault = fan.hasFault();
  }
  if (shtState.tempLowObserved != lastReportedTempLow) {
    publishDiagnosticEvent(shtState.tempLowObserved ? "temperature_low_tracking_on" : "temperature_low_tracking_off");
    lastReportedTempLow = shtState.tempLowObserved;
  }
  if (shtState.humLowObserved != lastReportedHumLow) {
    publishDiagnosticEvent(shtState.humLowObserved ? "humidity_low_tracking_on" : "humidity_low_tracking_off");
    lastReportedHumLow = shtState.humLowObserved;
  }
  if (shtState.unexplainedSummary != lastReportedUnexplainedSummary) {
    publishDiagnosticEvent(shtState.unexplainedSummary ? "sht_unexplained_summary_on" : "sht_unexplained_summary_off");
    lastReportedUnexplainedSummary = shtState.unexplainedSummary;
  }

  const bool soilValid = moisture.isLastPercentValid();
  if (!lastReportedSoilValidityKnown || soilValid != lastReportedSoilValid) {
    publishDiagnosticEvent(soilValid ? "soil_percent_valid" : "soil_percent_invalid");
    lastReportedSoilValidityKnown = true;
    lastReportedSoilValid = soilValid;
  }

  if (ad5263.hasFault() != lastReportedLightFault) {
    publishDiagnosticEvent(ad5263.hasFault() ? "light_fault_on" : "light_fault_off");
    lastReportedLightFault = ad5263.hasFault();
    ad5263HaStateDirty = true;
  }
}

void serviceDiagnosticMqtt(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    diagnosticMqttWasConnected = false;
    diagnosticMqtt.disconnect();
    diagnosticNetworkClient.stop();
    return;
  }

  if (!diagnosticMqtt.connected()) {
    diagnosticMqttWasConnected = false;
    if (static_cast<int32_t>(nowMs - nextDiagnosticMqttAttemptMs) < 0) {
      return;
    }
    nextDiagnosticMqttAttemptMs = nowMs + DIAGNOSTIC_RECONNECT_INTERVAL_MS;
    if (!diagnosticMqtt.connect(DIAGNOSTIC_MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
      return;
    }
    diagnosticMqtt.publish(MQTT_COMMAND_TOPIC, "", true);
    // Test 09 is controlled through Home Assistant; direct test commands stay disabled.
  }

  diagnosticMqtt.loop();
  publishBootIdentity();
  if (!retainedTopicsCleared) {
    bool cleanupOk = true;
    for (uint8_t index = 0; index < RETAINED_TOPIC_CLEANUP_COUNT; ++index) {
      cleanupOk = diagnosticMqtt.publish(RETAINED_TOPICS_TO_CLEAR[index], "", true) && cleanupOk;
    }
    for (uint8_t index = 0; index < RETIRED_IDENTITY_ENTITY_COUNT; ++index) {
      char topic[192];
      snprintf(topic,
               sizeof(topic),
               "%s/sensor/%s/%s/config",
               MQTT_PREFIX,
               DEVICE_ID,
               RETIRED_IDENTITY_ENTITY_IDS[index]);
      cleanupOk = diagnosticMqtt.publish(topic, "", true) && cleanupOk;
      snprintf(topic,
               sizeof(topic),
               "%s/%s/%s/stat_t",
               MQTT_DATA_PREFIX,
               DEVICE_ID,
               RETIRED_IDENTITY_ENTITY_IDS[index]);
      cleanupOk = diagnosticMqtt.publish(topic, "", true) && cleanupOk;
    }
    retainedTopicsCleared = cleanupOk;
  }
  if (!diagnosticMqttWasConnected) {
    diagnosticMqttWasConnected = true;
    publishDiagnosticEvent("diagnostic_mqtt_connected");
    publishDiagnosticStatus(nowMs, true);
  }
  publishDiagnosticStatus(nowMs, false);
}
void publishThresholdStates() {
  tempHighSetNumber.setState(activeThresholds.tempHighSet);
  tempHighClearNumber.setState(activeThresholds.tempHighClear);
  tempLowSetNumber.setState(activeThresholds.tempLowSet);
  tempLowClearNumber.setState(activeThresholds.tempLowClear);
  humHighSetNumber.setState(activeThresholds.humHighSet);
  humHighClearNumber.setState(activeThresholds.humHighClear);
  humLowSetNumber.setState(activeThresholds.humLowSet);
  humLowClearNumber.setState(activeThresholds.humLowClear);
}

void publishSoilConfigStates() {
  soilAirNumber.setState(static_cast<int32_t>(activeRecord.soilAir));
  soilWaterNumber.setState(static_cast<int32_t>(activeRecord.soilWater));
  soilDepthNumber.setState(static_cast<int32_t>(activeRecord.soilDepthMm));
}

void publishScheduleConfigStates() {
  lightOnTimeNumber.setState(static_cast<int32_t>(activeRecord.lightOnMinutes));
  lightOffTimeNumber.setState(static_cast<int32_t>(activeRecord.lightOffMinutes));
  lightOnTargetNumber.setState(static_cast<int32_t>(activeRecord.lightOnTargetPercent));
  lightOffTargetNumber.setState(static_cast<int32_t>(activeRecord.lightOffTargetPercent));
  lightDimMinutesNumber.setState(static_cast<int32_t>(activeRecord.dimMinutes));
}

void onScheduleNumberCommand(HANumeric number, HANumber* sender) {
  uint16_t onMinutes = activeRecord.lightOnMinutes;
  uint16_t offMinutes = activeRecord.lightOffMinutes;
  uint8_t onTargetPercent = activeRecord.lightOnTargetPercent;
  uint8_t offTargetPercent = activeRecord.lightOffTargetPercent;
  uint16_t dimMinutes = activeRecord.dimMinutes;
  const long requested = lroundf(number.toFloat());
  const char* eventName = "schedule_config_unknown";
  bool alarmTimesChanged = false;

  if (sender == &lightOnTimeNumber) {
    onMinutes = static_cast<uint16_t>(constrain(requested, 0L, 1439L));
    eventName = "schedule_on_time_updated";
    alarmTimesChanged = onMinutes != activeRecord.lightOnMinutes;
  } else if (sender == &lightOffTimeNumber) {
    offMinutes = static_cast<uint16_t>(constrain(requested, 0L, 1439L));
    eventName = "schedule_off_time_updated";
    alarmTimesChanged = offMinutes != activeRecord.lightOffMinutes;
  } else if (sender == &lightOnTargetNumber) {
    onTargetPercent = static_cast<uint8_t>(constrain(requested, 0L, 100L));
    eventName = "schedule_alarm1_target_updated";
  } else if (sender == &lightOffTargetNumber) {
    offTargetPercent = static_cast<uint8_t>(constrain(requested, 0L, 100L));
    eventName = "schedule_alarm2_target_updated";
  } else if (sender == &lightDimMinutesNumber) {
    dimMinutes = static_cast<uint16_t>(constrain(requested, 0L, 1440L));
    eventName = "schedule_dim_minutes_updated";
  } else {
    return;
  }

  if (!persistScheduleConfiguration(onMinutes,
                                    offMinutes,
                                    onTargetPercent,
                                    offTargetPercent,
                                    dimMinutes)) {
    publishScheduleConfigStates();
    publishScheduleEvent("schedule_config_persist_failed");
    return;
  }

  if (alarmTimesChanged) {
    configureRtcAlarms();
  }
  publishScheduleConfigStates();
  publishScheduleEvent(eventName);
}

void publishSoilStates(bool force) {
  if (!mqtt.isConnected()) {
    return;
  }

  soilRawSensor.setValue(static_cast<int32_t>(moisture.getLastRaw()), force);
  if (moisture.isLastPercentValid()) {
    char percent[4];
    snprintf(percent, sizeof(percent), "%u", moisture.getLastPercent());
    soilPercentSensor.setValue(percent);
  } else {
    soilPercentSensor.setValue("unavailable");
  }
}

void onSoilNumberCommand(HANumeric number, HANumber* sender) {
  int16_t soilAir = activeRecord.soilAir;
  int16_t soilWater = activeRecord.soilWater;
  int16_t soilDepthMm = activeRecord.soilDepthMm;
  const long requested = lroundf(number.toFloat());
  const char* target = "unknown";

  if (sender == &soilAirNumber) {
    target = "soil_air";
    soilAir = static_cast<int16_t>(constrain(requested,
                                             static_cast<long>(SOIL_CAL_MIN),
                                             static_cast<long>(SOIL_CAL_MAX)));
  } else if (sender == &soilWaterNumber) {
    target = "soil_water";
    soilWater = static_cast<int16_t>(constrain(requested,
                                               static_cast<long>(SOIL_CAL_MIN),
                                               static_cast<long>(SOIL_CAL_MAX)));
  } else if (sender == &soilDepthNumber) {
    target = "soil_depth_mm";
    soilDepthMm = static_cast<int16_t>(constrain(requested,
                                                 static_cast<long>(SOIL_DEPTH_MIN_MM),
                                                 static_cast<long>(SOIL_DEPTH_MAX_MM)));
  } else {
    return;
  }

  soilCommandCount++;
  if (!persistSoilCalibration(soilAir, soilWater, soilDepthMm)) {
    publishSoilConfigStates();
    publishDiagnosticEvent("soil_calibration_persist_failed");
    return;
  }

  moisture.setCalibration(activeRecord.soilAir, activeRecord.soilWater, activeRecord.soilDepthMm);
  publishSoilConfigStates();
  publishDiagnosticEvent(target);
}

void onReadSoilRawCommand(HAButton*) {
  moisture.sampleNow();
  lastSoilSampleMs = millis();
  soilSampleCount++;
  soilButtonReadCount++;
  if (!moisture.isLastPercentValid()) {
    soilInvalidSampleCount++;
  }
  publishSoilStates(true);
  publishDiagnosticEvent("soil_raw_read_requested");
}

void onFanSwitchCommand(bool state, HASwitch*) {
  if (fan.isAutoMode()) {
    fan.setManualState(false);
    fanSwitch.setState(false);
    publishDiagnosticEvent("fan_manual_rejected_auto_mode");
    return;
  }
  fan.setManualState(state);
  fan.update(millis());
  fanSwitch.setState(fan.getManualState());
  publishDiagnosticEvent(state ? "fan_manual_on" : "fan_manual_off");
}

void onFanAutoModeCommand(bool state, HASwitch*) {
  if (!persistFanAutoMode(state)) {
    fanAutoModeSwitch.setState(fan.isAutoMode());
    publishDiagnosticEvent("fan_auto_mode_persist_failed");
    return;
  }
  if (state) {
    fan.setManualState(false);
    fanSwitch.setState(false);
  }
  fan.setAutoMode(state);
  fan.update(millis());
  fanAutoModeSwitch.setState(fan.isAutoMode());
  publishDiagnosticEvent(state ? "fan_auto_mode_on" : "fan_auto_mode_off");
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
    publishDiagnosticEvent("threshold_command_rejected_order");
    publishThresholdStates();
    return;
  }

  const ThresholdConfig previous = activeThresholds;
  if (!applyShtThresholds(candidate, true)) {
    shtState.rejectedThresholdCommands++;
    const bool rollbackOk = applyShtThresholds(previous, false);
    forceSafeAutoDemand();
    snprintf(thresholdFeedback,
             sizeof(thresholdFeedback),
             "failed #%lu: %s=%.1f; rollback=%s",
             static_cast<unsigned long>(thresholdCommandCount),
             target,
             static_cast<double>(value),
             rollbackOk ? "ok" : "failed");
    setThresholdFeedback(thresholdFeedback);
    publishDiagnosticEvent("threshold_command_rejected_apply");
    publishThresholdStates();
    return;
  }

  activeThresholds = candidate;
  if (!readShtStatus(false)) {
    forceSafeAutoDemand();
    publishDiagnosticEvent("threshold_status_read_failed");
  }
  snprintf(thresholdFeedback,
           sizeof(thresholdFeedback),
           "applied #%lu: %s=%.1f; readback verified",
           static_cast<unsigned long>(thresholdCommandCount),
           target,
           static_cast<double>(value));
  setThresholdFeedback(thresholdFeedback);
  publishThresholdStates();
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
  device.setModel("Arduino Nano 33 IoT AD5263 Safe Readback Test");
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
  soilRawSensor.setName("Soil Moisture Raw");
  soilRawSensor.setStateClass("measurement");
  soilPercentSensor.setName("Soil Moisture Percent");
  soilPercentSensor.setUnitOfMeasurement("%");
  soilPercentSensor.setStateClass("measurement");
  sketchIdentitySensor.setName("Sketch Identity");
  ad5263TestStepSensor.setName("Light Test Step");
  ad5263TestStepSensor.setForceUpdate(true);
  ad5263StepIndexSensor.setName("Light Step Index");
  ad5263BrightnessSensor.setName("Light Effective Brightness");
  ad5263BrightnessSensor.setUnitOfMeasurement("%");
  ad5263ExpectedW2Sensor.setName("AD5263 Expected W2");
  ad5263ExpectedW1Sensor.setName("AD5263 Expected W1");
  ad5263ReadbackW2Sensor.setName("AD5263 Readback W2");
  ad5263ReadbackW1Sensor.setName("AD5263 Readback W1");
  scheduleEventSensor.setName("Light Schedule Event");
  scheduleEventSensor.setForceUpdate(true);
  scheduleStateSensor.setName("Light Schedule State");
  scheduleProgressSensor.setName("Light Schedule Progress");
  scheduleProgressSensor.setUnitOfMeasurement("%");
  rtcAlarm1NextEpochSensor.setName("Light Alarm1 Next Epoch");
  rtcAlarm2NextEpochSensor.setName("Light Alarm2 Next Epoch");
  shtThresholdResultSensor.setName("SHT Threshold Result");
  shtDiagnosticSensor.setName("SHT Diagnostic");
  lightFaultReasonSensor.setName("Light Fault Reason");

  eepromFaultSensor.setName("EEPROM Fault");
  rtcFaultSensor.setName("RTC Fault");
  fanFaultSensor.setName("Fan Fault");
  shtFaultSensor.setName("SHT Fault");
  lightFaultSensor.setName("Light Fault");
  rtcLostPowerSensor.setName("Persistence RTC Lost Power");
  alarm1ConfiguredSensor.setName("Persistence RTC Alarm1 Configured");
  alarm2ConfiguredSensor.setName("Persistence RTC Alarm2 Configured");
  fanSafeSensor.setName("Fan Output Off");
  relaySafeSensor.setName("Light Relay Open");
  shdnSafeSensor.setName("Light SHDN Asserted");

  fanSwitch.setName("Fan");
  fanAutoModeSwitch.setName("Fan Auto Mode");
  fanSwitch.onCommand(onFanSwitchCommand);
  fanAutoModeSwitch.onCommand(onFanAutoModeCommand);
  growLight.setName("Grow Light");
  growLight.setBrightnessScale(100);
  growLight.onStateCommand(onGrowLightStateCommand);
  growLight.onBrightnessCommand(onGrowLightBrightnessCommand);
  lightAutoModeSwitch.setName("Light Auto Mode");
  lightAutoModeSwitch.onCommand(onLightAutoModeCommand);
  lightHardPowerOffSwitch.setName("Light Hard Power Off");
  lightHardPowerOffSwitch.onCommand(onLightHardPowerOffCommand);

  configureThresholdNumber(tempHighSetNumber, "Temp High Set", "C", TEMP_MIN_C, TEMP_MAX_C);
  configureThresholdNumber(tempHighClearNumber, "Temp High Clear", "C", TEMP_MIN_C, TEMP_MAX_C);
  configureThresholdNumber(tempLowSetNumber, "Temp Low Set", "C", TEMP_MIN_C, TEMP_MAX_C);
  configureThresholdNumber(tempLowClearNumber, "Temp Low Clear", "C", TEMP_MIN_C, TEMP_MAX_C);
  configureThresholdNumber(humHighSetNumber, "Hum High Set", "%", HUM_MIN_PERCENT, HUM_MAX_PERCENT);
  configureThresholdNumber(humHighClearNumber, "Hum High Clear", "%", HUM_MIN_PERCENT, HUM_MAX_PERCENT);
  configureThresholdNumber(humLowSetNumber, "Hum Low Set", "%", HUM_MIN_PERCENT, HUM_MAX_PERCENT);
  configureThresholdNumber(humLowClearNumber, "Hum Low Clear", "%", HUM_MIN_PERCENT, HUM_MAX_PERCENT);

  soilAirNumber.setName("Soil Air");
  soilAirNumber.setMin(static_cast<float>(SOIL_CAL_MIN));
  soilAirNumber.setMax(static_cast<float>(SOIL_CAL_MAX));
  soilAirNumber.setStep(1.0f);
  soilAirNumber.onCommand(onSoilNumberCommand);

  soilWaterNumber.setName("Soil Water");
  soilWaterNumber.setMin(static_cast<float>(SOIL_CAL_MIN));
  soilWaterNumber.setMax(static_cast<float>(SOIL_CAL_MAX));
  soilWaterNumber.setStep(1.0f);
  soilWaterNumber.onCommand(onSoilNumberCommand);

  soilDepthNumber.setName("Soil Depth mm");
  soilDepthNumber.setUnitOfMeasurement("mm");
  soilDepthNumber.setMin(static_cast<float>(SOIL_MIN_VALID_DEPTH_MM));
  soilDepthNumber.setMax(static_cast<float>(SOIL_DEPTH_MAX_MM));
  soilDepthNumber.setStep(1.0f);
  soilDepthNumber.onCommand(onSoilNumberCommand);

  lightOnTimeNumber.setName("Light On Time Minutes");
  lightOnTimeNumber.setUnitOfMeasurement("min");
  lightOnTimeNumber.setMin(0.0f);
  lightOnTimeNumber.setMax(1439.0f);
  lightOnTimeNumber.setStep(1.0f);
  lightOnTimeNumber.onCommand(onScheduleNumberCommand);

  lightOffTimeNumber.setName("Light Off Time Minutes");
  lightOffTimeNumber.setUnitOfMeasurement("min");
  lightOffTimeNumber.setMin(0.0f);
  lightOffTimeNumber.setMax(1439.0f);
  lightOffTimeNumber.setStep(1.0f);
  lightOffTimeNumber.onCommand(onScheduleNumberCommand);

  lightOnTargetNumber.setName("Light Alarm1 Target Percent");
  lightOnTargetNumber.setUnitOfMeasurement("%");
  lightOnTargetNumber.setMin(0.0f);
  lightOnTargetNumber.setMax(100.0f);
  lightOnTargetNumber.setStep(1.0f);
  lightOnTargetNumber.onCommand(onScheduleNumberCommand);

  lightOffTargetNumber.setName("Light Alarm2 Target Percent");
  lightOffTargetNumber.setUnitOfMeasurement("%");
  lightOffTargetNumber.setMin(0.0f);
  lightOffTargetNumber.setMax(100.0f);
  lightOffTargetNumber.setStep(1.0f);
  lightOffTargetNumber.onCommand(onScheduleNumberCommand);

  lightDimMinutesNumber.setName("Light Schedule Dim Minutes");
  lightDimMinutesNumber.setUnitOfMeasurement("min");
  lightDimMinutesNumber.setMin(0.0f);
  lightDimMinutesNumber.setMax(1440.0f);
  lightDimMinutesNumber.setStep(1.0f);
  lightDimMinutesNumber.onCommand(onScheduleNumberCommand);

  readSoilRawButton.setName("Read Soil Raw Value");
  readSoilRawButton.onCommand(onReadSoilRawCommand);
}
void onOtaStartSafeState() {
  pendingLightCommand = PendingLightCommand::None;
  scheduledDim.active = false;
  lightActionState = LightActionState::Idle;
  ad5263.openRelay();
  ad5263.assertShutdown();
}
void beginOta() {
  ArduinoOTA.onStart(onOtaStartSafeState);
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
  if (!shtState.present) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: SHT31 not found at 0x45");
  } else if (!shtState.measurementRunning) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: periodic measurement not running");
  } else if (!shtState.limitsApplied) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: alert-limit write failed");
  } else if (!shtState.limitsVerified) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: alert-limit readback failed");
  } else if (!shtState.measurementOk) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: measurement error %d", shtState.lastMeasurementError);
  } else if (!shtState.statusOk) {
    snprintf(diagnostic, sizeof(diagnostic), "fault: status error %d", shtState.lastStatusError);
  } else if (shtState.commandError || shtState.crcError) {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "fault: status command_error=%s crc_error=%s",
             shtState.commandError ? "yes" : "no",
             shtState.crcError ? "yes" : "no");
  } else {
    snprintf(diagnostic,
             sizeof(diagnostic),
             "ok: T=%.2f C RH=%.1f %% status=0x%04X alert_irq=%s",
             static_cast<double>(shtState.temperature),
             static_cast<double>(shtState.humidity),
             shtState.statusRegister,
             shtState.alertInterruptAttached ? "attached" : "polling");
  }
  shtDiagnosticSensor.setValue(diagnostic);
}
void publishHaBootIdentity() {
  if (!mqtt.isConnected()) {
    return;
  }
  if (!haSketchIdentityPublished) {
    char identity[80];
    snprintf(identity, sizeof(identity), "%s v%s", SKETCH_NAME, SKETCH_VERSION);
    haSketchIdentityPublished = sketchIdentitySensor.setValue(identity);
  }
}
void publishAd5263HaState(bool force) {
  if (!mqtt.isConnected() || (!force && !ad5263HaStateDirty)) {
    return;
  }

  const Ad5263SafeController::RdacTarget expectedTarget = ad5263.getExpectedTarget();
  const Ad5263SafeController::RdacTarget readbackTarget = ad5263.getReadbackTarget();
  bool published = true;
  published = ad5263StepIndexSensor.setValue(ad5263StepIndex, true) && published;
  published = ad5263BrightnessSensor.setValue(ad5263.getCurrentPercent(), true) && published;
  published = ad5263ExpectedW2Sensor.setValue(expectedTarget.w2, true) && published;
  published = ad5263ExpectedW1Sensor.setValue(expectedTarget.w1, true) && published;
  published = ad5263ReadbackW2Sensor.setValue(readbackTarget.w2, true) && published;
  published = ad5263ReadbackW1Sensor.setValue(readbackTarget.w1, true) && published;
  published = lightFaultReasonSensor.setValue(ad5263.getFaultReason()) && published;
  published = lightFaultSensor.setState(ad5263.hasFault(), true) && published;
  published = relaySafeSensor.setState(ad5263.isRelayOpen(), true) && published;
  published = shdnSafeSensor.setState(!ad5263.isShutdownReleased(), true) && published;
  growLight.setState(!ad5263.isRelayOpen() && !ad5263.hasFault());
  growLight.setBrightness(ad5263.getCurrentPercent());
  lightAutoModeSwitch.setState(lightAutoMode);
  lightHardPowerOffSwitch.setState(lightHardPowerOff);

  // Publish the unique step marker last so HA history only records a completed snapshot.
  if (published) {
    published = ad5263TestStepSensor.setValue(ad5263LastStep);
  }
  if (published) {
    ad5263HaStateDirty = false;
  }
}
void publishScheduleStates(bool force) {
  if (!mqtt.isConnected() || (!force && !scheduleHaStateDirty)) {
    return;
  }

  const char* state = "idle";
  if (scheduledDim.active) {
    state = "scheduled_dim_active";
  } else if (lightActionState != LightActionState::Idle) {
    state = "light_action_active";
  } else if (!lightAutoMode) {
    state = "auto_mode_off";
  }

  bool published = true;
  published = scheduleStateSensor.setValue(state) && published;
  published = scheduleProgressSensor.setValue(scheduledDim.progressPercent, true) && published;
  published = rtcAlarm1NextEpochSensor.setValue(rtcState.nextAlarm1Epoch, true) && published;
  published = rtcAlarm2NextEpochSensor.setValue(rtcState.nextAlarm2Epoch, true) && published;
  if (published) {
    published = scheduleEventSensor.setValue(scheduleLastEvent) && published;
  }
  if (published) {
    scheduleHaStateDirty = false;
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

  uptimeSensor.setValue(static_cast<uint32_t>(nowMs / 1000UL), true);
  wifiJoinsSensor.setValue(static_cast<uint32_t>(wifiConnectionCount), true);
  wifiTimeoutsSensor.setValue(static_cast<uint32_t>(wifiConnectTimeoutCount), true);
  wifiModuleResetsSensor.setValue(static_cast<uint32_t>(wifiModuleResetCount), true);
  otaGapSensor.setValue(static_cast<uint32_t>(otaPollGapViolations), true);

  eepromWritesSensor.setValue(static_cast<uint32_t>(persistenceState.writes), true);
  eepromSkippedSensor.setValue(static_cast<uint32_t>(persistenceState.skipped), true);
  eepromSequenceSensor.setValue(static_cast<uint32_t>(persistenceState.sequence), true);
  eepromBootCountSensor.setValue(static_cast<uint32_t>(persistenceState.bootCount), true);
  eepromChecksumSensor.setValue(static_cast<uint32_t>(persistenceState.checksum), true);

  rtcEpochSensor.setValue(static_cast<uint32_t>(rtcState.present ? rtcState.now.unixtime() : 0UL), true);
  rtcAlarm1SeenSensor.setValue(static_cast<uint32_t>(rtcState.alarm1Seen), true);
  rtcAlarm2SeenSensor.setValue(static_cast<uint32_t>(rtcState.alarm2Seen), true);
  rtcInterruptSeenSensor.setValue(static_cast<uint32_t>(rtcState.isrSeen), true);
  rtcAlarmClearsSensor.setValue(static_cast<uint32_t>(rtcState.clears), true);

  if (!isnan(shtState.temperature)) {
    temperatureSensor.setValue(shtState.temperature);
  }
  if (!isnan(shtState.humidity)) {
    humiditySensor.setValue(shtState.humidity);
  }
  fanRpmSensor.setValue(static_cast<int32_t>(fan.getRPM()));
  fanTachPulsesSensor.setValue(static_cast<uint32_t>(fan.getTotalTachPulses()), true);
  publishSoilStates(force);
  shtThresholdResultSensor.setValue(thresholdFeedback);
  publishShtDiagnostic();

  eepromFaultSensor.setState(!(persistenceState.present && persistenceState.checksumOk && persistenceState.writeOk), true);
  rtcFaultSensor.setState(!(rtcState.present && rtcState.alarm1Configured && rtcState.alarm2Configured), true);
  fanFaultSensor.setState(fan.hasFault(), true);
  shtFaultSensor.setState(shtHasFault(), true);
  rtcLostPowerSensor.setState(rtcState.lostPower, true);
  alarm1ConfiguredSensor.setState(rtcState.alarm1Configured, true);
  alarm2ConfiguredSensor.setState(rtcState.alarm2Configured, true);
  fanSafeSensor.setState(digitalRead(PIN_FAN_SWITCH) == FAN_OFF_LEVEL, true);
  fanSwitch.setState(fan.getManualState());
  fanAutoModeSwitch.setState(fan.isAutoMode());
  publishThresholdStates();
  publishSoilConfigStates();
  publishScheduleConfigStates();
  publishAd5263HaState(force);
  publishScheduleStates(force);

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
  diagnosticMqttWasConnected = false;
  diagnosticMqtt.disconnect();
  diagnosticNetworkClient.stop();

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
  publishHaBootIdentity();
  publishAd5263HaState(false);
  publishScheduleStates(false);
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
  Serial.print(F(", ha="));
  Serial.print(mqtt.isConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", diag="));
  Serial.print(diagnosticMqtt.connected() ? F("UP") : F("DOWN"));
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
  Serial.print(F(", soil_raw="));
  Serial.print(moisture.getLastRaw());
  Serial.print(F(", soil_percent="));
  if (moisture.isLastPercentValid()) {
    Serial.print(moisture.getLastPercent());
  } else {
    Serial.print(F("INVALID"));
  }
  Serial.print(F(", soil_depth_mm="));
  Serial.print(moisture.getSoilDepthMm());
  const Ad5263SafeController::RdacTarget expectedTarget = ad5263.getExpectedTarget();
  const Ad5263SafeController::RdacTarget readbackTarget = ad5263.getReadbackTarget();
  Serial.print(F(", ad5263_percent="));
  Serial.print(ad5263.getCurrentPercent());
  Serial.print(F(", ad5263_expected="));
  Serial.print(expectedTarget.w2);
  Serial.print('/');
  Serial.print(expectedTarget.w1);
  Serial.print(F(", ad5263_readback="));
  Serial.print(readbackTarget.w2);
  Serial.print('/');
  Serial.print(readbackTarget.w1);
  Serial.print(F(", light_relay="));
  Serial.print(ad5263.isRelayOpen() ? F("OPEN") : F("CLOSED"));
  Serial.print(F(", light_shdn="));
  Serial.print(ad5263.isShutdownReleased() ? F("RELEASED") : F("ASSERTED"));
  Serial.print(F(", light_fault="));
  Serial.print(ad5263.hasFault() ? ad5263.getFaultReason() : "NONE");
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

  Wire.begin();
  ad5263.begin(Wire);
  const bool ad5263StartupOk = ad5263.initializeSafeTarget();
  publishAd5263TestStep(ad5263StartupOk ? "light_boot_safe_target_verified" : "light_boot_safe_target_failed");
  initializePersistence();
  lightAutoMode = activeRecord.lightAutoMode != 0;
  lightHardPowerOff = false;
  loadActiveThresholds();
  initializeRtc();
  initializeSht();

  fan.begin();
  fan.setManualState(false);
  fan.setAutoMode(activeRecord.fanAutoMode != 0);
  fan.setAutoDemand(!shtHasFault() && (shtState.tempHighDemand || shtState.humHighDemand));
  fan.update(millis());

  moisture.begin(activeRecord.soilAir, activeRecord.soilWater, activeRecord.soilDepthMm);
  soilSampleCount = 1;
  if (!moisture.isLastPercentValid()) {
    soilInvalidSampleCount = 1;
  }
  lastSoilSampleMs = millis();

  const int shtInterruptId = digitalPinToInterrupt(PIN_SHT_ALERT);
  shtState.alertInterruptAttached = pinSupportsExternalInterrupt(PIN_SHT_ALERT);
  if (shtState.alertInterruptAttached) {
    attachInterrupt(shtInterruptId, onShtAlert, FALLING);
  } else if (serialAvailable()) {
    Serial.println(F("[SHT] A7 has no external-interrupt mapping in the active board core."));
  }
  const int rtcInterruptId = digitalPinToInterrupt(PIN_RTC_ALARM);
  if (pinSupportsExternalInterrupt(PIN_RTC_ALARM)) {
    attachInterrupt(rtcInterruptId, onRtcAlarm, FALLING);
  }
  // FanController owns the tach interrupt and only counts pulses in its ISR.

  configureHomeAssistant();
  publishScheduleEvent("schedule_boot_ready_full_range_0_100");
  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  diagnosticNetworkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  diagnosticMqtt.setServer(MQTT_HOST, MQTT_PORT);
  diagnosticMqtt.setCallback(onDiagnosticMqttMessage);
  diagnosticMqtt.setSocketTimeout(NETWORK_OPERATION_TIMEOUT_MS / 1000UL);
  diagnosticMqtt.setBufferSize(DIAGNOSTIC_PACKET_BUFFER_SIZE);
  nextWifiAttemptMs = millis();

  if (serialAvailable()) {
    Serial.println();
    Serial.println(F("Grow Controller Arduino Schedule And RTC Light Test"));
    Serial.println(F("Runtime order: safe outputs -> verified AD5263 target -> RTC alarms -> HA-visible schedule steps"));
  }
}

void loop() {
  serviceOta();
  uint32_t nowMs = millis();
  serviceRtc(nowMs);
  serviceSht(nowMs);
  fan.update(nowMs);
  serviceSoil(nowMs);
  serviceScheduledDim(nowMs);
  serviceLightControl(nowMs);
  reportStateTransitions();

  serviceWifi(nowMs);
  serviceOta();
  nowMs = millis();
  serviceMqtt(nowMs);
  fan.update(millis());
  reportStateTransitions();
  serviceDiagnosticMqtt(millis());
  serviceOta();
  printStatus(millis());
}

