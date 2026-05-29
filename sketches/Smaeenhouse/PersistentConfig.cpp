#include "PersistentConfig.h"

#include <string.h>

#include "Config.h"

namespace {

struct PersistentConfigDataV2 {
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

  uint8_t reserved[8];
};

struct PersistentConfigBlobV2 {
  uint32_t magic;
  PersistentConfigDataV2 data;
  uint32_t crc;
};

struct PersistentConfigBlobV3 {
  uint32_t magic;
  PersistentConfigData data;
  uint32_t crc;
};

constexpr uint32_t CONFIG_MAGIC = 0x53474843UL;  // 'SGHC'
constexpr uint16_t CONFIG_SCHEMA_VERSION_V2 = 2;

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(static_cast<int32_t>(crc & 1U))));
    }
  }
  return crc;
}

bool isReasonableV2(const PersistentConfigDataV2& cfg) {
  if (cfg.schemaVersion != CONFIG_SCHEMA_VERSION_V2) {
    return false;
  }

  if (cfg.lightOnTimeMinutes > 1439 || cfg.lightOffTimeMinutes > 1439) {
    return false;
  }

  if (cfg.defaultLightDimMinutes > 1440) {
    return false;
  }

  if (cfg.lightFallbackMode > LIGHT_FALLBACK_USE_AUTO_MODE) {
    return false;
  }

  if (cfg.soilAir < 0 || cfg.soilAir > 4095 || cfg.soilWater < 0 || cfg.soilWater > 4095) {
    return false;
  }

  if (cfg.soilDepthMm < 0 || cfg.soilDepthMm > 1000) {
    return false;
  }

  return true;
}

}  // namespace

bool PersistentConfigManager::begin(TwoWire& wire) {
  configureEeprom(wire);

  if (load(current_)) {
    if (lastLoadMigrated_) {
      save(current_);
    }

    initialized_ = true;
    return true;
  }

  applyDefaults(current_);
  if (storageReady_) {
    save(current_);
  }

  initialized_ = true;
  return false;
}

bool PersistentConfigManager::configureEeprom(TwoWire& wire) {
  (void)wire;  // JC_EEPROM uses the default Wire bus.
  storageReady_ = storage_.begin();
  fault_ = !storageReady_;
  if (!storageReady_) {
    Serial.println(F("AT24C32 EEPROM not available."));
  }
  return storageReady_;
}

