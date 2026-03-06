#pragma once
#include <Arduino.h>

class ClockService {
public:
  void begin();
  uint16_t minutesSinceMidnight() const;
};
