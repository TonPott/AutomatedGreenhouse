#include "ClockService.h"

#include <WiFiNINA.h>
#include <WiFiUdp.h>

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

#ifndef NTP_SERVER
#error "Credentials.h must define NTP_SERVER."
#endif

#ifndef UTC_OFFSET_SECONDS
#define UTC_OFFSET_SECONDS 0
#endif

#ifndef DST_OFFSET_SECONDS
#define DST_OFFSET_SECONDS 0
#endif

namespace {

constexpr uint16_t NTP_PORT = 123;
constexpr uint16_t NTP_LOCAL_PORT = 2390;
constexpr uint16_t NTP_PACKET_SIZE = 48;
constexpr uint32_t NTP_EPOCH_OFFSET = 2208988800UL;
constexpr uint32_t NTP_RESPONSE_TIMEOUT_MS = 1500UL;

}  // namespace

void ClockService::begin() {
  rtcAvailable_ = rtc_.begin();
  if (!rtcAvailable_) {
    Serial.println(F("RTC DS3231 not found."));
    return;
  }

  rtc_.writeSqwPinMode(DS3231_OFF);
  clearAlarmState();

  const DateTime current = rtc_.now();
  timeValid_ = current.year() >= 2024 && !rtc_.lostPower();
}

void ClockService::update(uint32_t nowMs) {
  if (!rtcAvailable_) {
    return;
  }

  const uint32_t sinceAttempt = nowMs - lastSyncAttemptMs_;

  if (!timeValid_) {
    if (sinceAttempt >= NTP_RETRY_INTERVAL_MS) {
      syncFromNTP();
    }
    return;
  }

  if ((nowMs - lastSuccessfulSyncMs_) >= NTP_RESYNC_INTERVAL_MS && sinceAttempt >= NTP_RETRY_INTERVAL_MS) {
    syncFromNTP();
  }
}

