#pragma once

#include <Arduino.h>

class RuntimeWatchdog {
public:
  static uint8_t resetCause();
  static bool wasWatchdogReset(uint8_t cause);
  static const char* resetCauseText(uint8_t cause);
  static void begin16Seconds();
  static void feed();
};
