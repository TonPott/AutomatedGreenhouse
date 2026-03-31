#pragma once

#include <Arduino.h>

class MoistureSensor {
public:
  explicit MoistureSensor(uint8_t analogPin);

  void begin(int air, int water, int depthMm);
  void update(uint32_t nowMs);

  int readRawNow() const;
  void sampleNow();

  int getLastRaw() const;
  uint8_t getLastPercent() const;

  int getSoilAir() const;
  int getSoilWater() const;
  int getSoilDepthMm() const;

  void setCalibration(int air, int water, int depthMm);

private:
  uint8_t computePercent(int raw) const;

  const uint8_t analogPin_;

  int soilAir_ = 3000;
  int soilWater_ = 1500;
  int soilDepthMm_ = 50;

  int lastRaw_ = 0;
  uint8_t lastPercent_ = 0;

  uint32_t lastReadMs_ = 0;
};
