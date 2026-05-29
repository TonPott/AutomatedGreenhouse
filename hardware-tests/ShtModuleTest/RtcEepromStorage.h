#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <JC_EEPROM.h>

class RtcEepromStorage {
public:
  bool begin(TwoWire& wire = Wire);

  bool readBytes(uint32_t address, uint8_t* data, uint16_t length);
  bool writeBytes(uint32_t address, const uint8_t* data, uint16_t length);

  uint16_t capacityBytes() const;
  bool isAvailable() const;

private:
  JC_EEPROM eeprom_{JC_EEPROM::kbits_32, 1, 32, 0x57};
  bool available_ = false;
};
