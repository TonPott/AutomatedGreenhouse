#define MQTT_SOCKET_TIMEOUT 1

#include <Arduino.h>
#include <ArduinoHA.h>
#include <InternalStorage.h>
#include <RTClib.h>
#include <WiFiNINA.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <spi_drv.h>

#include "Ad5263Controller.h"
#include "Config.h"
#include "LightSensor.h"
#include "PersistentStore.h"
#include "RuntimeWatchdog.h"
#include "SafeDs3231.h"

#if defined(__has_include)
#if __has_include("Credentials.h")
#include "Credentials.h"
#else
#error "Missing Credentials.h. Copy Credentials.example.h and fill local values."
#endif
#else
#include "Credentials.h"
#endif

#ifndef NTP_SERVER
#error "Credentials.h must define NTP_SERVER."
#endif

namespace {

constexpr char SKETCH_NAME[] = "08a_LocalLightScheduleRuntimeTest";
constexpr char SKETCH_VERSION[] = "1.0.1";
constexpr char DEVICE_ID[] = "grow_controller_test_local_light_schedule";
constexpr char DEVICE_NAME[] = "Local RTC Light Schedule Test";
constexpr char MQTT_DATA_PREFIX[] = "smaeenhouse/test/local_light_schedule/ha";
constexpr uint32_t NTP_TO_UNIX_EPOCH_SECONDS = 2208988800UL;
constexpr uint32_t MIN_VALID_UNIX_EPOCH = 1577836800UL;  // 2020-01-01
constexpr uint32_t MAX_VALID_UNIX_EPOCH = 4102444799UL;  // 2099-12-31
constexpr uint8_t EVENT_QUEUE_SIZE = 24;
constexpr uint8_t EVENT_TEXT_SIZE = 72;
constexpr uint32_t HA_TX_INTERVAL_MS = 200UL;
constexpr uint32_t RTC_STUCK_LOW_MS = 500UL;
constexpr uint8_t HA_STATE_COUNT = 54;

enum class WifiState : uint8_t { Idle, Settling, Connecting };
enum class NtpState : uint8_t { WaitingForWifi, Ready, WaitingForResponse, Synced };

struct EventEntry {
  uint32_t sequence;
  char source[20];
  char target[20];
  char result[EVENT_TEXT_SIZE];
};

struct DimJob {
  bool active = false;
  uint8_t startPercent = 0;
  uint8_t targetPercent = 0;
  uint8_t progressPercent = 0;
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
};

class WatchdogOtaStorage : public OTAStorage {
public:
  int open(int length) override { RuntimeWatchdog::feed(); return InternalStorage.open(length); }
  size_t write(uint8_t value) override { RuntimeWatchdog::feed(); return InternalStorage.write(value); }
  void close() override { RuntimeWatchdog::feed(); InternalStorage.close(); }
  void clear() override { InternalStorage.clear(); }
  void apply() override { RuntimeWatchdog::feed(); InternalStorage.apply(); }
  long maxSize() override { return InternalStorage.maxSize(); }
};

WatchdogOtaStorage watchdogOtaStorage;
WiFiClient networkClient;
WiFiUDP ntpUdp;
HADevice device(DEVICE_ID);
HAMqtt mqtt(networkClient, device, HA_MQTT_ENTITY_LIMIT);
SafeDs3231 rtc;
PersistentStore persistentStore;
Ad5263Controller dimmer;
LightSensor lightSensor;

HASensor sketchIdentitySensor("sketch_identity");
HASensor uptimeSensor("uptime_h_mm");
HASensor rtcLocalTimeSensor("rtc_local_time");
HASensor rtcStatusSensor("rtc_status");
HASensorNumber rtcI2cErrorsSensor("rtc_i2c_error_count");
HABinarySensor rtcFaultSensor("rtc_fault");
HABinarySensor dstActiveSensor("dst_active");
HASensorNumber utcOffsetSensor("utc_offset_minutes");
HASensor ntpStatusSensor("ntp_status");
HASensorNumber ntpLastSyncSensor("ntp_last_sync_epoch");
HASensorNumber alarm1NextSensor("alarm1_next_epoch");
HASensorNumber alarm2NextSensor("alarm2_next_epoch");

HASensor eepromStatusSensor("eeprom_status");
HASensorNumber eepromI2cErrorsSensor("eeprom_i2c_error_count");
HABinarySensor eepromFaultSensor("eeprom_fault");
HASensorNumber eepromSequenceSensor("eeprom_sequence");
HASensorNumber eepromBootCountSensor("eeprom_boot_count");

HASensor dimmerStatusSensor("dimmer_status");
HASensorNumber dimmerI2cErrorsSensor("dimmer_i2c_error_count");
HABinarySensor lightFaultSensor("light_fault");
HASensor lightSensorStatusSensor("light_sensor_status");
HASensorNumber lightSensorI2cErrorsSensor("light_sensor_i2c_error_count");
HABinarySensor lightSensorFaultSensor("light_sensor_fault");
HASensorNumber fullSpectrumSensor("light_full_spectrum_raw");
HASensorNumber infraredSensor("light_infrared_raw");
HASensorNumber visibleSensor("light_visible_raw");
HABinarySensor lightInterruptSensor("light_interrupt");
HASensorNumber soilRawSensor("soil_moisture_raw");

HASensorNumber actualBrightnessSensor("light_actual_brightness_percent");
HASensorNumber logicalBrightnessSensor("light_logical_brightness_percent");
HABinarySensor relayOpenSensor("light_relay_open");
HABinarySensor shdnAssertedSensor("light_shdn_asserted");
HASensor scheduleStateSensor("schedule_state");
HASensorNumber scheduleProgressSensor("schedule_progress_percent");
HASensor testStepSensor("test_step");
HASwitch runtimeRecoveryLockoutSwitch("runtime_recovery_lockout");
HAButton watchdogRecoveryTestButton("trigger_watchdog_recovery_test");
HASensor resetCauseSensor("reset_cause");
HASensor i2cBusStatusSensor("i2c_bus_status");
HASensorNumber i2cRecoveryCountSensor("i2c_recovery_count");
HASensorNumber mqttPublishErrorCountSensor("mqtt_publish_error_count");
HASensorNumber rtcSpuriousIrqCountSensor("rtc_spurious_irq_count");

HASensorNumber wifiJoinsSensor("wifi_joins");
HASensorNumber wifiTimeoutsSensor("wifi_timeouts");
HASensorNumber wifiModuleResetsSensor("wifi_module_resets");
HASensorNumber otaGapSensor("ota_gap_violations");
HASensorNumber alarmLatencySensor("alarm_latency_seconds");
HASensorNumber maxAlarmLatencySensor("alarm_latency_max_seconds");

HASwitch hardPowerOffSwitch("light_hard_power_off");
HANumber alarm1HourNumber("alarm1_hour");
HANumber alarm1MinuteNumber("alarm1_minute");
HANumber alarm1TargetNumber("alarm1_target_percent");
HANumber alarm2HourNumber("alarm2_hour");
HANumber alarm2MinuteNumber("alarm2_minute");
HANumber alarm2TargetNumber("alarm2_target_percent");
HANumber dimDurationNumber("dim_duration_minutes");

volatile bool rtcAlarmPending = false;
EventEntry eventQueue[EVENT_QUEUE_SIZE]{};
uint8_t eventHead = 0;
uint8_t eventCount = 0;
uint32_t eventSequence = 0;
DimJob dimJob;

WifiState wifiState = WifiState::Idle;
NtpState ntpState = NtpState::WaitingForWifi;
bool wifiWasConnected = false;
bool mqttStarted = false;
bool mqttWasConnected = false;
bool otaStarted = false;
bool haIdentityPublished = false;
bool hardPowerOff = false;
bool rtcPresent = false;
bool rtcTimeValid = false;
bool scheduleEnabled = false;
bool rtcInterruptAttached = false;
bool timeRulesOk = true;
bool sensorSnapshotValid = false;
bool runtimeRecoveryLockout = false;
uint8_t logicalBrightness = 0;
uint32_t rtcI2cErrorCount = 0;
uint32_t rtcNextAlarm1Epoch = 0;
uint32_t rtcNextAlarm2Epoch = 0;
uint32_t lastRtcReadMs = 0;
uint32_t lastDstCheckMs = 0;
uint32_t lastSensorSampleMs = 0;
uint32_t lastHaPublishMs = 0;
uint32_t lastOtaPollMs = 0;
uint32_t otaGapViolations = 0;
uint32_t wifiJoinCount = 0;
uint32_t wifiTimeoutCount = 0;
uint32_t wifiModuleResetCount = 0;
uint8_t consecutiveWifiTimeouts = 0;
uint32_t wifiStateStartedMs = 0;
uint32_t nextWifiAttemptMs = 0;
uint32_t ntpRequestStartedMs = 0;
uint32_t lastNtpAttemptMs = 0;
uint32_t lastNtpSyncMs = 0;
uint32_t lastNtpSyncEpoch = 0;
uint32_t alarmLatencySeconds = 0;
uint32_t maxAlarmLatencySeconds = 0;
int soilRaw = 0;
char rtcStatus[32] = "not_initialized";
char ntpStatus[32] = "waiting_for_wifi";
char i2cBusStatus[32] = "not_initialized";
uint8_t bootResetCause = RuntimeWatchdog::resetCause();
uint32_t i2cRecoveryCount = 0;
uint32_t mqttPublishErrorCount = 0;
uint32_t rtcSpuriousIrqCount = 0;
uint32_t rtcLineLowSinceMs = 0;
uint32_t lastHaTxMs = 0;
uint32_t lastRtcTimeRefreshMs = 0;
uint8_t consecutiveMqttPublishErrors = 0;
uint8_t haStateCursor = 0;
uint8_t haStatesSinceEvent = 0;
uint8_t retainedCleanupStage = 0;
bool rtcSpuriousReported = false;
bool pendingManualRecovery = false;
bool pendingLockoutEnable = false;
bool pendingHardPowerCommand = false;
bool pendingHardPowerState = false;
bool pendingHardPowerPreviousState = false;
bool pendingScheduleCommand = false;
HANumber* pendingScheduleSender = nullptr;
long pendingScheduleValue = 0;
bool pendingWatchdogTest = false;
bool watchdogTestArmed = false;
bool recoveryWaitingForNtp = false;
bool haStateRefreshPending = false;
DateTime lastValidRtcTime(2000, 1, 1, 0, 0, 0);

void queueEvent(const char* result, const char* source = "runtime", const char* target = "device") {
  EventEntry entry{};
  entry.sequence = ++eventSequence;
  strncpy(entry.source, source, sizeof(entry.source) - 1U);
  strncpy(entry.target, target, sizeof(entry.target) - 1U);
  strncpy(entry.result, result != nullptr ? result : "unknown", sizeof(entry.result) - 1U);
  if (eventCount == EVENT_QUEUE_SIZE) {
    eventHead = static_cast<uint8_t>((eventHead + 1U) % EVENT_QUEUE_SIZE);
    eventCount--;
  }
  const uint8_t tail = static_cast<uint8_t>((eventHead + eventCount) % EVENT_QUEUE_SIZE);
  eventQueue[tail] = entry;
  eventCount++;
}

bool publishOneEvent() {
  if (!mqtt.isConnected() || eventCount == 0) return false;
  const EventEntry& entry = eventQueue[eventHead];
  char payload[144];
  snprintf(payload, sizeof(payload), "%lu|source=%s|target=%s|result=%s",
           static_cast<unsigned long>(entry.sequence), entry.source, entry.target, entry.result);
  if (!testStepSensor.setValue(payload)) return false;
  if (strcmp(entry.result, "watchdog_test_armed") == 0) watchdogTestArmed = true;
  eventHead = static_cast<uint8_t>((eventHead + 1U) % EVENT_QUEUE_SIZE);
  eventCount--;
  return true;
}

void setRtcStatus(const char* value) {
  strncpy(rtcStatus, value, sizeof(rtcStatus) - 1U);
  rtcStatus[sizeof(rtcStatus) - 1U] = '\0';
}

void setNtpStatus(const char* value) {
  strncpy(ntpStatus, value, sizeof(ntpStatus) - 1U);
  ntpStatus[sizeof(ntpStatus) - 1U] = '\0';
}

void forceSafeOutputsImmediate() {
  digitalWrite(PIN_LIGHT_POWER, LIGHT_RELAY_OPEN_LEVEL);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  dimJob.active = false;
}

bool rtcFailureIsTransport() {
  return strcmp(rtc.status(), "invalid_time_data") != 0 &&
         strcmp(rtc.status(), "invalid_time_write") != 0;
}

void enterRecoveryLockout(const char* reason, bool countTransportError) {
  forceSafeOutputsImmediate();
  runtimeRecoveryLockout = true;
  scheduleEnabled = false;
  rtcTimeValid = false;
  if (countTransportError) rtcI2cErrorCount++;
  setRtcStatus(reason);
  strncpy(i2cBusStatus, countTransportError ? "fault" : "semantic_fault", sizeof(i2cBusStatus) - 1U);
  queueEvent(reason, "runtime_recovery", "light_schedule");
  haStateRefreshPending = true;
}

bool readRtcChecked(DateTime& value, bool allowLostPower = false) {
  bool lostPower = false;
  if (!rtc.readTime(value, lostPower)) {
    enterRecoveryLockout(rtc.status(), rtcFailureIsTransport());
    return false;
  }
  if (lostPower && !allowLostPower) {
    enterRecoveryLockout("rtc_lost_power", false);
    return false;
  }
  lastValidRtcTime = value;
  return true;
}

uint32_t ymd(const DateTime& value) {
  return static_cast<uint32_t>(value.year()) * 10000UL +
         static_cast<uint32_t>(value.month()) * 100UL + value.day();
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && ((year % 4U == 0 && year % 100U != 0) || year % 400U == 0)) {
    return 29;
  }
  return days[month - 1U];
}

