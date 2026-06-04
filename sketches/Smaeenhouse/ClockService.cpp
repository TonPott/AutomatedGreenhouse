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
constexpr uint32_t NTP_RESPONSE_TIMEOUT_MS = 3000UL;
constexpr uint32_t MIN_VALID_UNIX_TIME = 1704067200UL;  // 2024-01-01T00:00:00Z

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

  Serial.print(F("ClockService init: rtcAvailable="));
  Serial.print(rtcAvailable_ ? F("YES") : F("NO"));
  Serial.print(F(", timeValid="));
  Serial.print(timeValid_ ? F("YES") : F("NO"));
  Serial.print(F(", now="));
  Serial.print(current.year());
  Serial.print(F("-"));
  Serial.print(current.month());
  Serial.print(F("-"));
  Serial.print(current.day());
  Serial.print(F(" "));
  Serial.print(current.hour());
  Serial.print(F(":"));
  Serial.print(current.minute());
  Serial.print(F(":"));
  Serial.println(current.second());
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
    Serial.println(F("ClockService NTP sync skipped: RTC unavailable or WiFi not connected."));
    return false;
  }

  uint32_t unixUtc = 0;
  NtpFailureReason failureReason = NtpFailureReason::None;
  bool usedWifiModuleTime = false;

  if (!fetchNtpUnixTime(unixUtc, failureReason)) {
    Serial.print(F("ClockService UDP NTP failed: "));
    Serial.println(ntpFailureReasonText(failureReason));

    if (!fetchWifiModuleUnixTime(unixUtc)) {
      Serial.println(F("ClockService WiFi.getTime fallback/check failed: no valid module time."));
      return false;
    }

    usedWifiModuleTime = true;
    Serial.println(F("ClockService WiFi.getTime fallback/check succeeded."));
  }

  const int32_t offsetSeconds = static_cast<int32_t>(UTC_OFFSET_SECONDS) + static_cast<int32_t>(DST_OFFSET_SECONDS);
  const int64_t localEpochSigned = static_cast<int64_t>(unixUtc) + static_cast<int64_t>(offsetSeconds);
  if (localEpochSigned <= 0) {
    Serial.println(F("ClockService NTP sync failed: invalid local epoch."));
    return false;
  }

  rtc_.adjust(DateTime(static_cast<uint32_t>(localEpochSigned)));

  timeValid_ = true;
  lastSuccessfulSyncMs_ = millis();

  if (alarmsConfigured_) {
    configureScheduleAlarms(lightOnMinutes_, lightOffMinutes_);
  }

  Serial.print(F("ClockService time sync successful: source="));
  Serial.println(usedWifiModuleTime ? F("WiFi.getTime") : F("UDP NTP"));

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

uint32_t ClockService::currentEpoch() {
  if (!rtcAvailable_ || !timeValid_) {
    return LIGHT_RESUME_INVALID_EPOCH;
  }

  return rtc_.now().unixtime();
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

bool ClockService::hasFault() const {
  return !rtcAvailable_ || !timeValid_;
}

bool ClockService::fetchNtpUnixTime(uint32_t& unixTimeUtc, NtpFailureReason& failureReason) {
  failureReason = NtpFailureReason::None;

  IPAddress ntpIp;
  if (WiFi.hostByName(NTP_SERVER, ntpIp) != 1) {
    failureReason = NtpFailureReason::DnsFailed;
    return false;
  }

  WiFiUDP udp;
  if (!udp.begin(NTP_LOCAL_PORT)) {
    failureReason = NtpFailureReason::UdpBeginFailed;
    return false;
  }

  uint8_t packetBuffer[NTP_PACKET_SIZE] = {0};
  packetBuffer[0] = 0x23;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;

  if (!udp.beginPacket(ntpIp, NTP_PORT)) {
    failureReason = NtpFailureReason::UdpBeginPacketFailed;
    udp.stop();
    return false;
  }

  if (udp.write(packetBuffer, NTP_PACKET_SIZE) != NTP_PACKET_SIZE) {
    failureReason = NtpFailureReason::UdpWriteFailed;
    udp.stop();
    return false;
  }

  if (!udp.endPacket()) {
    failureReason = NtpFailureReason::UdpEndPacketFailed;
    udp.stop();
    return false;
  }

  const uint32_t startMs = millis();
  bool sawShortResponse = false;
  while (millis() - startMs < NTP_RESPONSE_TIMEOUT_MS) {
    const int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(10);
      continue;
    }

    if (packetSize < static_cast<int>(NTP_PACKET_SIZE)) {
      const size_t readSize = static_cast<size_t>(packetSize);
      udp.read(packetBuffer, readSize);
      sawShortResponse = true;
      continue;
    }

    if (udp.read(packetBuffer, NTP_PACKET_SIZE) != NTP_PACKET_SIZE) {
      failureReason = NtpFailureReason::ShortResponse;
      udp.stop();
      return false;
    }

    udp.stop();

    const uint32_t secondsSince1900 = (static_cast<uint32_t>(packetBuffer[40]) << 24) |
                                      (static_cast<uint32_t>(packetBuffer[41]) << 16) |
                                      (static_cast<uint32_t>(packetBuffer[42]) << 8) |
                                      static_cast<uint32_t>(packetBuffer[43]);

    if (secondsSince1900 <= NTP_EPOCH_OFFSET) {
      failureReason = NtpFailureReason::InvalidTimestamp;
      return false;
    }

    unixTimeUtc = secondsSince1900 - NTP_EPOCH_OFFSET;
    if (unixTimeUtc < MIN_VALID_UNIX_TIME) {
      failureReason = NtpFailureReason::InvalidTimestamp;
      return false;
    }

    return true;
  }

  udp.stop();
  failureReason = sawShortResponse ? NtpFailureReason::ShortResponse : NtpFailureReason::MissingResponse;
  return false;
}

bool ClockService::fetchWifiModuleUnixTime(uint32_t& unixTimeUtc) const {
  const unsigned long moduleTime = WiFi.getTime();
  if (moduleTime < MIN_VALID_UNIX_TIME) {
    return false;
  }

  unixTimeUtc = static_cast<uint32_t>(moduleTime);
  return true;
}

const __FlashStringHelper* ClockService::ntpFailureReasonText(NtpFailureReason reason) const {
  switch (reason) {
    case NtpFailureReason::None:
      return F("none");
    case NtpFailureReason::DnsFailed:
      return F("DNS lookup failed");
    case NtpFailureReason::UdpBeginFailed:
      return F("UDP setup failed");
    case NtpFailureReason::UdpBeginPacketFailed:
      return F("UDP beginPacket failed");
    case NtpFailureReason::UdpWriteFailed:
      return F("UDP packet write failed");
    case NtpFailureReason::UdpEndPacketFailed:
      return F("UDP packet send failed");
    case NtpFailureReason::MissingResponse:
      return F("missing UDP response");
    case NtpFailureReason::ShortResponse:
      return F("short UDP response");
    case NtpFailureReason::InvalidTimestamp:
      return F("invalid timestamp");
  }

  return F("unknown");
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
