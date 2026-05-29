#include "LightController.h"

#include "Config.h"

namespace {

uint8_t clampPercent(uint8_t value) {
  return value > 100 ? 100 : value;
}

}  // namespace

void LightController::begin() {
  pinMode(PIN_LIGHT_PWM, OUTPUT);
  pinMode(PIN_LIGHT_POWER, OUTPUT);

  currentBrightnessPercent_ = 0;
  lastNonZeroBrightnessPercent_ = 100;
  hardPowerOffActive_ = false;

  activeDimJob_.active = false;
  applyOutput();
}

void LightController::update(uint32_t nowMs) {
  if (activeDimJob_.active) {
    if (activeDimJob_.durationMs == 0) {
      setBrightnessInternal(activeDimJob_.targetPercent);
      stopDimJob();
    } else {
      const uint32_t elapsed = nowMs - activeDimJob_.startMs;
      if (elapsed >= activeDimJob_.durationMs) {
        setBrightnessInternal(activeDimJob_.targetPercent);
        stopDimJob();
      } else {
        const float progress = static_cast<float>(elapsed) / static_cast<float>(activeDimJob_.durationMs);
        const int16_t start = activeDimJob_.startPercent;
        const int16_t delta = static_cast<int16_t>(activeDimJob_.targetPercent) - start;
        const int16_t interpolated = static_cast<int16_t>(start + (delta * progress));
        setBrightnessInternal(static_cast<uint8_t>(constrain(interpolated, 0, 100)));
      }
    }
  }

  applyOutput();
}

void LightController::setAutoMode(bool enabled) {
  if (lightAutoMode_ == enabled) {
    return;
  }

  lightAutoMode_ = enabled;
  stopDimJob();
}

bool LightController::isAutoMode() const {
  return lightAutoMode_;
}

void LightController::applyManualBrightness(uint8_t percent) {
  const uint8_t clamped = clampPercent(percent);
  setBrightnessInternal(clamped);
  stopDimJob();
  applyOutput();
}

void LightController::applyManualOnOff(bool on) {
  if (lightAutoMode_) {
    // In Arduino auto mode, HA ON/OFF commands are ignored by design.
    return;
  }

  if (on) {
    const uint8_t resumePercent = (currentBrightnessPercent_ > 0) ? currentBrightnessPercent_ : lastNonZeroBrightnessPercent_;
    applyManualBrightness(resumePercent);
  } else {
    applyManualBrightness(0);
  }
}

void LightController::applyHardPowerOff(bool off) {
  hardPowerOffActive_ = off;
  applyOutput();
}

void LightController::startArduinoDimJob(uint8_t targetPercent, uint32_t durationMs) {
  if (!lightAutoMode_) {
    // Arduino schedule is inactive while HA is control source.
    return;
  }

  startDimJob(ARDUINO_AUTO_SOURCE, targetPercent, durationMs);
}

void LightController::startHADimJob(uint8_t targetPercent, uint32_t durationMs) {
  if (lightAutoMode_) {
    // HA dim jobs are ignored while Arduino auto schedule is active.
    return;
  }

  startDimJob(HA_CONTROL_SOURCE, targetPercent, durationMs);
}

uint8_t LightController::getCurrentBrightness() const {
  return currentBrightnessPercent_;
}

bool LightController::isPowerOn() const {
  return !hardPowerOffActive_ && currentBrightnessPercent_ > 0;
}

bool LightController::isHardPowerOffActive() const {
  return hardPowerOffActive_;
}

uint8_t LightController::brightnessToPwm(uint8_t percent) const {
  if (percent == 0) {
    return PWM_SIGNAL_OFF;
  }

  const long mapped = map(percent, 1, 100, PWM_DARKEST_STABLE, PWM_FULL_ON);
  return static_cast<uint8_t>(constrain(mapped, PWM_FULL_ON, PWM_DARKEST_STABLE));
}

void LightController::setBrightnessInternal(uint8_t percent) {
  const uint8_t clamped = clampPercent(percent);
  currentBrightnessPercent_ = clamped;
  if (clamped > 0) {
    lastNonZeroBrightnessPercent_ = clamped;
  }
}

void LightController::startDimJob(LightControlSource source, uint8_t targetPercent, uint32_t durationMs) {
  const uint8_t clamped = clampPercent(targetPercent);

  activeDimJob_.active = true;
  activeDimJob_.source = source;
  activeDimJob_.startPercent = currentBrightnessPercent_;
  activeDimJob_.targetPercent = clamped;
  activeDimJob_.startMs = millis();
  activeDimJob_.durationMs = durationMs;
}

void LightController::stopDimJob() {
  activeDimJob_.active = false;
}

void LightController::applyOutput() {
  const bool powerOn = !hardPowerOffActive_ && currentBrightnessPercent_ > 0;
  const uint8_t targetPwm = powerOn ? brightnessToPwm(currentBrightnessPercent_) : PWM_SIGNAL_OFF;

  const bool powerPinState = powerOn;
  if (powerPinState != lastAppliedPowerPin_) {
    digitalWrite(PIN_LIGHT_POWER, powerPinState ? HIGH : LOW);
    lastAppliedPowerPin_ = powerPinState;
  }

  if (targetPwm != lastAppliedPwm_) {
    analogWrite(PIN_LIGHT_PWM, targetPwm);
    lastAppliedPwm_ = targetPwm;
  }
}
