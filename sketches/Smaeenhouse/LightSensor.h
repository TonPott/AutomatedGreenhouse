#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_TSL2591.h>

class LightSensor {
public:
  LightSensor();

  void begin(TwoWire& wire);
  void update(uint32_t nowMs);
  void sampleNow();

  bool isAvailable() const;
  bool hasFault() const;
  bool hasValidSample() const;

  float getLastLux() const;
  uint16_t getLastFullSpectrum() const;
  uint16_t getLastInfrared() const;
  uint16_t getLastVisible() const;

private:
  bool initializeSensor();
  void markReadFailure();

  TwoWire* wire_ = nullptr;
  Adafruit_TSL2591 sensor_;

  bool available_ = false;
  bool fault_ = false;
  bool lastSampleValid_ = false;

  float lastLux_ = NAN;
  uint16_t lastFullSpectrum_ = 0;
  uint16_t lastInfrared_ = 0;
  uint16_t lastVisible_ = 0;

  uint8_t consecutiveReadFailures_ = 0;
  uint32_t lastReadMs_ = 0;
};
