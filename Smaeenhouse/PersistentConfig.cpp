#include "PersistentConfig.h"
#include <FlashStorage.h>

FlashStorage(flashCfg, PersistentConfig);

uint32_t ConfigStore::crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++)
      crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
  }
  return crc;
}

bool ConfigStore::load(PersistentConfig& cfg) {
  PersistentConfig tmp = flashCfg.read();
  uint32_t crc = crc32((uint8_t*)&tmp, sizeof(PersistentConfig) - 4);
  if (crc != tmp.crc) return false;
  cfg = tmp;
  return true;
}

void ConfigStore::save(PersistentConfig& cfg) {
  cfg.crc = crc32((uint8_t*)&cfg, sizeof(PersistentConfig) - 4);
  flashCfg.write(cfg);
}
