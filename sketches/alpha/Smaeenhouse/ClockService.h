#pragma once

#include <Arduino.h>
#include <RTClib.h>

enum class ClockAlarmEvent : uint8_t {
  None = 0,
  LightOnAlarm,
  LightOffAlarm
};

class ClockService {
public:
  void begin();
  void update(uint32_t nowMs);

  bool syncFromNTP();
  bool configureScheduleAlarms(uint16_t onMinutes, uint16_t offMinutes);
  ClockAlarmEvent serviceAlarmFlags();
  bool hasPendingAlarmEvent() const;

  DateTime now();
  uint32_t currentEpoch();
  uint16_t minutesSinceMidnight();

  bool isTimeValid() const;
  bool isRtcAvailable() const;
  bool hasFault() const;

private:
  enum class NtpFailureReason : uint8_t {
    None = 0,
    DnsFailed,
    UdpBeginFailed,
    UdpBeginPacketFailed,
    UdpWriteFailed,
    UdpEndPacketFailed,
    MissingResponse,
    ShortResponse,
    InvalidTimestamp
  };

  bool fetchNtpUnixTime(uint32_t& unixTimeUtc, NtpFailureReason& failureReason);
  bool fetchWifiModuleUnixTime(uint32_t& unixTimeUtc) const;
  const __FlashStringHelper* ntpFailureReasonText(NtpFailureReason reason) const;
  DateTime buildNextAlarmTime(uint16_t minutesSinceMidnight, const DateTime& current) const;
  bool configureAlarm1ForNextOnEvent();
  bool configureAlarm2ForNextOffEvent();
  void clearAlarmState();

  RTC_DS3231 rtc_;
  bool rtcAvailable_ = false;
  bool timeValid_ = false;
  bool alarmsConfigured_ = false;

  uint16_t lightOnMinutes_ = 0;
  uint16_t lightOffMinutes_ = 0;

  ClockAlarmEvent queuedAlarmEvent_ = ClockAlarmEvent::None;

  uint32_t lastSuccessfulSyncMs_ = 0;
  uint32_t lastSyncAttemptMs_ = 0;
};
