#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <JC_EEPROM.h>

#include "Config.h"

class RtcEepromStorage {
public:
  bool begin();

  bool readBytes(uint32_t address, uint8_t* data, uint16_t length);
  bool writeBytes(uint32_t address, const uint8_t* data, uint16_t length);

  uint16_t capacityBytes() const;
  bool isAvailable() const;

private:
  // Fixed AT24C32 geometry and verified device address from project docs.
  JC_EEPROM eeprom_{JC_EEPROM::kbits_32, 1, RTC_EEPROM_PAGE_SIZE, RTC_EEPROM_I2C_ADDRESS};
  bool available_ = false;
};