bool ClockService::syncFromNTP() {
  lastSyncAttemptMs_ = millis();

  if (!rtcAvailable_ || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  uint32_t unixUtc = 0;
  if (!fetchNtpUnixTime(unixUtc)) {
    return false;
  }

  const int32_t offsetSeconds = static_cast<int32_t>(UTC_OFFSET_SECONDS) + static_cast<int32_t>(DST_OFFSET_SECONDS);
  const int64_t localEpochSigned = static_cast<int64_t>(unixUtc) + static_cast<int64_t>(offsetSeconds);
  if (localEpochSigned <= 0) {
    return false;
  }

  rtc_.adjust(DateTime(static_cast<uint32_t>(localEpochSigned)));

  timeValid_ = true;
  lastSuccessfulSyncMs_ = millis();

  if (alarmsConfigured_) {
    configureScheduleAlarms(lightOnMinutes_, lightOffMinutes_);
  }

  return true;
}

bool ClockService::configureScheduleAlarms(uint16_t onMinutes, uint16_t offMinutes) {
  lightOnMinutes_ = onMinutes;
  lightOffMinutes_ = offMinutes;
  alarmsConfigured_ = true;

  if (!rtcAvailable_) {
    return false;
  }

  clearAlarmState();

  if (!timeValid_) {
    return false;
  }

  const bool alarm1Ok = configureAlarm1ForNextOnEvent();
  const bool alarm2Ok = configureAlarm2ForNextOffEvent();
  return alarm1Ok && alarm2Ok;
}

ClockAlarmEvent ClockService::serviceAlarmFlags() {
  if (!rtcAvailable_) {
    return ClockAlarmEvent::None;
  }

  if (queuedAlarmEvent_ != ClockAlarmEvent::None) {
    const ClockAlarmEvent pending = queuedAlarmEvent_;
    queuedAlarmEvent_ = ClockAlarmEvent::None;
    return pending;
  }

  const bool alarm1Triggered = rtc_.alarmFired(1);
  const bool alarm2Triggered = rtc_.alarmFired(2);

  if (alarm1Triggered) {
    rtc_.clearAlarm(1);
    if (alarmsConfigured_ && timeValid_) {
      configureAlarm1ForNextOnEvent();
    }
  }

  if (alarm2Triggered) {
    rtc_.clearAlarm(2);
    if (alarmsConfigured_ && timeValid_) {
      configureAlarm2ForNextOffEvent();
    }
  }

  if (alarm1Triggered && alarm2Triggered) {
    // Surface both events deterministically over two loop iterations.
    queuedAlarmEvent_ = ClockAlarmEvent::LightOffAlarm;
    return ClockAlarmEvent::LightOnAlarm;
  }

  if (alarm1Triggered) {
    return ClockAlarmEvent::LightOnAlarm;
  }

  if (alarm2Triggered) {
    return ClockAlarmEvent::LightOffAlarm;
  }

  return ClockAlarmEvent::None;
}

bool ClockService::hasPendingAlarmEvent() const {
  return queuedAlarmEvent_ != ClockAlarmEvent::None;
}

DateTime ClockService::now() {
  if (!rtcAvailable_) {
    return DateTime(2000, 1, 1, 0, 0, 0);
  }

  return rtc_.now();
}

uint16_t ClockService::minutesSinceMidnight() {
  const DateTime current = now();
  return static_cast<uint16_t>((current.hour() * 60U) + current.minute());
}

bool ClockService::isTimeValid() const {
  return rtcAvailable_ && timeValid_;
}

bool ClockService::isRtcAvailable() const {
  return rtcAvailable_;
}

bool ClockService::fetchNtpUnixTime(uint32_t& unixTimeUtc) {
  WiFiUDP udp;
  if (!udp.begin(NTP_LOCAL_PORT)) {
    return false;
  }

  IPAddress ntpIp;
  if (WiFi.hostByName(NTP_SERVER, ntpIp) != 1) {
    udp.stop();
    return false;
  }

  uint8_t packetBuffer[NTP_PACKET_SIZE] = {0};
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;

  if (!udp.beginPacket(ntpIp, NTP_PORT)) {
    udp.stop();
    return false;
  }

  udp.write(packetBuffer, NTP_PACKET_SIZE);
  if (!udp.endPacket()) {
    udp.stop();
    return false;
  }

  const uint32_t startMs = millis();
  while (millis() - startMs < NTP_RESPONSE_TIMEOUT_MS) {
    const int packetSize = udp.parsePacket();
    if (packetSize >= static_cast<int>(NTP_PACKET_SIZE)) {
      udp.read(packetBuffer, NTP_PACKET_SIZE);
      udp.stop();

      const uint32_t secondsSince1900 = (static_cast<uint32_t>(packetBuffer[40]) << 24) |
                                        (static_cast<uint32_t>(packetBuffer[41]) << 16) |
                                        (static_cast<uint32_t>(packetBuffer[42]) << 8) |
                                        static_cast<uint32_t>(packetBuffer[43]);

      if (secondsSince1900 <= NTP_EPOCH_OFFSET) {
        return false;
      }

      unixTimeUtc = secondsSince1900 - NTP_EPOCH_OFFSET;
      return true;
    }

    delay(10);
  }

  udp.stop();
  return false;
}

DateTime ClockService::buildNextAlarmTime(uint16_t minutesSinceMidnightValue, const DateTime& current) const {
  const uint8_t hour = static_cast<uint8_t>(minutesSinceMidnightValue / 60U);
  const uint8_t minute = static_cast<uint8_t>(minutesSinceMidnightValue % 60U);

  DateTime candidate(current.year(), current.month(), current.day(), hour, minute, 0);
  if (candidate <= current) {
    candidate = candidate + TimeSpan(1, 0, 0, 0);
  }

  return candidate;
}

bool ClockService::configureAlarm1ForNextOnEvent() {
  const DateTime nextOn = buildNextAlarmTime(lightOnMinutes_, now());
  rtc_.clearAlarm(1);
  rtc_.disableAlarm(1);
  return rtc_.setAlarm1(nextOn, DS3231_A1_Date);
}

bool ClockService::configureAlarm2ForNextOffEvent() {
  const DateTime nextOff = buildNextAlarmTime(lightOffMinutes_, now());
  rtc_.clearAlarm(2);
  rtc_.disableAlarm(2);
  return rtc_.setAlarm2(nextOff, DS3231_A2_Date);
}

void ClockService::clearAlarmState() {
  rtc_.disableAlarm(1);
  rtc_.disableAlarm(2);
  rtc_.clearAlarm(1);
  rtc_.clearAlarm(2);
  queuedAlarmEvent_ = ClockAlarmEvent::None;
}
