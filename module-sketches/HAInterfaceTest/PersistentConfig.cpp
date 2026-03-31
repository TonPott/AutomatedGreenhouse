#include "PersistentConfig.h"

#include <string.h>

#include "Config.h"

namespace {

struct PersistentConfigBlob {
  uint32_t magic;
  PersistentConfigData data;
  uint32_t crc;
};

constexpr uint32_t CONFIG_MAGIC = 0x53474843UL;  // 'SGHC'

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

}  // namespace

bool PersistentConfigManager::begin(TwoWire& wire) {
  configureEeprom(wire);

  if (load(current_)) {
    initialized_ = true;
    return true;
  }

  applyDefaults(current_);
  save(current_);
  initialized_ = true;
  return false;
}

bool PersistentConfigManager::configureEeprom(TwoWire& wire) {
  storageReady_ = storage_.begin(wire);
  if (!storageReady_) {
    Serial.println(F("AT24C32 EEPROM not available."));
  }
  return storageReady_;
}

bool PersistentConfigManager::load(PersistentConfigData& out) {
  if (!storageReady_) {
    return false;
  }

  PersistentConfigBlob blob{};
  if (!storage_.readBytes(CONFIG_EEPROM_BASE_ADDRESS,
                          reinterpret_cast<uint8_t*>(&blob),
                          sizeof(blob))) {
    return false;
  }

  if (blob.magic != CONFIG_MAGIC) {
    return false;
  }

  const uint32_t expectedCrc = crc32(reinterpret_cast<const uint8_t*>(&blob),
                                     sizeof(PersistentConfigBlob) - sizeof(blob.crc));
  if (expectedCrc != blob.crc) {
    return false;
  }

  if (!isReasonable(blob.data)) {
    return false;
  }

  out = blob.data;
  current_ = blob.data;
  return true;
}

bool PersistentConfigManager::save(const PersistentConfigData& cfg) {
  if (!storageReady_) {
    return false;
  }

  PersistentConfigBlob blob{};
  blob.magic = CONFIG_MAGIC;
  blob.data = cfg;
  blob.crc = crc32(reinterpret_cast<const uint8_t*>(&blob),
                   sizeof(PersistentConfigBlob) - sizeof(blob.crc));

  if (!storage_.writeBytes(CONFIG_EEPROM_BASE_ADDRESS,
                           reinterpret_cast<const uint8_t*>(&blob),
                           sizeof(blob))) {
    return false;
  }

  current_ = cfg;
  initialized_ = true;
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
}

const PersistentConfigData& PersistentConfigManager::current() const {
  return current_;
}

bool PersistentConfigManager::isStorageAvailable() const {
  return storageReady_ && storage_.isAvailable();
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

  if (cfg.soilAir < 0 || cfg.soilAir > 4095 || cfg.soilWater < 0 || cfg.soilWater > 4095) {
    return false;
  }

  if (cfg.soilDepthMm < 0 || cfg.soilDepthMm > 1000) {
    return false;
  }

  return true;
}