uint8_t lastSunday(uint16_t year, uint8_t month) {
  const uint8_t lastDay = daysInMonth(year, month);
  const DateTime date(year, month, lastDay, 0, 0, 0);
  return static_cast<uint8_t>(lastDay - date.dayOfTheWeek());
}

bool isDstAmbiguous(const DateTime& local) {
  const bool transitionMonth = local.month() == 3 || local.month() == 10;
  return transitionMonth && local.day() == lastSunday(local.year(), local.month()) &&
         local.hour() == 2;
}

bool expectedDstForLocal(const DateTime& local, bool currentDst) {
  if (local.month() < 3 || local.month() > 10) return false;
  if (local.month() > 3 && local.month() < 10) return true;
  if (local.month() == 3) {
    const uint8_t transitionDay = lastSunday(local.year(), 3);
    const uint8_t transitionHour = currentDst ? 3 : 2;
    return local.day() > transitionDay || (local.day() == transitionDay && local.hour() >= transitionHour);
  }
  const uint8_t transitionDay = lastSunday(local.year(), 10);
  if (local.day() < transitionDay) return true;
  if (local.day() > transitionDay) return false;
  if (local.hour() < 2) return true;
  if (local.hour() >= 3) return false;
  return currentDst;
}

bool dstForUtc(uint32_t utcEpoch) {
  const DateTime utc(utcEpoch);
  const uint32_t spring = DateTime(utc.year(), 3, lastSunday(utc.year(), 3), 1, 0, 0).unixtime();
  const uint32_t autumn = DateTime(utc.year(), 10, lastSunday(utc.year(), 10), 1, 0, 0).unixtime();
  return utcEpoch >= spring && utcEpoch < autumn;
}

uint32_t decodeNtpEpoch(uint32_t rawSeconds) {
  uint64_t fullSeconds = rawSeconds;
  if (rawSeconds <= NTP_TO_UNIX_EPOCH_SECONDS) {
    fullSeconds += (1ULL << 32);
  }
  if (fullSeconds <= NTP_TO_UNIX_EPOCH_SECONDS) {
    return 0;
  }
  const uint64_t unixEpoch = fullSeconds - NTP_TO_UNIX_EPOCH_SECONDS;
  if (unixEpoch < MIN_VALID_UNIX_EPOCH || unixEpoch > MAX_VALID_UNIX_EPOCH) {
    return 0;
  }
  return static_cast<uint32_t>(unixEpoch);
}

bool runTimeRuleSelfTests() {
  if (lastSunday(2026, 3) != 29 || lastSunday(2026, 10) != 25) return false;
  const DateTime springBefore(2026, 3, 29, 1, 59, 59);
  const DateTime springTransition(2026, 3, 29, 2, 0, 0);
  const DateTime autumnRepeatedHour(2026, 10, 25, 2, 30, 0);
  if (expectedDstForLocal(springBefore, false) ||
      !expectedDstForLocal(springTransition, false) ||
      !expectedDstForLocal(autumnRepeatedHour, true) ||
      expectedDstForLocal(autumnRepeatedHour, false)) return false;

  const uint32_t beforeSpring = DateTime(2026, 3, 29, 0, 59, 59).unixtime();
  const uint32_t afterSpring = DateTime(2026, 3, 29, 1, 0, 0).unixtime();
  if (dstForUtc(beforeSpring) || !dstForUtc(afterSpring)) return false;
  const uint32_t epoch2036 = DateTime(2036, 3, 1, 12, 0, 0).unixtime();
  const uint32_t raw2036 = static_cast<uint32_t>(static_cast<uint64_t>(epoch2036) + NTP_TO_UNIX_EPOCH_SECONDS);
  return decodeNtpEpoch(raw2036) == epoch2036;
}

