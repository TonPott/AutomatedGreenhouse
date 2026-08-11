#include "Ad5263Controller.h"

#include "Config.h"

void Ad5263Controller::begin(TwoWire& wire) {
  wire_ = &wire;
  pinMode(PIN_LIGHT_POWER, OUTPUT);
  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);
  forceSafeOff();
}

bool Ad5263Controller::initializeSafe() {
  forceSafeOff();
  present_ = probe();
  if (!present_) {
    setFault("not_found", true);
    return false;
  }
  return apply(0, false);
}

bool Ad5263Controller::apply(uint8_t percent, bool allowPower) {
  const uint8_t requested = clampPercent(percent);
  openRelay();
  assertShutdown();
  const Target target = mapTarget(requested);
  if (!writeAndVerify(target)) {
    return false;
  }
  expected_ = target;
  currentPercent_ = requested;
  fault_ = false;
  present_ = true;
  status_ = "ok";
  if (requested > 0 && allowPower) {
    releaseShutdown();
    return closeRelay();
  }
  return true;
}

bool Ad5263Controller::applyWhilePowered(uint8_t percent) {
  const uint8_t requested = clampPercent(percent);
  if (requested == 0 || relayOpen_ || !shdnReleased_ || fault_) {
    setFault("invalid_live_state");
    return false;
  }
  const Target target = mapTarget(requested);
  if (!writeAndVerify(target)) {
    return false;
  }
  expected_ = target;
  currentPercent_ = requested;
  fault_ = false;
  status_ = "ok";
  return true;
}

bool Ad5263Controller::verifyCurrent() {
  if (!present_) {
    setFault("not_found");
    return false;
  }
  uint8_t w2 = 0;
  uint8_t w1 = 0;
  if (!readChannel(AD5263_CHANNEL_W2, w2) || !readChannel(AD5263_CHANNEL_W1, w1)) {
    setFault("readback_failed", true);
    return false;
  }
  if (w2 != expected_.w2 || w1 != expected_.w1) {
    setFault("readback_mismatch", true);
    return false;
  }
  status_ = "ok";
  return true;
}

void Ad5263Controller::hardPowerOff() {
  openRelay();
  status_ = fault_ ? status_ : "hard_power_off";
}

bool Ad5263Controller::releaseHardPowerOff(uint8_t percent) {
  return apply(percent, percent > 0);
}

void Ad5263Controller::forceSafeOff() {
  openRelay();
  assertShutdown();
}

uint8_t Ad5263Controller::clampPercent(uint8_t value) {
  return value > 100 ? 100 : value;
}

Ad5263Controller::Target Ad5263Controller::mapTarget(uint8_t percent) const {
  Target result{};
  if (percent <= LIGHT_DIM_MAPPING_SPLIT_PERCENT) {
    result.w2 = LIGHT_DIM_W2_AT_0_PERCENT;
    result.w1 = static_cast<uint8_t>(map(percent, 0, LIGHT_DIM_MAPPING_SPLIT_PERCENT,
                                         LIGHT_DIM_W1_AT_0_PERCENT, LIGHT_DIM_W1_AT_50_PERCENT));
  } else {
    result.w2 = static_cast<uint8_t>(map(percent, LIGHT_DIM_MAPPING_SPLIT_PERCENT, 100,
                                         LIGHT_DIM_W2_AT_50_PERCENT, LIGHT_DIM_W2_AT_100_PERCENT));
    result.w1 = LIGHT_DIM_W1_AT_100_PERCENT;
  }
  return result;
}

bool Ad5263Controller::probe() {
  if (wire_ == nullptr) {
    return false;
  }
  wire_->beginTransmission(I2C_ADDRESS_AD5263);
  return wire_->endTransmission() == 0;
}

bool Ad5263Controller::writeAndVerify(const Target& target) {
  for (uint8_t attempt = 0; attempt < LIGHT_DIM_COMMAND_MAX_ATTEMPTS; ++attempt) {
    if (writeChannel(AD5263_CHANNEL_W2, target.w2) &&
        writeChannel(AD5263_CHANNEL_W1, target.w1)) {
      uint8_t w2 = 0;
      uint8_t w1 = 0;
      if (readChannel(AD5263_CHANNEL_W2, w2) && readChannel(AD5263_CHANNEL_W1, w1) &&
          w2 == target.w2 && w1 == target.w1) {
        return true;
      }
    }
  }
  setFault("write_or_verify_failed", true);
  return false;
}

bool Ad5263Controller::writeChannel(uint8_t channel, uint8_t value) {
  if (wire_ == nullptr) {
    return false;
  }
  wire_->beginTransmission(I2C_ADDRESS_AD5263);
  wire_->write(static_cast<uint8_t>((channel & 0x03U) << 5));
  wire_->write(value);
  const bool ok = wire_->endTransmission() == 0;
  delay(LIGHT_DIM_WRITE_SETTLE_MS);
  return ok;
}

bool Ad5263Controller::readChannel(uint8_t channel, uint8_t& value) {
  if (wire_ == nullptr) {
    return false;
  }
  wire_->beginTransmission(I2C_ADDRESS_AD5263);
  wire_->write(static_cast<uint8_t>((channel & 0x03U) << 5));
  if (wire_->endTransmission() != 0) {
    return false;
  }
  delay(LIGHT_DIM_WRITE_SETTLE_MS);
  if (wire_->requestFrom(static_cast<int>(I2C_ADDRESS_AD5263), 1) != 1 || !wire_->available()) {
    return false;
  }
  value = wire_->read();
  return true;
}

void Ad5263Controller::openRelay() {
  digitalWrite(PIN_LIGHT_POWER, LIGHT_RELAY_OPEN_LEVEL);
  relayOpen_ = digitalRead(PIN_LIGHT_POWER) == LIGHT_RELAY_OPEN_LEVEL;
}

bool Ad5263Controller::closeRelay() {
  if (fault_ || currentPercent_ == 0 || !shdnReleased_) {
    openRelay();
    return false;
  }
  digitalWrite(PIN_LIGHT_POWER, HIGH);
  relayOpen_ = digitalRead(PIN_LIGHT_POWER) == LIGHT_RELAY_OPEN_LEVEL;
  if (relayOpen_) {
    setFault("relay_close_failed");
    return false;
  }
  return true;
}

void Ad5263Controller::assertShutdown() {
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  delay(LIGHT_DIM_BOOT_SETTLE_MS);
  shdnReleased_ = false;
}

void Ad5263Controller::releaseShutdown() {
  digitalWrite(PIN_LIGHT_DIM_SHDN, HIGH);
  delay(LIGHT_DIM_BOOT_SETTLE_MS);
  shdnReleased_ = true;
}

void Ad5263Controller::setFault(const char* status, bool countI2cError) {
  if (countI2cError) {
    errorCount_++;
  }
  fault_ = true;
  status_ = status;
  forceSafeOff();
}
