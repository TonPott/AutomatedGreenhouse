#include "RtcEepromStorage.h"

bool RtcEepromStorage::begin() {
  available_ = (eeprom_.begin(JC_EEPROM::twiClock100kHz) == 0);
  return available_;
}

bool RtcEepromStorage::readBytes(uint32_t address, uint8_t* data, uint16_t length) {
  if (!available_ || data == nullptr) {
    return false;
  }

  return eeprom_.read(address, data, length) == 0;
}

bool RtcEepromStorage::writeBytes(uint32_t address, const uint8_t* data, uint16_t length) {
  if (!available_ || data == nullptr) {
    return false;
  }

  for (uint16_t index = 0; index < length; ++index) {
    if (eeprom_.update(address + index, data[index]) != 0) {
      return false;
    }
  }

  return true;
}

uint16_t RtcEepromStorage::capacityBytes() const {
  return RTC_EEPROM_SIZE_BYTES;
}

bool RtcEepromStorage::isAvailable() const {
  return available_;
}