uint16_t minutesOfDay(uint8_t hour, uint8_t minute) {
  return static_cast<uint16_t>(hour) * 60U + minute;
}

uint8_t dueScheduleTarget(const DateTime& now) {
  const ScheduleRecord& record = persistentStore.record();
  const uint16_t nowMinutes = minutesOfDay(now.hour(), now.minute());
  const uint16_t alarm1 = minutesOfDay(record.alarm1Hour, record.alarm1Minute);
  const uint16_t alarm2 = minutesOfDay(record.alarm2Hour, record.alarm2Minute);
  if (alarm1 <= nowMinutes && alarm2 <= nowMinutes) {
    return alarm1 > alarm2 ? record.alarm1Target : record.alarm2Target;
  }
  if (alarm1 <= nowMinutes) return record.alarm1Target;
  if (alarm2 <= nowMinutes) return record.alarm2Target;
  return alarm1 > alarm2 ? record.alarm1Target : record.alarm2Target;
}

DateTime nextAlarmDate(uint8_t alarmIndex, const DateTime& now) {
  const ScheduleRecord& record = persistentStore.record();
  const uint8_t hour = alarmIndex == 1 ? record.alarm1Hour : record.alarm2Hour;
  const uint8_t minute = alarmIndex == 1 ? record.alarm1Minute : record.alarm2Minute;
  const uint32_t lastDate = alarmIndex == 1 ? record.lastAlarm1Ymd : record.lastAlarm2Ymd;
  DateTime candidate(now.year(), now.month(), now.day(), hour, minute, 0);
  if (candidate <= now || lastDate == ymd(now)) {
    candidate = candidate + TimeSpan(1, 0, 0, 0);
  }
  return candidate;
}

bool configureRtcAlarms() {
  if (!rtcPresent || !rtcTimeValid || !scheduleEnabled || runtimeRecoveryLockout) return false;
  DateTime now;
  if (!readRtcChecked(now)) return false;
  const DateTime next1 = nextAlarmDate(1, now);
  const DateTime next2 = nextAlarmDate(2, now);
  if (!rtc.configureAlarms(next1, next2)) {
    enterRecoveryLockout(rtc.status(), rtcFailureIsTransport());
    return false;
  }
  rtcNextAlarm1Epoch = next1.unixtime();
  rtcNextAlarm2Epoch = next2.unixtime();
  noInterrupts();
  rtcAlarmPending = false;
  interrupts();
  rtcLineLowSinceMs = 0;
  rtcSpuriousReported = false;
  setRtcStatus("ok");
  return true;
}

void startDim(uint8_t target, uint32_t nowMs, const char* eventName, const char* source = "schedule") {
  dimJob.active = false;
  logicalBrightness = target;
  if (hardPowerOff) {
    queueEvent(eventName, source, "light");
    queueEvent("schedule_suppressed_hard_power_off");
    return;
  }
  dimJob.active = true;
  dimJob.startPercent = dimmer.currentPercent();
  dimJob.targetPercent = target;
  dimJob.progressPercent = 0;
  dimJob.startMs = nowMs;
  dimJob.durationMs = static_cast<uint32_t>(persistentStore.record().dimMinutes) * 60000UL;
  queueEvent(eventName, source, "light");
}

bool saveAlarmExecution(uint8_t alarmIndex, const DateTime& now) {
  ScheduleRecord updated = persistentStore.record();
  if (alarmIndex == 1) updated.lastAlarm1Ymd = ymd(now);
  else updated.lastAlarm2Ymd = ymd(now);
  return persistentStore.save(updated);
}

void handleAlarm(uint8_t alarmIndex, const DateTime& now, uint32_t scheduledEpoch) {
  const ScheduleRecord& record = persistentStore.record();
  const uint8_t target = alarmIndex == 1 ? record.alarm1Target : record.alarm2Target;
  const uint32_t nowEpoch = now.unixtime();
  alarmLatencySeconds = scheduledEpoch != 0 && nowEpoch >= scheduledEpoch
      ? nowEpoch - scheduledEpoch : 0;
  if (alarmLatencySeconds > maxAlarmLatencySeconds) maxAlarmLatencySeconds = alarmLatencySeconds;
  if (!saveAlarmExecution(alarmIndex, now)) {
    queueEvent("alarm_execution_persist_failed");
  }
  startDim(target, millis(), alarmIndex == 1 ? "alarm1_schedule_started" : "alarm2_schedule_started",
           alarmIndex == 1 ? "rtc_alarm1" : "rtc_alarm2");
}

void reconcileSchedule(const DateTime& now, const char* reason, const char* source = "schedule") {
  logicalBrightness = dueScheduleTarget(now);
  if (!scheduleEnabled) {
    logicalBrightness = 0;
    dimJob.active = false;
    dimmer.forceSafeOff();
    queueEvent("schedule_reconcile_blocked");
    return;
  }
  dimJob.active = false;
  if (hardPowerOff) {
    dimmer.apply(logicalBrightness, false);
    dimmer.hardPowerOff();
  } else if (!dimmer.apply(logicalBrightness, logicalBrightness > 0)) {
    queueEvent("schedule_reconcile_light_failed");
  }
  queueEvent(reason, source, "light");
}

void serviceDim(uint32_t nowMs) {
  if (!dimJob.active) return;
  if (hardPowerOff) {
    dimJob.active = false;
    queueEvent("schedule_dim_cancelled_hard_power_off");
    return;
  }
  const uint32_t elapsed = nowMs - dimJob.startMs;
  const bool complete = dimJob.durationMs == 0 || elapsed >= dimJob.durationMs;
  uint8_t desired = dimJob.targetPercent;
  if (!complete) {
    const int32_t delta = static_cast<int32_t>(dimJob.targetPercent) - dimJob.startPercent;
    const int32_t interpolated = dimJob.startPercent +
        static_cast<int32_t>((static_cast<int64_t>(delta) * elapsed) / dimJob.durationMs);
    desired = static_cast<uint8_t>(constrain(interpolated, 0L, 100L));
    dimJob.progressPercent = static_cast<uint8_t>((static_cast<uint64_t>(elapsed) * 100ULL) / dimJob.durationMs);
  } else {
    dimJob.progressPercent = 100;
  }
  if (desired != dimmer.currentPercent()) {
    bool ok = false;
    if (desired > 0 && dimmer.currentPercent() > 0 && !dimmer.isRelayOpen() && dimmer.isShutdownReleased()) {
      ok = dimmer.applyWhilePowered(desired);
    } else {
      ok = dimmer.apply(desired, desired > 0);
    }
    if (!ok) {
      dimJob.active = false;
      queueEvent("schedule_dim_light_fault");
      return;
    }
  }
  logicalBrightness = desired;
  if (complete) {
    dimJob.active = false;
    logicalBrightness = dimJob.targetPercent;
    queueEvent("schedule_dim_complete");
  }
}

