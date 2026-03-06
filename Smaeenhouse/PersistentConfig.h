#pragma once
#include <Arduino.h>

struct PersistentConfig {
  float tempHighSet;
  float tempHighClr;
  float humHighSet;
  float humHighClr;

  uint16_t lightOnTime;
  uint16_t lightOffTime;
  uint16_t lightDimMinutes;

  bool lightAutoMode;
  bool fanAutoMode;

  int soilAir;
  int soilWater;
  int soilDepthMm;

  uint32_t crc;
};

class ConfigStore {
public:
  bool load(PersistentConfig& cfg);
  void save(PersistentConfig& cfg);

private:
  uint32_t crc32(const uint8_t* data, size_t len);
};
