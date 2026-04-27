#include "LightController.h"

#include "Config.h"

namespace {

constexpr char kNoLightFault[] = "";
constexpr char kAd5263NotFound[] = "ad5263_not_found";
constexpr char kAd5263WriteFailed[] = "ad5263_write_failed";
constexpr char kAd5263ReadbackMismatch[] = "ad5263_readback_mismatch";

uint8_t clampPercentValue(uint8_t value) {
  return value > 100 ? 100 : value;
}

}  // namespace

LightController::LightController(PersistentConfigManager& configManager,
                                 PersistentConfigData& configData,
                                 ClockService& clockService)
    : configManager_(configManager),
      configData_(configData),
      clockService_(clockService) {
}

void LightController::begin(TwoWire& wire) {
  wire_ = &wire;

  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);
  pinMode(PIN_LIGHT_POWER, OUTPUT);

  lightAutoMode_ = configData_.lightAutoMode;
  currentBrightnessPercent_ = 0;
  lastNonZeroBrightnessPercent_ = configData_.lightResumeState.lastNonZeroBrightnessPercent;
  if (lastNonZeroBrightnessPercent_ == 0 || lastNonZeroBrightnessPercent_ > 100) {
    lastNonZeroBrightnessPercent_ = 100;
  }

  hardPowerOffActive_ = false;
  lightFault_ = false;
  lightFaultReason_ = kNoLightFault;
  pendingFailureReason_ = kNoLightFault;
  activeDimJob_.active = false;
  shdnReleased_ = false;
  relayOn_ = false;
  initialized_ = true;

  setHardwareOutputs(false, false);

  Serial.print(F("LightController init: shdn pin "));
  Serial.print(PIN_LIGHT_DIM_SHDN);
  Serial.print(F("=LOW, relay pin "));
  Serial.print(PIN_LIGHT_POWER);
  Serial.println(F("=LOW"));
}

void LightController::restoreOnBoot(bool preferAutoScheduleState, uint8_t scheduleTargetPercent) {
  if (!initialized_) {
    return;
  }

  stopDimJob(false);

  const LightResumeState& resume = configData_.lightResumeState;
  lastNonZeroBrightnessPercent_ =
      (resume.lastNonZeroBrightnessPercent > 0 && resume.lastNonZeroBrightnessPercent <= 100)
          ? resume.lastNonZeroBrightnessPercent
          : 100;

  uint8_t targetPercent = clampPercent(resume.effectiveBrightnessPercent);
  hardPowerOffActive_ = (!lightAutoMode_ && resume.hardPowerOffActive != 0);

  if (lightAutoMode_ && preferAutoScheduleState) {
    targetPercent = clampPercent(scheduleTargetPercent);
    hardPowerOffActive_ = false;
  }

  if (!applyBrightnessInternal(targetPercent, true)) {
    return;
  }

  if (!lightAutoMode_ && !hardPowerOffActive_ && resume.haDimJobActive != 0 &&
      clockService_.isTimeValid() && resume.dimJobStartEpoch != LIGHT_RESUME_INVALID_EPOCH &&
      resume.dimJobDurationMs > 0) {
    const uint32_t nowEpoch = currentEpoch();
    const uint32_t elapsedSeconds = (nowEpoch > resume.dimJobStartEpoch)
                                        ? (nowEpoch - resume.dimJobStartEpoch)
                                        : 0;
    const uint64_t elapsedMs = static_cast<uint64_t>(elapsedSeconds) * 1000ULL;

    if (elapsedMs >= resume.dimJobDurationMs) {
      const uint8_t finalPercent = clampPercent(resume.dimJobTargetPercent);
      if (applyBrightnessInternal(finalPercent, true)) {
        persistResumeState(false, finalPercent, 0, 0, 0, LIGHT_RESUME_INVALID_EPOCH);
      }
      return;
    }

    const uint32_t remainingMs =
        resume.dimJobDurationMs - static_cast<uint32_t>(elapsedMs);
    const int16_t delta = static_cast<int16_t>(resume.dimJobTargetPercent) -
                          static_cast<int16_t>(resume.dimJobStartPercent);
    const float progress =
        static_cast<float>(elapsedMs) / static_cast<float>(resume.dimJobDurationMs);
    const int16_t interpolated =
        static_cast<int16_t>(resume.dimJobStartPercent + (delta * progress));
    const uint8_t resumedPercent =
        clampPercent(static_cast<uint8_t>(constrain(interpolated, 0, 100)));

    if (applyBrightnessInternal(resumedPercent, true)) {
      activeDimJob_.active = true;
      activeDimJob_.source = HA_CONTROL_SOURCE;
      activeDimJob_.startPercent = resumedPercent;
      activeDimJob_.targetPercent = clampPercent(resume.dimJobTargetPercent);
      activeDimJob_.startMs = millis();
      activeDimJob_.durationMs = remainingMs;
      activeDimJob_.lastStepMs = activeDimJob_.startMs;
    }

    return;
  }

  if (!lightAutoMode_ && resume.haDimJobActive != 0 && !clockService_.isTimeValid()) {
    persistResumeState(false,
                       currentBrightnessPercent_,
                       0,
                       0,
                       0,
                       LIGHT_RESUME_INVALID_EPOCH);
  }
}