void applyDstTransition(bool toDst, const DateTime& before) {
  const uint32_t today = ymd(before);
  const bool liveTransition = toDst
      ? (before.month() == 3 && before.day() == lastSunday(before.year(), 3) && before.hour() == 2)
      : (before.month() == 10 && before.day() == lastSunday(before.year(), 10) && before.hour() == 3);
  ScheduleRecord updated = persistentStore.record();
  if (toDst && before.month() == 3 && before.day() == lastSunday(before.year(), 3)) {
    const bool alarm1Skipped = updated.alarm1Hour == 2 && updated.lastAlarm1Ymd != today;
    const bool alarm2Skipped = updated.alarm2Hour == 2 && updated.lastAlarm2Ymd != today;
    if (alarm1Skipped && alarm2Skipped) {
      if (updated.alarm1Minute <= updated.alarm2Minute) {
        handleAlarm(1, before, before.unixtime());
        handleAlarm(2, before, before.unixtime());
      } else {
        handleAlarm(2, before, before.unixtime());
        handleAlarm(1, before, before.unixtime());
      }
    } else if (alarm1Skipped) handleAlarm(1, before, before.unixtime());
    else if (alarm2Skipped) handleAlarm(2, before, before.unixtime());
  }
  updated = persistentStore.record();
  if (!toDst && before.month() == 10 && before.day() == lastSunday(before.year(), 10)) {
    if (updated.alarm1Hour == 2) updated.lastAlarm1Ymd = today;
    if (updated.alarm2Hour == 2) updated.lastAlarm2Ymd = today;
  }
  updated.dstState = toDst ? 1 : 0;
  updated.lastDstTransitionYmd = today;
  const DateTime adjusted = before + TimeSpan(0, toDst ? 1 : -1, 0, 0);
  if (!rtc.writeTime(adjusted)) {
    enterRecoveryLockout(rtc.status(), rtcFailureIsTransport());
    return;
  }
  if (!persistentStore.save(updated)) queueEvent("dst_state_persist_failed", "dst", "eeprom");
  configureRtcAlarms();
  if (liveTransition) queueEvent(toDst ? "dst_spring_applied" : "dst_autumn_applied", "dst", "rtc");
  else reconcileSchedule(adjusted, toDst ? "dst_spring_reconciled" : "dst_autumn_reconciled");
}

void serviceDst(uint32_t nowMs) {
  if (runtimeRecoveryLockout || !rtcTimeValid || !scheduleEnabled || nowMs - lastDstCheckMs < 1000UL) return;
  lastDstCheckMs = nowMs;
  DateTime now;
  if (!readRtcChecked(now)) return;
  const ScheduleRecord& record = persistentStore.record();
  const bool currentDst = record.dstState == 1;
  const bool expected = expectedDstForLocal(now, currentDst);
  if (expected != currentDst && record.lastDstTransitionYmd != ymd(now)) applyDstTransition(expected, now);
}

void initializeRtc() {
  rtcPresent = rtc.begin(Wire);
  if (!rtcPresent) {
    enterRecoveryLockout(rtc.status(), true);
    return;
  }
  DateTime now;
  bool lostPower = false;
  if (!rtc.readTime(now, lostPower)) {
    enterRecoveryLockout(rtc.status(), rtcFailureIsTransport());
    return;
  }
  lastValidRtcTime = now;
  if (lostPower) {
    enterRecoveryLockout("lost_power_waiting_for_ntp", false);
    return;
  }
  rtcTimeValid = true;
  if (!persistentStore.isValid() || !timeRulesOk) {
    forceSafeOutputsImmediate();
    setRtcStatus(!timeRulesOk ? "time_rule_self_test_failed" : "eeprom_invalid_schedule_blocked");
    queueEvent("rtc_valid_schedule_blocked", "boot", "schedule");
    return;
  }
  ScheduleRecord updated = persistentStore.record();
  if (updated.dstState == 0xFF) {
    if (isDstAmbiguous(now)) {
      enterRecoveryLockout("dst_ambiguous_waiting_for_ntp", false);
      return;
    }
    updated.dstState = expectedDstForLocal(now, false) ? 1 : 0;
    if (!persistentStore.save(updated)) {
      enterRecoveryLockout("dst_state_persist_failed", false);
      return;
    }
  }
  scheduleEnabled = true;
  if (configureRtcAlarms()) reconcileSchedule(now, "schedule_boot_reconciled");
}

void serviceRtc(uint32_t nowMs) {
  if (runtimeRecoveryLockout || !rtcPresent || nowMs - lastRtcReadMs < RTC_SERVICE_INTERVAL_MS) return;
  lastRtcReadMs = nowMs;
  bool pending = false;
  noInterrupts();
  pending = rtcAlarmPending;
  rtcAlarmPending = false;
  interrupts();
  const bool lineLow = digitalRead(PIN_RTC_ALARM) == LOW;
  if (!lineLow) {
    rtcLineLowSinceMs = 0;
    rtcSpuriousReported = false;
  } else if (rtcLineLowSinceMs == 0) rtcLineLowSinceMs = nowMs;
  if (!rtcTimeValid || !scheduleEnabled) return;
  if (!pending && !lineLow) {
    if (nowMs - lastRtcTimeRefreshMs >= 1000UL) {
      lastRtcTimeRefreshMs = nowMs;
      DateTime current;
      readRtcChecked(current);
    }
    return;
  }
  uint8_t flags = 0;
  if (!rtc.readAlarmFlags(flags)) {
    enterRecoveryLockout(rtc.status(), rtcFailureIsTransport());
    return;
  }
  const bool alarm1 = (flags & 0x01U) != 0;
  const bool alarm2 = (flags & 0x02U) != 0;
  if (!alarm1 && !alarm2) {
    if (!rtcSpuriousReported) {
      rtcSpuriousReported = true;
      rtcSpuriousIrqCount++;
      queueEvent("interrupt_without_alarm_flag", "rtc_irq", "rtc");
    }
    if (lineLow && nowMs - rtcLineLowSinceMs >= RTC_STUCK_LOW_MS) enterRecoveryLockout("rtc_interrupt_stuck_low", false);
    return;
  }
  DateTime now;
  if (!readRtcChecked(now)) return;
  if (!rtc.clearAlarmFlags(flags)) {
    enterRecoveryLockout(rtc.status(), rtcFailureIsTransport());
    return;
  }
  if (alarm1 && alarm2) {
    const ScheduleRecord& record = persistentStore.record();
    if (minutesOfDay(record.alarm1Hour, record.alarm1Minute) < minutesOfDay(record.alarm2Hour, record.alarm2Minute)) {
      handleAlarm(1, now, rtcNextAlarm1Epoch);
      handleAlarm(2, now, rtcNextAlarm2Epoch);
    } else {
      handleAlarm(2, now, rtcNextAlarm2Epoch);
      handleAlarm(1, now, rtcNextAlarm1Epoch);
    }
  } else if (alarm1) handleAlarm(1, now, rtcNextAlarm1Epoch);
  else handleAlarm(2, now, rtcNextAlarm2Epoch);
  configureRtcAlarms();
}

bool sendNtpRequest() {
  while (ntpUdp.parsePacket() > 0) {
    uint8_t discard[NTP_PACKET_SIZE];
    ntpUdp.read(discard, sizeof(discard));
  }
  uint8_t packet[NTP_PACKET_SIZE]{};
  packet[0] = 0b00100011;
  if (ntpUdp.beginPacket(NTP_SERVER, NTP_SERVER_PORT) != 1 ||
      ntpUdp.write(packet, sizeof(packet)) != sizeof(packet) || ntpUdp.endPacket() != 1) {
    return false;
  }
  ntpRequestStartedMs = millis();
  return true;
}

void applyNtpTime(uint32_t utcEpoch) {
  const bool dst = dstForUtc(utcEpoch);
  const uint32_t localEpoch = utcEpoch + (dst ? 7200UL : 3600UL);
  const DateTime local(localEpoch);
  if (runtimeRecoveryLockout && !recoveryWaitingForNtp) {
    setNtpStatus("runtime_recovery_lockout");
    return;
  }
  if (recoveryWaitingForNtp) runtimeRecoveryLockout = false;
  const bool hadValidTime = rtcTimeValid;
  DateTime previous = lastValidRtcTime;
  if (hadValidTime && !readRtcChecked(previous)) return;
  const uint32_t previousLocalEpoch = hadValidTime ? previous.unixtime() : 0;
  if (!rtc.writeTime(local)) {
    enterRecoveryLockout(rtc.status(), rtcFailureIsTransport());
    setNtpStatus("rtc_write_failed");
    return;
  }
  DateTime verified;
  if (!readRtcChecked(verified)) return;
  const uint32_t actual = verified.unixtime();
  const uint32_t delta = actual > localEpoch ? actual - localEpoch : localEpoch - actual;
  if (delta > 2U) {
    enterRecoveryLockout("ntp_rtc_verify_failed", false);
    setNtpStatus("rtc_verify_failed");
    return;
  }
  rtcTimeValid = true;
  recoveryWaitingForNtp = false;
  lastNtpSyncEpoch = utcEpoch;
  lastNtpSyncMs = millis();
  setNtpStatus("synced");
  if (!persistentStore.isValid() || !timeRulesOk) {
    scheduleEnabled = false;
    forceSafeOutputsImmediate();
    setRtcStatus(!timeRulesOk ? "time_rule_self_test_failed" : "eeprom_invalid_schedule_blocked");
    queueEvent("ntp_time_valid_schedule_blocked", "ntp", "schedule");
    return;
  }
  scheduleEnabled = true;
  ScheduleRecord updated = persistentStore.record();
  updated.dstState = dst ? 1 : 0;
  if (!persistentStore.save(updated)) queueEvent("ntp_dst_state_persist_failed", "ntp", "eeprom");
  setRtcStatus("ok");
  configureRtcAlarms();
  const uint32_t correction = hadValidTime ? (previousLocalEpoch > localEpoch ? previousLocalEpoch - localEpoch : localEpoch - previousLocalEpoch) : UINT32_MAX;
  const bool targetChanged = hadValidTime && dueScheduleTarget(previous) != dueScheduleTarget(local);
  if (!hadValidTime || correction > 60U || targetChanged) reconcileSchedule(local, "ntp_time_applied_schedule_reconciled");
  else queueEvent("ntp_time_applied_schedule_unchanged", "ntp", "schedule");
  strncpy(i2cBusStatus, "ok", sizeof(i2cBusStatus) - 1U);
}