bool PersistentConfigManager::load(PersistentConfigData& out) {
  lastLoadMigrated_ = false;

  if (!storageReady_) {
    fault_ = true;
    return false;
  }

  uint32_t magic = 0;
  if (!storage_.readBytes(CONFIG_EEPROM_BASE_ADDRESS,
                          reinterpret_cast<uint8_t*>(&magic),
                          sizeof(magic))) {
    fault_ = true;
    return false;
  }

  if (magic != CONFIG_MAGIC) {
    return false;
  }

  uint16_t schemaVersion = 0;
  if (!storage_.readBytes(CONFIG_EEPROM_BASE_ADDRESS + sizeof(magic),
                          reinterpret_cast<uint8_t*>(&schemaVersion),
                          sizeof(schemaVersion))) {
    fault_ = true;
    return false;
  }

  if (schemaVersion == CONFIG_SCHEMA_VERSION_V2) {
    PersistentConfigBlobV2 blob{};
    if (!storage_.readBytes(CONFIG_EEPROM_BASE_ADDRESS,
                            reinterpret_cast<uint8_t*>(&blob),
                            sizeof(blob))) {
      fault_ = true;
      return false;
    }

    const uint32_t expectedCrc = crc32(reinterpret_cast<const uint8_t*>(&blob),
                                       sizeof(PersistentConfigBlobV2) - sizeof(blob.crc));
    if (expectedCrc != blob.crc || !isReasonableV2(blob.data)) {
      fault_ = true;
      return false;
    }

    applyDefaults(out);
    out.schemaVersion = CONFIG_SCHEMA_VERSION;
    out.tempHighSet = blob.data.tempHighSet;
    out.tempHighClear = blob.data.tempHighClear;
    out.tempLowSet = blob.data.tempLowSet;
    out.tempLowClear = blob.data.tempLowClear;
    out.humHighSet = blob.data.humHighSet;
    out.humHighClear = blob.data.humHighClear;
    out.humLowSet = blob.data.humLowSet;
    out.humLowClear = blob.data.humLowClear;
    out.lightOnTimeMinutes = blob.data.lightOnTimeMinutes;
    out.lightOffTimeMinutes = blob.data.lightOffTimeMinutes;
    out.defaultLightDimMinutes = blob.data.defaultLightDimMinutes;
    out.fanAutoMode = blob.data.fanAutoMode;
    out.lightAutoMode = blob.data.lightAutoMode;
    out.lightFallbackMode = blob.data.lightFallbackMode;
    out.soilAir = blob.data.soilAir;
    out.soilWater = blob.data.soilWater;
    out.soilDepthMm = blob.data.soilDepthMm;
    resetResumeState(out.lightResumeState);
    normalizeSoilCalibration(out);

    current_ = out;
    fault_ = false;
    lastLoadMigrated_ = true;
    return true;
  }

  if (schemaVersion == CONFIG_SCHEMA_VERSION) {
    PersistentConfigBlobV3 blob{};
    if (!storage_.readBytes(CONFIG_EEPROM_BASE_ADDRESS,
                            reinterpret_cast<uint8_t*>(&blob),
                            sizeof(blob))) {
      fault_ = true;
      return false;
    }

    const uint32_t expectedCrc = crc32(reinterpret_cast<const uint8_t*>(&blob),
                                       sizeof(PersistentConfigBlobV3) - sizeof(blob.crc));
    PersistentConfigData normalized = blob.data;
    const bool normalizedChanged = normalizeSoilCalibration(normalized);
    if (expectedCrc != blob.crc || !isReasonable(normalized)) {
      fault_ = true;
      return false;
    }

    out = normalized;
    current_ = normalized;
    fault_ = false;
    lastLoadMigrated_ = normalizedChanged;
    return true;
  }

  fault_ = true;
  return false;
}

bool PersistentConfigManager::save(const PersistentConfigData& cfg) {
  if (!storageReady_) {
    fault_ = true;
    return false;
  }

  if (!isReasonable(cfg)) {
    fault_ = true;
    return false;
  }

  PersistentConfigBlobV3 blob{};
  blob.magic = CONFIG_MAGIC;
  blob.data = cfg;
  blob.crc = crc32(reinterpret_cast<const uint8_t*>(&blob),
                   sizeof(PersistentConfigBlobV3) - sizeof(blob.crc));

  if (!storage_.writeBytes(CONFIG_EEPROM_BASE_ADDRESS,
                           reinterpret_cast<const uint8_t*>(&blob),
                           sizeof(blob))) {
    fault_ = true;
    return false;
  }

  current_ = cfg;
  initialized_ = true;
  fault_ = false;
  return true;
}

bool PersistentConfigManager::saveIfChanged(const PersistentConfigData& cfg) {
  if (!initialized_) {
    begin();
  }

  if (memcmp(&current_, &cfg, sizeof(PersistentConfigData)) == 0) {
    return false;
  }

  return save(cfg);
}

