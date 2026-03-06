#include "ClockService.h"
#include <WiFiNINA.h>
#include <RTClib.h>

RTC_DS1307 rtc;

void ClockService::begin() {
  rtc.begin();
}

uint16_t ClockService::minutesSinceMidnight() const {
  DateTime now = rtc.now();
  return now.hour() * 60 + now.minute();
}