void LightController::update(uint32_t nowMs) {
  if (!activeDimJob_.active || lightFault_) {
    return;
  }

  const uint32_t elapsed = nowMs - activeDimJob_.startMs;
  if (elapsed >= activeDimJob_.durationMs) {
    const uint8_t targetPercent = clampPercent(activeDimJob_.targetPercent);
    if (applyBrightnessInternal(targetPercent, true)) {
      persistResumeState(false,
                         targetPercent,
                         0,
                         0,
                         0,
                         LIGHT_RESUME_INVALID_EPOCH);
    }

    activeDimJob_.active = false;
    return;
  }

  if ((nowMs - activeDimJob_.lastStepMs) < LIGHT_DIM_STEP_INTERVAL_MS) {
    return;
  }

  activeDimJob_.lastStepMs = nowMs;

  const float progress = static_cast<float>(elapsed) / static_cast<float>(activeDimJob_.durationMs);
  const int16_t delta = static_cast<int16_t>(activeDimJob_.targetPercent) -
                        static_cast<int16_t>(activeDimJob_.startPercent);
  const int16_t interpolated =
      static_cast<int16_t>(activeDimJob_.startPercent + (delta * progress));
  const uint8_t stepPercent =
      clampPercent(static_cast<uint8_t>(constrain(interpolated, 0, 100)));

  if (stepPercent == currentBrightnessPercent_) {
    return;
  }

  if (!applyBrightnessInternal(stepPercent, false)) {
    activeDimJob_.active = false;
  }
}

void LightController::setAutoMode(bool enabled) {
  if (lightAutoMode_ == enabled) {
    return;
  }

  stopDimJob(true);
  lightAutoMode_ = enabled;

  if (lightAutoMode_) {
    hardPowerOffActive_ = false;
  }

  syncOutputsToState();
  persistResumeState(false,
                     currentBrightnessPercent_,
                     0,
                     0,
                     0,
                     LIGHT_RESUME_INVALID_EPOCH);
}

bool LightController::isAutoMode() const {
  return lightAutoMode_;
}

void LightController::applyManualBrightness(uint8_t percent) {
  stopDimJob(true);
  const uint8_t targetPercent = clampPercent(percent);
  if (applyBrightnessInternal(targetPercent, true)) {
    persistResumeState(false,
                       targetPercent,
                       0,
                       0,
                       0,
                       LIGHT_RESUME_INVALID_EPOCH);
  }
}

void LightController::applyManualOnOff(bool on) {
  if (lightAutoMode_) {
    return;
  }

  if (on) {
    const uint8_t resumePercent =
        (currentBrightnessPercent_ > 0) ? currentBrightnessPercent_ : lastNonZeroBrightnessPercent_;
    applyManualBrightness(resumePercent);
  } else {
    applyManualBrightness(0);
  }
}

void LightController::applyHardPowerOff(bool off) {
  stopDimJob(true);
  hardPowerOffActive_ = off;
  syncOutputsToState();
  persistResumeState(false,
                     currentBrightnessPercent_,
                     0,
                     0,
                     0,
                     LIGHT_RESUME_INVALID_EPOCH);
}

void LightController::startArduinoDimJob(uint8_t targetPercent, uint32_t durationMs) {
  if (!lightAutoMode_) {
    return;
  }

  startDimJob(ARDUINO_AUTO_SOURCE, targetPercent, durationMs);
}

void LightController::startHADimJob(uint8_t targetPercent, uint32_t durationMs) {
  if (lightAutoMode_) {
    return;
  }

  startDimJob(HA_CONTROL_SOURCE, targetPercent, durationMs);
}

uint8_t LightController::getCurrentBrightness() const {
  return currentBrightnessPercent_;
}

bool LightController::isPowerOn() const {
  return relayOn_ && !lightFault_;
}

bool LightController::isHardPowerOffActive() const {
  return hardPowerOffActive_;
}