void PersistentConfigManager::applyDefaults(PersistentConfigData& cfg) {
  memset(&cfg, 0, sizeof(cfg));

  cfg.schemaVersion = CONFIG_SCHEMA_VERSION;

  cfg.tempHighSet = DEFAULT_TEMP_HIGH_SET;
  cfg.tempHighClear = DEFAULT_TEMP_HIGH_CLEAR;
  cfg.tempLowSet = DEFAULT_TEMP_LOW_SET;
  cfg.tempLowClear = DEFAULT_TEMP_LOW_CLEAR;

  cfg.humHighSet = DEFAULT_HUM_HIGH_SET;
  cfg.humHighClear = DEFAULT_HUM_HIGH_CLEAR;
  cfg.humLowSet = DEFAULT_HUM_LOW_SET;
  cfg.humLowClear = DEFAULT_HUM_LOW_CLEAR;

  cfg.lightOnTimeMinutes = DEFAULT_LIGHT_ON_TIME_MINUTES;
  cfg.lightOffTimeMinutes = DEFAULT_LIGHT_OFF_TIME_MINUTES;
  cfg.defaultLightDimMinutes = DEFAULT_LIGHT_DIM_MINUTES;

  cfg.fanAutoMode = DEFAULT_FAN_AUTO_MODE;
  cfg.lightAutoMode = DEFAULT_LIGHT_AUTO_MODE;
  cfg.lightFallbackMode = LIGHT_FALLBACK_USE_AUTO_MODE;

  cfg.soilAir = DEFAULT_SOIL_AIR;
  cfg.soilWater = DEFAULT_SOIL_WATER;
  cfg.soilDepthMm = DEFAULT_SOIL_DEPTH_MM;

  resetResumeState(cfg.lightResumeState);
}

const PersistentConfigData& PersistentConfigManager::current() const {
  return current_;
}

bool PersistentConfigManager::isStorageAvailable() const {
  return storageReady_ && storage_.isAvailable();
}

bool PersistentConfigManager::hasFault() const {
  return fault_;
}

bool PersistentConfigManager::isReasonable(const PersistentConfigData& cfg) const {
  if (cfg.schemaVersion != CONFIG_SCHEMA_VERSION) {
    return false;
  }

  if (cfg.lightOnTimeMinutes > 1439 || cfg.lightOffTimeMinutes > 1439) {
    return false;
  }

  if (cfg.defaultLightDimMinutes > 1440) {
    return false;
  }

  if (cfg.lightFallbackMode > LIGHT_FALLBACK_USE_AUTO_MODE) {
    return false;
  }

  if (cfg.soilAir < SOIL_CAL_MIN || cfg.soilAir > SOIL_CAL_MAX ||
      cfg.soilWater < SOIL_CAL_MIN || cfg.soilWater > SOIL_CAL_MAX) {
    return false;
  }

  if (cfg.soilDepthMm < SOIL_DEPTH_MIN_MM || cfg.soilDepthMm > SOIL_DEPTH_MAX_MM) {
    return false;
  }

  const LightResumeState& resume = cfg.lightResumeState;
  if (resume.effectiveBrightnessPercent > 100 ||
      resume.lastNonZeroBrightnessPercent > 100 ||
      resume.dimJobStartPercent > 100 ||
      resume.dimJobTargetPercent > 100 ||
      resume.hardPowerOffActive > 1 ||
      resume.haDimJobActive > 1) {
    return false;
  }

  return true;
}

void PersistentConfigManager::resetResumeState(LightResumeState& resumeState) const {
  memset(&resumeState, 0, sizeof(resumeState));
  resumeState.effectiveBrightnessPercent = 0;
  resumeState.lastNonZeroBrightnessPercent = 100;
  resumeState.hardPowerOffActive = 0;
  resumeState.haDimJobActive = 0;
  resumeState.dimJobStartPercent = 0;
  resumeState.dimJobTargetPercent = 0;
  resumeState.dimJobDurationMs = 0;
  resumeState.dimJobStartEpoch = LIGHT_RESUME_INVALID_EPOCH;
}

bool PersistentConfigManager::normalizeSoilCalibration(PersistentConfigData& cfg) const {
  const int16_t oldAir = cfg.soilAir;
  const int16_t oldWater = cfg.soilWater;
  const int16_t oldDepth = cfg.soilDepthMm;

  cfg.soilAir = constrain(cfg.soilAir, SOIL_CAL_MIN, SOIL_CAL_MAX);
  cfg.soilWater = constrain(cfg.soilWater, SOIL_CAL_MIN, SOIL_CAL_MAX);
  cfg.soilDepthMm = constrain(cfg.soilDepthMm, SOIL_DEPTH_MIN_MM, SOIL_DEPTH_MAX_MM);

  return oldAir != cfg.soilAir || oldWater != cfg.soilWater || oldDepth != cfg.soilDepthMm;
}
