#include "FanController.h"

#include "Config.h"

FanController* FanController::instance_ = nullptr;

void FanController::begin() {
  instance_ = this;

  pinMode(PIN_FAN_SWITCH, OUTPUT);
  digitalWrite(PIN_FAN_SWITCH, LOW);

  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  const int interruptId = digitalPinToInterrupt(PIN_FAN_TACH);
  if (interruptId >= 0) {
    attachInterrupt(interruptId, tachISR, FALLING);
  } else {
    Serial.println(F("Fan tach pin does not support interrupts."));
  }

  lastRpmCalcMs_ = millis();
}

void FanController::update(uint32_t nowMs) {
  const bool desiredState = autoMode_ ? autoDemand_ : manualState_;
  if (desiredState != effectiveState_) {
    effectiveState_ = desiredState;
    digitalWrite(PIN_FAN_SWITCH, effectiveState_ ? HIGH : LOW);

    if (!effectiveState_) {
      rpm_ = 0;
      noInterrupts();
      tachPulseCount_ = 0;
      interrupts();
      lastRpmCalcMs_ = nowMs;
    }
  }

  if (!effectiveState_) {
    return;
  }

  const uint32_t elapsed = nowMs - lastRpmCalcMs_;
  if (elapsed < FAN_RPM_PUBLISH_INTERVAL_MS) {
    return;
  }

  uint32_t pulses = 0;
  noInterrupts();
  pulses = tachPulseCount_;
  tachPulseCount_ = 0;
  interrupts();

  // 2 pulses per revolution.
  rpm_ = static_cast<uint16_t>((pulses * 60000UL) / (2UL * elapsed));
  lastRpmCalcMs_ = nowMs;
}

void FanController::setAutoMode(bool enabled) {
  autoMode_ = enabled;
}

void FanController::setManualState(bool on) {
  manualState_ = on;
}

void FanController::setAutoDemand(bool on) {
  autoDemand_ = on;
}

bool FanController::isAutoMode() const {
  return autoMode_;
}

bool FanController::getManualState() const {
  return manualState_;
}

bool FanController::isOn() const {
  return effectiveState_;
}

uint16_t FanController::getRPM() const {
  return rpm_;
}

void FanController::tachISR() {
  if (instance_ != nullptr) {
    instance_->tachPulseCount_++;
  }
}