bool LightController::hasLightFault() const {
  return lightFault_;
}

const char* LightController::getLightFaultReason() const {
  return lightFaultReason_;
}

uint8_t LightController::clampPercent(uint8_t value) const {
  return clampPercentValue(value);
}

LightController::RdacTarget LightController::brightnessToRdac(uint8_t percent) const {
  const uint8_t clampedPercent = clampPercent(percent);
  if (clampedPercent <= LIGHT_DIM_MAPPING_SPLIT_PERCENT) {
    const long mappedW2 =
        map(clampedPercent,
            0,
            LIGHT_DIM_MAPPING_SPLIT_PERCENT,
            LIGHT_DIM_W2_AT_0_PERCENT,
            LIGHT_DIM_W2_AT_50_PERCENT);

    RdacTarget target{};
    target.w2 = static_cast<uint8_t>(constrain(mappedW2,
                                               LIGHT_DIM_W2_RDAC_MIN_EFFECTIVE,
                                               LIGHT_DIM_W2_RDAC_MAX_EFFECTIVE));
    target.w1 = LIGHT_DIM_W1_AT_50_PERCENT;
    return target;
  }

  const long mappedW1 =
      map(clampedPercent,
          LIGHT_DIM_MAPPING_SPLIT_PERCENT,
          100,
          LIGHT_DIM_W1_AT_50_PERCENT,
          LIGHT_DIM_W1_AT_100_PERCENT);

  RdacTarget target{};
  target.w2 = LIGHT_DIM_W2_AT_50_PERCENT;
  target.w1 = static_cast<uint8_t>(constrain(mappedW1,
                                             LIGHT_DIM_W1_RDAC_MIN_EFFECTIVE,
                                             LIGHT_DIM_W1_RDAC_MAX_EFFECTIVE));
  return target;
}

void LightController::setHardwareOutputs(bool shdnReleased, bool relayOn) {
  if (!relayOn && relayOn_) {
    digitalWrite(PIN_LIGHT_POWER, LOW);
    relayOn_ = false;
  }

  if (shdnReleased != shdnReleased_) {
    digitalWrite(PIN_LIGHT_DIM_SHDN, shdnReleased ? HIGH : LOW);
    shdnReleased_ = shdnReleased;
    delay(LIGHT_DIM_BOOT_SETTLE_MS);
  }

  if (relayOn && !relayOn_) {
    if (!shdnReleased_) {
      digitalWrite(PIN_LIGHT_DIM_SHDN, HIGH);
      shdnReleased_ = true;
      delay(LIGHT_DIM_BOOT_SETTLE_MS);
    }

    digitalWrite(PIN_LIGHT_POWER, HIGH);
    relayOn_ = true;
  }
}

void LightController::syncOutputsToState() {
  if (lightFault_) {
    setHardwareOutputs(false, false);
    return;
  }

  if (currentBrightnessPercent_ == 0) {
    setHardwareOutputs(false, false);
    return;
  }

  if (hardPowerOffActive_) {
    setHardwareOutputs(true, false);
    return;
  }

  setHardwareOutputs(true, true);
}

void LightController::stopDimJob(bool clearResumeJob) {
  if (!activeDimJob_.active) {
    return;
  }

  activeDimJob_.active = false;

  if (clearResumeJob) {
    persistResumeState(false,
                       currentBrightnessPercent_,
                       0,
                       0,
                       0,
                       LIGHT_RESUME_INVALID_EPOCH);
  }
}

void LightController::startDimJob(LightControlSource source, uint8_t targetPercent, uint32_t durationMs) {
  stopDimJob(true);

  const uint8_t clampedTarget = clampPercent(targetPercent);
  if (durationMs == 0) {
    if (applyBrightnessInternal(clampedTarget, true)) {
      persistResumeState(false,
                         clampedTarget,
                         0,
                         0,
                         0,
                         LIGHT_RESUME_INVALID_EPOCH);
    }
    return;
  }

  activeDimJob_.active = true;
  activeDimJob_.source = source;
  activeDimJob_.startPercent = currentBrightnessPercent_;
  activeDimJob_.targetPercent = clampedTarget;
  activeDimJob_.startMs = millis();
  activeDimJob_.durationMs = durationMs;
  activeDimJob_.lastStepMs = activeDimJob_.startMs;

  if (source == HA_CONTROL_SOURCE) {
    persistResumeState(true,
                       currentBrightnessPercent_,
                       currentBrightnessPercent_,
                       clampedTarget,
                       durationMs,
                       currentEpoch());
  } else {
    persistResumeState(false,
                       currentBrightnessPercent_,
                       0,
                       0,
                       0,
                       LIGHT_RESUME_INVALID_EPOCH);
  }
}

