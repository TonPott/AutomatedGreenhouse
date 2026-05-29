#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "RtcEepromStorage.h"

enum LightFallbackMode : uint8_t {
  LIGHT_FALLBACK_TURN_OFF = 0,
  LIGHT_FALLBACK_USE_AUTO_MODE = 1
};

struct LightResumeState {
  uint8_t effectiveBrightnessPercent;
  uint8_t lastNonZeroBrightnessPercent;
  uint8_t hardPowerOffActive;
  uint8_t haDimJobActive;
  uint8_t dimJobStartPercent;
  uint8_t dimJobTargetPercent;
  uint32_t dimJobDurationMs;
  uint32_t dimJobStartEpoch;
  uint8_t reserved[2];
};

struct PersistentConfigData {
  uint16_t schemaVersion;

  float tempHighSet;
  float tempHighClear;
  float tempLowSet;
  float tempLowClear;

  float humHighSet;
  float humHighClear;
  float humLowSet;
  float humLowClear;

  uint16_t lightOnTimeMinutes;
  uint16_t lightOffTimeMinutes;
  uint16_t defaultLightDimMinutes;

  bool fanAutoMode;
  bool lightAutoMode;
  uint8_t lightFallbackMode;

  int16_t soilAir;
  int16_t soilWater;
  int16_t soilDepthMm;

  LightResumeState lightResumeState;

  uint8_t reserved[4];
};

class PersistentConfigManager {
public:
  bool begin(TwoWire& wire = Wire);
  bool configureEeprom(TwoWire& wire = Wire);
  bool load(PersistentConfigData& out);
  bool save(const PersistentConfigData& cfg);
  bool saveIfChanged(const PersistentConfigData& cfg);
  void applyDefaults(PersistentConfigData& cfg);

  const PersistentConfigData& current() const;
  bool isStorageAvailable() const;
  bool hasFault() const;

private:
  bool isReasonable(const PersistentConfigData& cfg) const;
  void resetResumeState(LightResumeState& resumeState) const;
  bool normalizeSoilCalibration(PersistentConfigData& cfg) const;

  RtcEepromStorage storage_;
  bool storageReady_ = false;
  bool initialized_ = false;
  bool fault_ = false;
  bool lastLoadMigrated_ = false;
  PersistentConfigData current_{};
};