void serviceNtp(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    ntpState = NtpState::WaitingForWifi;
    setNtpStatus("waiting_for_wifi");
    return;
  }
  if (ntpState == NtpState::WaitingForWifi) {
    if (ntpUdp.begin(NTP_LOCAL_PORT) != 1) {
      setNtpStatus("udp_begin_failed");
      return;
    }
    ntpState = NtpState::Ready;
    lastNtpAttemptMs = nowMs - NTP_RETRY_INTERVAL_MS;
  }
  if (ntpState == NtpState::Synced && nowMs - lastNtpSyncMs >= NTP_RESYNC_INTERVAL_MS) {
    ntpState = NtpState::Ready;
    lastNtpAttemptMs = nowMs - NTP_RETRY_INTERVAL_MS;
  }
  if (ntpState == NtpState::Ready && nowMs - lastNtpAttemptMs >= NTP_RETRY_INTERVAL_MS) {
    lastNtpAttemptMs = nowMs;
    if (sendNtpRequest()) {
      ntpState = NtpState::WaitingForResponse;
      setNtpStatus("request_sent");
    } else {
      setNtpStatus("request_failed");
    }
  }
  if (ntpState != NtpState::WaitingForResponse) return;
  const int packetSize = ntpUdp.parsePacket();
  if (packetSize >= NTP_PACKET_SIZE) {
    uint8_t packet[NTP_PACKET_SIZE]{};
    const int bytesRead = ntpUdp.read(packet, sizeof(packet));
    const uint8_t leap = packet[0] >> 6;
    const uint8_t mode = packet[0] & 0x07U;
    const uint8_t stratum = packet[1];
    const uint32_t raw = (static_cast<uint32_t>(packet[40]) << 24) |
                         (static_cast<uint32_t>(packet[41]) << 16) |
                         (static_cast<uint32_t>(packet[42]) << 8) |
                         packet[43];
    const uint32_t utcEpoch = decodeNtpEpoch(raw);
    if (bytesRead == NTP_PACKET_SIZE && leap != 3 && (mode == 4 || mode == 5) &&
        stratum >= 1 && stratum <= 15 && utcEpoch != 0) {
      applyNtpTime(utcEpoch);
      ntpState = NtpState::Synced;
      queueEvent("ntp_sync_success");
    } else {
      ntpState = NtpState::Ready;
      setNtpStatus("invalid_response");
    }
  } else if (nowMs - ntpRequestStartedMs >= NTP_REQUEST_TIMEOUT_MS) {
    ntpState = NtpState::Ready;
    setNtpStatus("timeout");
  }
}

void sampleSensors(uint32_t nowMs, bool force) {
  if (!force && nowMs - lastSensorSampleMs < SENSOR_SAMPLE_INTERVAL_MS) return;
  lastSensorSampleMs = nowMs;
  lightSensor.sample();
  soilRaw = analogRead(PIN_SOIL_SENSOR);
  sensorSnapshotValid = true;
}

const char* scheduleStatusText() {
  if (runtimeRecoveryLockout) return "runtime_recovery_lockout";
  if (hardPowerOff) return "hard_power_off";
  if (!scheduleEnabled) return "schedule_blocked";
  return dimJob.active ? "dimming" : "waiting";
}

bool publishStateByIndex(uint8_t index, uint32_t nowMs) {
  char value[64];
  const ScheduleRecord& record = persistentStore.record();
  switch (index) {
    case 0: return runtimeRecoveryLockoutSwitch.setState(runtimeRecoveryLockout, true);
    case 1: return resetCauseSensor.setValue(RuntimeWatchdog::resetCauseText(bootResetCause));
    case 2: return rtcFaultSensor.setState(runtimeRecoveryLockout || !rtcPresent || !rtcTimeValid || !timeRulesOk, true);
    case 3: return rtcStatusSensor.setValue(rtcStatus);
    case 4: return i2cBusStatusSensor.setValue(i2cBusStatus);
    case 5: return relayOpenSensor.setState(digitalRead(PIN_LIGHT_POWER) == LIGHT_RELAY_OPEN_LEVEL, true);
    case 6: return shdnAssertedSensor.setState(digitalRead(PIN_LIGHT_DIM_SHDN) == LIGHT_DIM_SHDN_ASSERTED_LEVEL, true);
    case 7: return hardPowerOffSwitch.setState(hardPowerOff, true);
    case 8:
      snprintf(value, sizeof(value), "%s v%s", SKETCH_NAME, SKETCH_VERSION);
      haIdentityPublished = sketchIdentitySensor.setValue(value);
      return haIdentityPublished;
    case 9:
      snprintf(value, sizeof(value), "%lu:%02lu", static_cast<unsigned long>(nowMs / 3600000UL),
               static_cast<unsigned long>((nowMs / 60000UL) % 60UL));
      return uptimeSensor.setValue(value);
    case 10:
      if (!rtcTimeValid) return rtcLocalTimeSensor.setValue("unavailable");
      snprintf(value, sizeof(value), "%04u-%02u-%02u %02u:%02u:%02u", lastValidRtcTime.year(),
               lastValidRtcTime.month(), lastValidRtcTime.day(), lastValidRtcTime.hour(),
               lastValidRtcTime.minute(), lastValidRtcTime.second());
      return rtcLocalTimeSensor.setValue(value);
    case 11: return rtcI2cErrorsSensor.setValue(rtcI2cErrorCount, true);
    case 12: return dstActiveSensor.setState(record.dstState == 1, true);
    case 13: return utcOffsetSensor.setValue(record.dstState == 1 ? 120 : 60, true);
    case 14: return ntpStatusSensor.setValue(ntpStatus);
    case 15: return ntpLastSyncSensor.setValue(lastNtpSyncEpoch, true);
    case 16: return alarm1NextSensor.setValue(rtcNextAlarm1Epoch, true);
    case 17: return alarm2NextSensor.setValue(rtcNextAlarm2Epoch, true);
    case 18: return eepromStatusSensor.setValue(persistentStore.status());
    case 19: return eepromI2cErrorsSensor.setValue(persistentStore.errorCount(), true);
    case 20: return eepromFaultSensor.setState(persistentStore.hasFault(), true);
    case 21: return eepromSequenceSensor.setValue(record.sequence, true);
    case 22: return eepromBootCountSensor.setValue(record.bootCount, true);
    case 23: return dimmerStatusSensor.setValue(dimmer.status());
    case 24: return dimmerI2cErrorsSensor.setValue(dimmer.errorCount(), true);
    case 25: return lightFaultSensor.setState(dimmer.hasFault(), true);
    case 26: return lightSensorStatusSensor.setValue(lightSensor.status());
    case 27: return lightSensorI2cErrorsSensor.setValue(lightSensor.errorCount(), true);
    case 28: return lightSensorFaultSensor.setState(lightSensor.hasFault(), true);
    case 29: return fullSpectrumSensor.setValue(lightSensor.fullSpectrum(), true);
    case 30: return infraredSensor.setValue(lightSensor.infrared(), true);
    case 31: return visibleSensor.setValue(lightSensor.visible(), true);
    case 32: return lightInterruptSensor.setState(digitalRead(PIN_LIGHT_SENSOR_INT) == LOW, true);
    case 33: return soilRawSensor.setValue(static_cast<int32_t>(soilRaw), true);
    case 34: return actualBrightnessSensor.setValue(dimmer.currentPercent(), true);
    case 35: return logicalBrightnessSensor.setValue(logicalBrightness, true);
    case 36: return scheduleStateSensor.setValue(scheduleStatusText());
    case 37: return scheduleProgressSensor.setValue(dimJob.progressPercent, true);
    case 38: return wifiJoinsSensor.setValue(wifiJoinCount, true);
    case 39: return wifiTimeoutsSensor.setValue(wifiTimeoutCount, true);
    case 40: return wifiModuleResetsSensor.setValue(wifiModuleResetCount, true);
    case 41: return otaGapSensor.setValue(otaGapViolations, true);
    case 42: return alarmLatencySensor.setValue(alarmLatencySeconds, true);
    case 43: return maxAlarmLatencySensor.setValue(maxAlarmLatencySeconds, true);
    case 44: return alarm1HourNumber.setState(record.alarm1Hour, true);
    case 45: return alarm1MinuteNumber.setState(record.alarm1Minute, true);
    case 46: return alarm1TargetNumber.setState(record.alarm1Target, true);
    case 47: return alarm2HourNumber.setState(record.alarm2Hour, true);
    case 48: return alarm2MinuteNumber.setState(record.alarm2Minute, true);
    case 49: return alarm2TargetNumber.setState(record.alarm2Target, true);
    case 50: return dimDurationNumber.setState(record.dimMinutes, true);
    case 51: return i2cRecoveryCountSensor.setValue(i2cRecoveryCount, true);
    case 52: return mqttPublishErrorCountSensor.setValue(mqttPublishErrorCount, true);
    case 53: return rtcSpuriousIrqCountSensor.setValue(rtcSpuriousIrqCount, true);
    default: return true;
  }
}

