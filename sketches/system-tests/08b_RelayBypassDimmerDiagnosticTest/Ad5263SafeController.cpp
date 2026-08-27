#include "Ad5263SafeController.h"

#include "Config.h"

namespace {

constexpr char kNoLightFault[] = "";
constexpr char kAd5263NotFound[] = "ad5263_not_found";
constexpr char kAd5263WriteFailed[] = "ad5263_write_failed";
constexpr char kAd5263ReadbackMismatch[] = "ad5263_readback_mismatch";
constexpr char kShdnReleaseFailed[] = "shdn_release_failed";
constexpr uint8_t kKnownUnusedInjectionAddress = 0x2D;

}  // namespace

void Ad5263SafeController::begin(TwoWire& wire) {
  wire_ = &wire;

  // Preload both output latches before enabling them. The bypass diagnostic
  // must never assert SHDN because an open dimming input drives this lamp bright.
  digitalWrite(PIN_LIGHT_POWER, LOW);
  pinMode(PIN_LIGHT_POWER, OUTPUT);
  digitalWrite(PIN_LIGHT_DIM_SHDN, HIGH);
  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);

  relayCoilEnergized_ = false;
  shdnReleased_ = digitalRead(PIN_LIGHT_DIM_SHDN) == HIGH;
  clearFault();
}

bool Ad5263SafeController::initializeDimOffTarget() {
  deenergizeRelayCoil();
  if (!ensureShutdownReleased()) {
    setFault(kShdnReleaseFailed);
    return false;
  }
  present_ = probeAddress(AD5263_I2C_ADDRESS);
  if (!present_) {
    setFault(kAd5263NotFound);
    return false;
  }
  return applyBrightness(0);
}
bool Ad5263SafeController::applyBrightness(uint8_t percent) {
  if (fault_) {
    return false;
  }
  deenergizeRelayCoil();
  if (!ensureShutdownReleased()) {
    setFault(kShdnReleaseFailed);
    return false;
  }
  const uint8_t clamped = clampPercent(percent);
  const RdacTarget target = brightnessToRdac(clamped);
  if (!writeTargetWithStrategy(target)) {
    return false;
  }

  // Only advance the logical state after a byte-identical readback. On error
  // the last verified state is retained and light_fault marks it as uncertain.
  currentPercent_ = clamped;
  expectedTarget_ = target;
  applyCount_++;
  present_ = true;
  clearFault();
  return true;
}

bool Ad5263SafeController::verifyCurrentTarget() {
  if (fault_) {
    return false;
  }
  ensureShutdownReleased();
  if (!verifyTargetOnce(expectedTarget_)) {
    setFault(pendingFailureReason_);
    return false;
  }
  present_ = true;
  clearFault();
  return true;
}

bool Ad5263SafeController::ensureShutdownReleased() {
  digitalWrite(PIN_LIGHT_DIM_SHDN, HIGH);
  if (!shdnReleased_) {
    delay(LIGHT_DIM_BOOT_SETTLE_MS);
  }
  shdnReleased_ = digitalRead(PIN_LIGHT_DIM_SHDN) == HIGH;
  return shdnReleased_;
}

bool Ad5263SafeController::energizeRelayCoil() {
  if (fault_ || !present_ || !shdnReleased_ || currentPercent_ == 0) {
    deenergizeRelayCoil();
    return false;
  }

  digitalWrite(PIN_LIGHT_POWER, HIGH);
  relayCoilEnergized_ = digitalRead(PIN_LIGHT_POWER) == HIGH;
  if (!relayCoilEnergized_) {
    setFault(kAd5263WriteFailed);
    return false;
  }
  return true;
}

void Ad5263SafeController::deenergizeRelayCoil() {
  digitalWrite(PIN_LIGHT_POWER, LOW);
  relayCoilEnergized_ = digitalRead(PIN_LIGHT_POWER) == HIGH;
}

bool Ad5263SafeController::injectNotFound() {
  deenergizeRelayCoil();
  ensureShutdownReleased();
  injectedFaultCount_++;
  if (!probeAddress(kKnownUnusedInjectionAddress)) {
    present_ = false;
    setFault(kAd5263NotFound);
    return true;
  }
  setFault(kAd5263WriteFailed);
  return false;
}

void Ad5263SafeController::injectWriteFailure() {
  deenergizeRelayCoil();
  ensureShutdownReleased();
  injectedFaultCount_++;
  setFault(kAd5263WriteFailed);
}