bool LightController::applyBrightnessInternal(uint8_t percent, bool verifyReadback) {
  const uint8_t clampedPercent = clampPercent(percent);
  const RdacTarget target = brightnessToRdac(clampedPercent);

  if (clampedPercent == 0) {
    setHardwareOutputs(false, false);
  } else if (!relayOn_ || hardPowerOffActive_ || !shdnReleased_) {
    setHardwareOutputs(false, false);
  }

  if (!writeTargetWithStrategy(target, verifyReadback)) {
    return false;
  }

  currentBrightnessPercent_ = clampedPercent;
  if (clampedPercent > 0) {
    lastNonZeroBrightnessPercent_ = clampedPercent;
  }

  syncOutputsToState();
  return true;
}

bool LightController::writeTargetOnce(const RdacTarget& target) {
  if (!writeRdacChannel(AD5263_CHANNEL_W2, target.w2)) {
    return false;
  }

  return writeRdacChannel(AD5263_CHANNEL_W1, target.w1);
}

bool LightController::verifyTargetOnce(const RdacTarget& target) {
  uint8_t w2Value = 0;
  if (!selectRdacChannel(AD5263_CHANNEL_W2) || !readSelectedRdac(w2Value)) {
    return false;
  }

  uint8_t w1Value = 0;
  if (!selectRdacChannel(AD5263_CHANNEL_W1) || !readSelectedRdac(w1Value)) {
    return false;
  }

  if (w2Value != target.w2 || w1Value != target.w1) {
    pendingFailureReason_ = kAd5263ReadbackMismatch;
    return false;
  }

  return true;
}

bool LightController::writeTargetWithStrategy(const RdacTarget& target, bool verifyReadback) {
  pendingFailureReason_ = kAd5263WriteFailed;

  for (uint8_t attempt = 0; attempt < LIGHT_DIM_COMMAND_MAX_ATTEMPTS; ++attempt) {
    if (!writeTargetOnce(target)) {
      continue;
    }

    if (!verifyReadback) {
      return true;
    }

    if (verifyTargetOnce(target)) {
      clearLightFault();
      return true;
    }
  }

  setLightFault(pendingFailureReason_);
  return false;
}

bool LightController::writeRdacChannel(uint8_t channel, uint8_t value) {
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
  return false;
}

bool LightController::selectRdacChannel(uint8_t channel) {
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
  return false;
}

bool LightController::readSelectedRdac(uint8_t& value) {
  if (wire_ == nullptr) {
    pendingFailureReason_ = kAd5263WriteFailed;
    return false;
  }

  const uint8_t bytesRead = wire_->requestFrom(static_cast<int>(AD5263_I2C_ADDRESS), 1);
  if (bytesRead != 1 || !wire_->available()) {
    pendingFailureReason_ = kAd5263NotFound;
    return false;
  }

  value = wire_->read();
  return true;
}

void LightController::persistResumeState(bool haDimJobActive,
                                         uint8_t effectiveBrightnessPercent,
                                         uint8_t dimJobStartPercent,
                                         uint8_t dimJobTargetPercent,
                                         uint32_t dimJobDurationMs,
                                         uint32_t dimJobStartEpoch) {
  LightResumeState& resume = configData_.lightResumeState;
  resume.effectiveBrightnessPercent = clampPercent(effectiveBrightnessPercent);
  resume.lastNonZeroBrightnessPercent = lastNonZeroBrightnessPercent_;
  resume.hardPowerOffActive = hardPowerOffActive_ ? 1 : 0;
  resume.haDimJobActive = haDimJobActive ? 1 : 0;
  resume.dimJobStartPercent = clampPercent(dimJobStartPercent);
  resume.dimJobTargetPercent = clampPercent(dimJobTargetPercent);
  resume.dimJobDurationMs = haDimJobActive ? dimJobDurationMs : 0;
  resume.dimJobStartEpoch = haDimJobActive ? dimJobStartEpoch : LIGHT_RESUME_INVALID_EPOCH;
  configManager_.saveIfChanged(configData_);
}

uint32_t LightController::currentEpoch() {
  return clockService_.currentEpoch();
}

void LightController::setLightFault(const char* reason) {
  lightFault_ = true;
  lightFaultReason_ = (reason != nullptr) ? reason : kAd5263WriteFailed;
  setHardwareOutputs(false, false);
}

void LightController::clearLightFault() {
  lightFault_ = false;
  lightFaultReason_ = kNoLightFault;
}
