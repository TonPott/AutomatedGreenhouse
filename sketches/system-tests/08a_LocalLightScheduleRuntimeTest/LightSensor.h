#pragma once

#include <Adafruit_TSL2591.h>
#include <Arduino.h>
#include <Wire.h>

class LightSensor {
public:
  LightSensor();

  void begin(TwoWire& wire);
  bool sample();

  bool isPresent() const { return present_; }
  bool hasFault() const { return fault_; }
  bool hasSample() const { return sampleValid_; }
  uint16_t fullSpectrum() const { return fullSpectrum_; }
  uint16_t infrared() const { return infrared_; }
  uint16_t visible() const { return visible_; }
  uint32_t errorCount() const { return errorCount_; }
  const char* status() const { return status_; }

private:
  bool initialize();
  bool probe();
  void fail(const char* status);

  TwoWire* wire_ = nullptr;
  Adafruit_TSL2591 sensor_;
  bool present_ = false;
  bool fault_ = false;
  bool sampleValid_ = false;
  uint16_t fullSpectrum_ = 0;
  uint16_t infrared_ = 0;
  uint16_t visible_ = 0;
  uint32_t errorCount_ = 0;
  const char* status_ = "not_initialized";
};