bool Ad5263SafeController::injectReadbackMismatch() {
  deenergizeRelayCoil();
  ensureShutdownReleased();
  injectedFaultCount_++;

  uint8_t w2 = 0;
  uint8_t w1 = 0;
  if (!selectRdacChannel(AD5263_CHANNEL_W2) || !readSelectedRdac(w2) ||
      !selectRdacChannel(AD5263_CHANNEL_W1) || !readSelectedRdac(w1)) {
    setFault(pendingFailureReason_);
    return false;
  }

  readbackTarget_.w2 = w2;
  readbackTarget_.w1 = w1;
  pendingFailureReason_ = kAd5263ReadbackMismatch;
  setFault(kAd5263ReadbackMismatch);
  return true;
}

bool Ad5263SafeController::recover() {
  deenergizeRelayCoil();
  if (!ensureShutdownReleased()) {
    setFault(kShdnReleaseFailed);
    return false;
  }
  present_ = probeAddress(AD5263_I2C_ADDRESS);
  if (!present_) {
    setFault(kAd5263NotFound);
    return false;
  }

  // A fault may only be cleared inside this controlled recovery entry point.
  clearFault();
  return applyBrightness(currentPercent_);
}
bool Ad5263SafeController::isPresent() const {
  return present_;
}

bool Ad5263SafeController::isShutdownReleased() const {
  return shdnReleased_;
}

bool Ad5263SafeController::isRelayCoilEnergized() const {
  return relayCoilEnergized_;
}

bool Ad5263SafeController::hasFault() const {
  return fault_;
}

const char* Ad5263SafeController::getFaultReason() const {
  return faultReason_;
}

uint8_t Ad5263SafeController::getCurrentPercent() const {
  return currentPercent_;
}

Ad5263SafeController::RdacTarget Ad5263SafeController::getExpectedTarget() const {
  return expectedTarget_;
}

Ad5263SafeController::RdacTarget Ad5263SafeController::getReadbackTarget() const {
  return readbackTarget_;
}

uint32_t Ad5263SafeController::getApplyCount() const {
  return applyCount_;
}

uint32_t Ad5263SafeController::getVerifyCount() const {
  return verifyCount_;
}

uint32_t Ad5263SafeController::getRetryCount() const {
  return retryCount_;
}

uint32_t Ad5263SafeController::getInjectedFaultCount() const {
  return injectedFaultCount_;
}

uint8_t Ad5263SafeController::clampPercent(uint8_t value) {
  return value > 100 ? 100 : value;
}

Ad5263SafeController::RdacTarget Ad5263SafeController::brightnessToRdac(uint8_t percent) const {
  const uint8_t clamped = clampPercent(percent);
  RdacTarget target{};
  if (clamped <= LIGHT_DIM_MAPPING_SPLIT_PERCENT) {
    const long mappedW1 = map(clamped,
                              0,
                              LIGHT_DIM_MAPPING_SPLIT_PERCENT,
                              LIGHT_DIM_W1_AT_0_PERCENT,
                              LIGHT_DIM_W1_AT_50_PERCENT);
    target.w2 = static_cast<uint8_t>(constrain(LIGHT_DIM_W2_AT_0_PERCENT,
                                               LIGHT_DIM_W2_RDAC_MIN_EFFECTIVE,
                                               LIGHT_DIM_W2_RDAC_MAX_EFFECTIVE));
    target.w1 = static_cast<uint8_t>(constrain(mappedW1,
                                               LIGHT_DIM_W1_RDAC_MIN_EFFECTIVE,
                                               LIGHT_DIM_W1_RDAC_MAX_EFFECTIVE));
    return target;
  }

  const long mappedW2 = map(clamped,
                            LIGHT_DIM_MAPPING_SPLIT_PERCENT,
                            100,
                            LIGHT_DIM_W2_AT_50_PERCENT,
                            LIGHT_DIM_W2_AT_100_PERCENT);
  target.w2 = static_cast<uint8_t>(constrain(mappedW2,
                                             LIGHT_DIM_W2_RDAC_MIN_EFFECTIVE,
                                             LIGHT_DIM_W2_RDAC_MAX_EFFECTIVE));
  target.w1 = static_cast<uint8_t>(constrain(LIGHT_DIM_W1_AT_100_PERCENT,
                                             LIGHT_DIM_W1_RDAC_MIN_EFFECTIVE,
                                             LIGHT_DIM_W1_RDAC_MAX_EFFECTIVE));
  return target;
}