void handlePublishResult(bool success) {
  if (success) {
    consecutiveMqttPublishErrors = 0;
    return;
  }
  mqttPublishErrorCount++;
  if (++consecutiveMqttPublishErrors >= 3) {
    mqttWasConnected = false;
    mqtt.disconnect();
    networkClient.stop();
    consecutiveMqttPublishErrors = 0;
  }
}

void serviceHaTransmit(uint32_t nowMs) {
  if (!mqtt.isConnected() || nowMs - lastHaTxMs < HA_TX_INTERVAL_MS) return;
  lastHaTxMs = nowMs;
  if (!haStateRefreshPending && nowMs - lastHaPublishMs >= HA_PUBLISH_INTERVAL_MS) {
    haStateRefreshPending = true;
    haStateCursor = 0;
  }
  if (eventCount > 0 && (haStatesSinceEvent >= 3 || !haStateRefreshPending)) {
    const bool ok = publishOneEvent();
    handlePublishResult(ok);
    if (ok) haStatesSinceEvent = 0;
    return;
  }
  if (haStateRefreshPending) {
    const bool ok = publishStateByIndex(haStateCursor, nowMs);
    handlePublishResult(ok);
    if (ok) {
      haStateCursor++;
      haStatesSinceEvent++;
      if (haStateCursor >= HA_STATE_COUNT) {
        haStateRefreshPending = false;
        haStateCursor = 0;
        lastHaPublishMs = nowMs;
      }
    }
    return;
  }
  static const char* cleanupTopics[] = {
      "homeassistant/sensor/grow_controller_test_local_light_schedule_test_step_index/config",
      "homeassistant/sensor/grow_controller_test_local_light_schedule/test_step_index/config",
      "smaeenhouse/test/local_light_schedule/ha/sensor/test_step_index/state",
      "smaeenhouse/test/local_light_schedule/ha/test_step_index/state"};
  if (retainedCleanupStage < 4) {
    handlePublishResult(mqtt.publish(cleanupTopics[retainedCleanupStage], "", true));
    retainedCleanupStage++;
  }
}

void onScheduleNumberCommand(HANumeric number, HANumber* sender) {
  pendingScheduleValue = lroundf(number.toFloat());
  pendingScheduleSender = sender;
  pendingScheduleCommand = true;
}

void onHardPowerOffCommand(bool state, HASwitch*) {
  pendingHardPowerPreviousState = hardPowerOff;
  pendingHardPowerState = state;
  pendingHardPowerCommand = true;
  if (state) {
    hardPowerOff = true;
    forceSafeOutputsImmediate();
  }
}

void onRuntimeRecoveryLockoutCommand(bool state, HASwitch*) {
  if (state) {
    runtimeRecoveryLockout = true;
    pendingLockoutEnable = true;
    forceSafeOutputsImmediate();
  } else {
    pendingManualRecovery = true;
  }
}

void onWatchdogRecoveryTestCommand(HAButton*) {
  pendingWatchdogTest = true;
  forceSafeOutputsImmediate();
}

void recoverI2cLines() {
  Wire.end();
  pinMode(SDA, INPUT_PULLUP);
  pinMode(SCL, INPUT_PULLUP);
  delayMicroseconds(10);
  if (digitalRead(SDA) == LOW) {
    pinMode(SCL, OUTPUT);
    for (uint8_t pulse = 0; pulse < 9 && digitalRead(SDA) == LOW; ++pulse) {
      digitalWrite(SCL, LOW); delayMicroseconds(5);
      digitalWrite(SCL, HIGH); delayMicroseconds(5);
    }
    pinMode(SDA, OUTPUT);
    digitalWrite(SDA, LOW); delayMicroseconds(5);
    digitalWrite(SCL, HIGH); delayMicroseconds(5);
    digitalWrite(SDA, HIGH); delayMicroseconds(5);
  }
  pinMode(SDA, INPUT_PULLUP);
  pinMode(SCL, INPUT_PULLUP);
  Wire.begin();
  Wire.setClock(100000UL);
}

bool attemptRuntimeRecovery() {
  forceSafeOutputsImmediate();
  i2cRecoveryCount++;
  strncpy(i2cBusStatus, "recovering", sizeof(i2cBusStatus) - 1U);
  recoverI2cLines();
  dimmer.begin(Wire);
  const bool dimmerOk = dimmer.initializeSafe();
  const bool eepromOk = persistentStore.begin();
  rtcPresent = rtc.begin(Wire);
  DateTime now;
  bool lostPower = true;
  const bool rtcReadOk = rtcPresent && rtc.readTime(now, lostPower);
  if (!dimmerOk || !eepromOk || !rtcReadOk || !timeRulesOk) {
    runtimeRecoveryLockout = true;
    scheduleEnabled = false;
    rtcTimeValid = false;
    strncpy(i2cBusStatus, "recovery_failed", sizeof(i2cBusStatus) - 1U);
    queueEvent("manual_recovery_failed", "runtime_recovery", "i2c_bus");
    return false;
  }
  if (lostPower) {
    recoveryWaitingForNtp = true;
    runtimeRecoveryLockout = true;
    scheduleEnabled = false;
    rtcTimeValid = false;
    ntpState = NtpState::Ready;
    lastNtpAttemptMs = millis() - NTP_RETRY_INTERVAL_MS;
    strncpy(i2cBusStatus, "waiting_for_ntp", sizeof(i2cBusStatus) - 1U);
    queueEvent("manual_recovery_waiting_for_ntp", "runtime_recovery", "rtc");
    return false;
  }
  lastValidRtcTime = now;
  hardPowerOff = persistentStore.record().hardPowerOff != 0;
  rtcTimeValid = true;
  scheduleEnabled = true;
  runtimeRecoveryLockout = false;
  lightSensor.begin(Wire);
  if (!configureRtcAlarms()) return false;
  reconcileSchedule(now, "manual_recovery_reconciled");
  if (hardPowerOff) dimmer.hardPowerOff();
  strncpy(i2cBusStatus, "ok_recovered", sizeof(i2cBusStatus) - 1U);
  queueEvent("manual_recovery_succeeded", "runtime_recovery", "i2c_bus");
  haStateRefreshPending = true;
  haStateCursor = 0;
  return true;
}

