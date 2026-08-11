#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

class SafeDs3231 {
public:
  bool begin(TwoWire& wire);
  bool readTime(DateTime& value, bool& lostPower);
  bool writeTime(const DateTime& value);
  bool readAlarmFlags(uint8_t& flags);
  bool clearAlarmFlags(uint8_t flags);
  bool configureAlarms(const DateTime& alarm1, const DateTime& alarm2);

  const char* status() const { return status_; }

  static bool validateRawTime(const uint8_t raw[7]);

private:
  static uint8_t fromBcd(uint8_t value);
  static uint8_t toBcd(uint8_t value);
  static bool validBcd(uint8_t value);
  static uint8_t daysInMonth(uint16_t year, uint8_t month);

  bool readRegisters(uint8_t start, uint8_t* data, size_t length);
  bool writeRegisters(uint8_t start, const uint8_t* data, size_t length);
  bool readRegister(uint8_t address, uint8_t& value);
  bool writeRegister(uint8_t address, uint8_t value);
  bool fail(const char* status);

  TwoWire* wire_ = nullptr;
  const char* status_ = "not_initialized";
};