bool Ad5263SafeController::probeAddress(uint8_t address) {
  if (wire_ == nullptr) {
    pendingFailureReason_ = kAd5263WriteFailed;
    return false;
  }
  wire_->beginTransmission(address);
  const uint8_t status = wire_->endTransmission();
  pendingFailureReason_ = (status == 2) ? kAd5263NotFound : kAd5263WriteFailed;
  return status == 0;
}

bool Ad5263SafeController::writeTargetOnce(const RdacTarget& target) {
  if (!writeRdacChannel(AD5263_CHANNEL_W2, target.w2)) {
    return false;
  }
  return writeRdacChannel(AD5263_CHANNEL_W1, target.w1);
}

bool Ad5263SafeController::verifyTargetOnce(const RdacTarget& target) {
  verifyCount_++;
  uint8_t w2 = 0;
  uint8_t w1 = 0;
  if (!selectRdacChannel(AD5263_CHANNEL_W2) || !readSelectedRdac(w2) ||
      !selectRdacChannel(AD5263_CHANNEL_W1) || !readSelectedRdac(w1)) {
    return false;
  }

  readbackTarget_.w2 = w2;
  readbackTarget_.w1 = w1;
  if (w2 != target.w2 || w1 != target.w1) {
    pendingFailureReason_ = kAd5263ReadbackMismatch;
    return false;
  }
  return true;
}

bool Ad5263SafeController::writeTargetWithStrategy(const RdacTarget& target) {
  pendingFailureReason_ = kAd5263WriteFailed;
  for (uint8_t attempt = 0; attempt < LIGHT_DIM_COMMAND_MAX_ATTEMPTS; ++attempt) {
    if (attempt > 0) {
      retryCount_++;
    }
    if (writeTargetOnce(target) && verifyTargetOnce(target)) {
      return true;
    }
  }
  setFault(pendingFailureReason_);
  return false;
}

bool Ad5263SafeController::writeRdacChannel(uint8_t channel, uint8_t value) {
  if (wire_ == nullptr) {
    pendingFailureReason_ = kAd5263WriteFailed;
    return false;
  }
  const uint8_t instruction = static_cast<uint8_t>((channel & 0x03U) << 5);
  wire_->beginTransmission(AD5263_I2C_ADDRESS);
  wire_->write(instruction);
  wire_->write(value);
  const uint8_t status = wire_->endTransmission();
  delay(LIGHT_DIM_WRITE_SETTLE_MS);
  if (status == 0) {
    return true;
  }
  pendingFailureReason_ = (status == 2) ? kAd5263NotFound : kAd5263WriteFailed;
  present_ = status != 2;
  return false;
}

bool Ad5263SafeController::selectRdacChannel(uint8_t channel) {
  if (wire_ == nullptr) {
    pendingFailureReason_ = kAd5263WriteFailed;
    return false;
  }
  const uint8_t instruction = static_cast<uint8_t>((channel & 0x03U) << 5);
  wire_->beginTransmission(AD5263_I2C_ADDRESS);
  wire_->write(instruction);
  const uint8_t status = wire_->endTransmission();
  delay(LIGHT_DIM_WRITE_SETTLE_MS);
  if (status == 0) {
    return true;
  }
  pendingFailureReason_ = (status == 2) ? kAd5263NotFound : kAd5263WriteFailed;
  present_ = status != 2;
  return false;
}

bool Ad5263SafeController::readSelectedRdac(uint8_t& value) {
  if (wire_ == nullptr) {
    pendingFailureReason_ = kAd5263WriteFailed;
    return false;
  }
  const uint8_t bytesRead = wire_->requestFrom(static_cast<int>(AD5263_I2C_ADDRESS), 1);
  if (bytesRead != 1 || !wire_->available()) {
    pendingFailureReason_ = kAd5263NotFound;
    present_ = false;
    return false;
  }
  value = wire_->read();
  return true;
}

void Ad5263SafeController::clearFault() {
  fault_ = false;
  faultReason_ = kNoLightFault;
  pendingFailureReason_ = kNoLightFault;
}

void Ad5263SafeController::setFault(const char* reason) {
  deenergizeRelayCoil();
  ensureShutdownReleased();
  fault_ = true;
  faultReason_ = (reason != nullptr) ? reason : kAd5263WriteFailed;
}