void processPendingCommands() {
  if (pendingLockoutEnable) {
    pendingLockoutEnable = false;
    runtimeRecoveryLockout = true;
    scheduleEnabled = false;
    forceSafeOutputsImmediate();
    queueEvent("manual_lockout_enabled", "ha_command", "runtime_recovery");
  }
  if (pendingManualRecovery) {
    pendingManualRecovery = false;
    attemptRuntimeRecovery();
  }
  if (pendingWatchdogTest) {
    pendingWatchdogTest = false;
    runtimeRecoveryLockout = true;
    scheduleEnabled = false;
    forceSafeOutputsImmediate();
    queueEvent("watchdog_test_armed", "ha_command", "watchdog");
  }
  if (pendingHardPowerCommand) {
    pendingHardPowerCommand = false;
    if (runtimeRecoveryLockout) {
      hardPowerOff = pendingHardPowerPreviousState;
      forceSafeOutputsImmediate();
      queueEvent("hard_power_command_rejected_lockout", "ha_command", "light");
      haStateRefreshPending = true;
      haStateCursor = 0;
      return;
    }
    ScheduleRecord updated = persistentStore.record();
    updated.hardPowerOff = pendingHardPowerState ? 1 : 0;
    if (pendingHardPowerState) {
      hardPowerOff = true;
      if (!persistentStore.save(updated)) queueEvent("hard_power_off_persist_failed_kept_active", "ha_command", "eeprom");
      else queueEvent("hard_power_off_enabled", "ha_command", "light");
    } else if (!persistentStore.save(updated)) {
      hardPowerOff = true;
      queueEvent("hard_power_off_release_persist_failed", "ha_command", "eeprom");
    } else if (runtimeRecoveryLockout) {
      hardPowerOff = false;
      forceSafeOutputsImmediate();
      queueEvent("hard_power_off_released_lockout_active", "ha_command", "light");
    } else {
      DateTime now;
      hardPowerOff = false;
      if (!readRtcChecked(now)) return;
      logicalBrightness = dueScheduleTarget(now);
      if (!dimmer.releaseHardPowerOff(logicalBrightness)) enterRecoveryLockout("hard_off_release_light_failed", true);
      else queueEvent("hard_power_off_released", "ha_command", "light");
    }
  }
  if (pendingScheduleCommand) {
    pendingScheduleCommand = false;
    if (runtimeRecoveryLockout) {
      queueEvent("schedule_config_rejected_lockout", "ha_command", "schedule");
      haStateRefreshPending = true;
      haStateCursor = 0;
      return;
    }
    ScheduleRecord updated = persistentStore.record();
    const long requested = pendingScheduleValue;
    if (pendingScheduleSender == &alarm1HourNumber) updated.alarm1Hour = constrain(requested,0L,23L);
    else if (pendingScheduleSender == &alarm1MinuteNumber) updated.alarm1Minute = constrain(requested,0L,59L);
    else if (pendingScheduleSender == &alarm1TargetNumber) updated.alarm1Target = constrain(requested,0L,100L);
    else if (pendingScheduleSender == &alarm2HourNumber) updated.alarm2Hour = constrain(requested,0L,23L);
    else if (pendingScheduleSender == &alarm2MinuteNumber) updated.alarm2Minute = constrain(requested,0L,59L);
    else if (pendingScheduleSender == &alarm2TargetNumber) updated.alarm2Target = constrain(requested,0L,100L);
    else if (pendingScheduleSender == &dimDurationNumber) updated.dimMinutes = constrain(requested,0L,1440L);
    else return;
    if (updated.alarm1Hour == updated.alarm2Hour && updated.alarm1Minute == updated.alarm2Minute) {
      queueEvent("equal_alarm_times_rejected", "config_reconcile", "schedule");
    } else if (!persistentStore.save(updated)) {
      queueEvent("schedule_config_persist_failed", "config_reconcile", "eeprom");
    } else if (!runtimeRecoveryLockout && scheduleEnabled) {
      DateTime now;
      if (readRtcChecked(now) && configureRtcAlarms()) reconcileSchedule(now, "schedule_config_reconciled", "config_reconcile");
    } else queueEvent("schedule_config_saved_lockout_active", "config_reconcile", "schedule");
    haStateRefreshPending = true;
    haStateCursor = 0;
  }
}

void configureNumber(HANumber& entity, const char* name, float minimum, float maximum, const char* unit = nullptr) {
  entity.setName(name);
  entity.setMin(minimum);
  entity.setMax(maximum);
  entity.setStep(1.0f);
  if (unit != nullptr) entity.setUnitOfMeasurement(unit);
  entity.onCommand(onScheduleNumberCommand);
}

void configureHomeAssistant() {
  device.setName(DEVICE_NAME);
  device.setManufacturer("Smaeenhouse");
  device.setModel("Arduino Nano 33 IoT Local RTC Schedule Test");
  device.setSoftwareVersion(SKETCH_VERSION);
  device.enableExtendedUniqueIds();
  device.enableSharedAvailability();
  device.enableLastWill();
  mqtt.setDiscoveryPrefix(MQTT_PREFIX);
  mqtt.setDataPrefix(MQTT_DATA_PREFIX);

  sketchIdentitySensor.setName("Sketch Identity");
  uptimeSensor.setName("Uptime");
  rtcLocalTimeSensor.setName("RTC Local Time");
  rtcStatusSensor.setName("RTC Status");
  rtcI2cErrorsSensor.setName("RTC I2C Error Count");
  rtcI2cErrorsSensor.setStateClass("total_increasing");
  rtcFaultSensor.setName("RTC Fault");
  dstActiveSensor.setName("DST Active");
  utcOffsetSensor.setName("UTC Offset");
  utcOffsetSensor.setUnitOfMeasurement("min");
  ntpStatusSensor.setName("NTP Status");
  ntpLastSyncSensor.setName("NTP Last Sync Epoch");
  alarm1NextSensor.setName("Alarm 1 Next Epoch");
  alarm2NextSensor.setName("Alarm 2 Next Epoch");

  eepromStatusSensor.setName("EEPROM Status");
  eepromI2cErrorsSensor.setName("EEPROM I2C Error Count");
  eepromI2cErrorsSensor.setStateClass("total_increasing");
  eepromFaultSensor.setName("EEPROM Fault");
  eepromSequenceSensor.setName("EEPROM Sequence");
  eepromBootCountSensor.setName("EEPROM Boot Count");
  dimmerStatusSensor.setName("Dimmer Status");
  dimmerI2cErrorsSensor.setName("Dimmer I2C Error Count");
  dimmerI2cErrorsSensor.setStateClass("total_increasing");
  lightFaultSensor.setName("Light Fault");
  lightSensorStatusSensor.setName("Light Sensor Status");
  lightSensorI2cErrorsSensor.setName("Light Sensor I2C Error Count");
  lightSensorI2cErrorsSensor.setStateClass("total_increasing");
  lightSensorFaultSensor.setName("Light Sensor Fault");

  fullSpectrumSensor.setName("Light Full Spectrum Raw");
  fullSpectrumSensor.setStateClass("measurement");
  infraredSensor.setName("Light Infrared Raw");
  infraredSensor.setStateClass("measurement");
  visibleSensor.setName("Light Visible Raw");
  visibleSensor.setStateClass("measurement");
  lightInterruptSensor.setName("Light Interrupt");
  soilRawSensor.setName("Soil Moisture Raw");
  soilRawSensor.setStateClass("measurement");
  actualBrightnessSensor.setName("Light Actual Brightness");
  actualBrightnessSensor.setUnitOfMeasurement("%");
  logicalBrightnessSensor.setName("Light Logical Brightness");
  logicalBrightnessSensor.setUnitOfMeasurement("%");
  relayOpenSensor.setName("Light Relay Open");
  shdnAssertedSensor.setName("Light SHDN Asserted");
  scheduleStateSensor.setName("Schedule State");
  scheduleProgressSensor.setName("Schedule Progress");
  scheduleProgressSensor.setUnitOfMeasurement("%");
  testStepSensor.setName("Test Step");
  testStepSensor.setForceUpdate(true);
  runtimeRecoveryLockoutSwitch.setName("Runtime Recovery Lockout");
  runtimeRecoveryLockoutSwitch.onCommand(onRuntimeRecoveryLockoutCommand);
  watchdogRecoveryTestButton.setName("Trigger Watchdog Recovery Test");
  watchdogRecoveryTestButton.onCommand(onWatchdogRecoveryTestCommand);
  resetCauseSensor.setName("Reset Cause");
  i2cBusStatusSensor.setName("I2C Bus Status");
  i2cRecoveryCountSensor.setName("I2C Recovery Count");
  i2cRecoveryCountSensor.setStateClass("total_increasing");
  mqttPublishErrorCountSensor.setName("MQTT Publish Error Count");
  mqttPublishErrorCountSensor.setStateClass("total_increasing");
  rtcSpuriousIrqCountSensor.setName("RTC Spurious IRQ Count");
  rtcSpuriousIrqCountSensor.setStateClass("total_increasing");
  wifiJoinsSensor.setName("WiFi Joins");
  wifiJoinsSensor.setStateClass("total_increasing");
  wifiTimeoutsSensor.setName("WiFi Timeouts");
  wifiTimeoutsSensor.setStateClass("total_increasing");
  wifiModuleResetsSensor.setName("WiFi Module Resets");
  wifiModuleResetsSensor.setStateClass("total_increasing");
  otaGapSensor.setName("OTA Gap Violations");
  otaGapSensor.setStateClass("total_increasing");
  alarmLatencySensor.setName("Alarm Latency");
  alarmLatencySensor.setUnitOfMeasurement("s");
  maxAlarmLatencySensor.setName("Alarm Maximum Latency");
  maxAlarmLatencySensor.setUnitOfMeasurement("s");

  hardPowerOffSwitch.setName("Light Hard Power Off");
  hardPowerOffSwitch.onCommand(onHardPowerOffCommand);
  configureNumber(alarm1HourNumber, "Alarm 1 Hour", 0, 23);
  configureNumber(alarm1MinuteNumber, "Alarm 1 Minute", 0, 59);
  configureNumber(alarm1TargetNumber, "Alarm 1 Target", 0, 100, "%");
  configureNumber(alarm2HourNumber, "Alarm 2 Hour", 0, 23);
  configureNumber(alarm2MinuteNumber, "Alarm 2 Minute", 0, 59);
  configureNumber(alarm2TargetNumber, "Alarm 2 Target", 0, 100, "%");
  configureNumber(dimDurationNumber, "Dim Duration", 0, 1440, "min");
}

void onOtaStart() {
  dimJob.active = false;
  dimmer.forceSafeOff();
}

void beginOta() {
  ArduinoOTA.onStart(onOtaStart);
  ArduinoOTA.begin(WiFi.localIP(), OTA_NAME, OTA_PASSWORD, watchdogOtaStorage);
  otaStarted = true;
  lastOtaPollMs = millis();
  queueEvent("ota_ready");
}

void serviceOta() {
  if (!otaStarted || WiFi.status() != WL_CONNECTED) return;
  const uint32_t before = millis();
  if (before - lastOtaPollMs > OTA_MAX_POLL_GAP_MS) otaGapViolations++;
  ArduinoOTA.poll();
  lastOtaPollMs = millis();
}

void onWifiConnected() {
  wifiJoinCount++;
  consecutiveWifiTimeouts = 0;
  beginOta();
  if (!mqttStarted) {
    mqtt.begin(MQTT_HOST, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD);
    mqttStarted = true;
  }
  ntpState = NtpState::WaitingForWifi;
  queueEvent("wifi_connected");
}

void serviceWifi(uint32_t nowMs) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    wifiState = WifiState::Idle;
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      onWifiConnected();
    }
    return;
  }
  if (wifiWasConnected) {
    wifiWasConnected = false;
    otaStarted = false;
    mqttWasConnected = false;
    haIdentityPublished = false;
    if (mqtt.isConnected()) mqtt.disconnect();
    networkClient.stop();
    ntpUdp.stop();
    wifiState = WifiState::Idle;
    nextWifiAttemptMs = nowMs;
    queueEvent("wifi_disconnected");
  }
  switch (wifiState) {
    case WifiState::Idle:
      if (static_cast<int32_t>(nowMs - nextWifiAttemptMs) >= 0) {
        WiFi.disconnect();
        wifiState = WifiState::Settling;
        wifiStateStartedMs = nowMs;
      }
      break;
    case WifiState::Settling:
      if (nowMs - wifiStateStartedMs >= WIFI_SETTLE_MS) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiState = WifiState::Connecting;
        wifiStateStartedMs = millis();
      }
      break;
    case WifiState::Connecting:
      if (nowMs - wifiStateStartedMs >= WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect();
        wifiTimeoutCount++;
        consecutiveWifiTimeouts++;
        if (consecutiveWifiTimeouts >= WIFI_TIMEOUTS_BEFORE_MODULE_RESET) {
          networkClient.stop();
          SpiDrv::begin(true);
          WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
          wifiModuleResetCount++;
          consecutiveWifiTimeouts = 0;
          wifiState = WifiState::Settling;
          wifiStateStartedMs = millis();
        } else {
          wifiState = WifiState::Idle;
          nextWifiAttemptMs = nowMs + WIFI_RETRY_INTERVAL_MS;
        }
      }
      break;
  }
}

void serviceMqtt(uint32_t nowMs) {
  if (!mqttStarted || WiFi.status() != WL_CONNECTED) return;
  mqtt.loop();
  if (mqtt.isConnected() && !mqttWasConnected) {
    mqttWasConnected = true;
    haIdentityPublished = false;
    haStateRefreshPending = true;
    haStateCursor = 0;
    haStatesSinceEvent = 0;
    queueEvent("states_queued", "ha_connect", "home_assistant");
  } else if (!mqtt.isConnected() && mqttWasConnected) {
    mqttWasConnected = false;
    haIdentityPublished = false;
  }
  serviceHaTransmit(nowMs);
}

void onRtcAlarm() {
  rtcAlarmPending = true;
}

void configureSafePins() {
  pinMode(PIN_FAN_SWITCH, OUTPUT);
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  pinMode(PIN_LIGHT_POWER, OUTPUT);
  digitalWrite(PIN_LIGHT_POWER, LIGHT_RELAY_OPEN_LEVEL);
  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  pinMode(PIN_RTC_ALARM, INPUT_PULLUP);
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);
  pinMode(PIN_SOIL_SENSOR, INPUT);
}

}  // namespace

void setup() {
  configureSafePins();
  RuntimeWatchdog::begin16Seconds();
  Serial.begin(115200);
  analogReadResolution(12);
  configureHomeAssistant();
  const uint8_t validRtcBcd[7] = {0x00, 0x00, 0x12, 0x02, 0x11, 0x08, 0x26};
  const uint8_t invalidRtcBcd[7] = {0x00, 0x00, 0x12, 0x02, 0x11, 0x19, 0x26};
  timeRulesOk = runTimeRuleSelfTests() && SafeDs3231::validateRawTime(validRtcBcd) &&
                !SafeDs3231::validateRawTime(invalidRtcBcd);
  queueEvent(timeRulesOk ? "time_and_bcd_self_tests_pass" : "time_or_bcd_self_test_fail", "boot", "self_test");

  if (RuntimeWatchdog::wasWatchdogReset(bootResetCause)) {
    runtimeRecoveryLockout = true;
    scheduleEnabled = false;
    setRtcStatus("watchdog_recovery_lockout");
    strncpy(i2cBusStatus, "not_started_watchdog_lockout", sizeof(i2cBusStatus) - 1U);
    queueEvent("watchdog_reset_lockout", "boot", "runtime_recovery");
  } else {
    Wire.begin();
    Wire.setClock(100000UL);
    strncpy(i2cBusStatus, "initializing", sizeof(i2cBusStatus) - 1U);
    dimmer.begin(Wire);
    const bool dimmerOk = dimmer.initializeSafe();
    queueEvent(dimmerOk ? "safe_target_verified" : "safe_target_failed", "boot", "ad5263");
    const bool persistenceOk = persistentStore.begin();
    queueEvent(persistenceOk ? "read_write_verify_ok" : "boot_failed", "boot", "eeprom");
    hardPowerOff = persistenceOk && persistentStore.record().hardPowerOff != 0;
    initializeRtc();
    if (hardPowerOff || runtimeRecoveryLockout) forceSafeOutputsImmediate();
    lightSensor.begin(Wire);
    sampleSensors(millis(), true);
    strncpy(i2cBusStatus, runtimeRecoveryLockout ? "fault" : "ok", sizeof(i2cBusStatus) - 1U);
  }

  const int interruptId = digitalPinToInterrupt(PIN_RTC_ALARM);
  rtcInterruptAttached = interruptId != NOT_AN_INTERRUPT;
  if (rtcInterruptAttached) attachInterrupt(interruptId, onRtcAlarm, FALLING);
  else queueEvent("interrupt_not_supported", "boot", "rtc_irq");
  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  char versionEvent[72];
  const String ninaVersion = WiFi.firmwareVersion();
  snprintf(versionEvent, sizeof(versionEvent), "WiFiNINA_2.0.1_NINA_%s", ninaVersion.c_str());
  queueEvent(versionEvent, "boot", "network_stack");
  nextWifiAttemptMs = millis();
  queueEvent("boot_ready", "boot", "device");
}

void loop() {
  serviceOta();
  uint32_t nowMs = millis();
  processPendingCommands();
  serviceRtc(nowMs);
  serviceDst(nowMs);
  serviceDim(nowMs);
  if (!runtimeRecoveryLockout) sampleSensors(nowMs, false);
  serviceWifi(nowMs);
  serviceOta();
  nowMs = millis();
  serviceNtp(nowMs);
  serviceMqtt(nowMs);
  serviceOta();
  if (!watchdogTestArmed) RuntimeWatchdog::feed();
}
