#include "FanController.h"

#include "Config.h"

namespace {

bool pinSupportsExternalInterrupt(uint8_t pin) {
  return pin < PINS_COUNT && g_APinDescription[pin].ulExtInt != EXTERNAL_INT_NONE;
}

}  // namespace

FanController* FanController::instance_ = nullptr;

void FanController::begin() {
  instance_ = this;

  pinMode(PIN_FAN_SWITCH, OUTPUT);
  digitalWrite(PIN_FAN_SWITCH, LOW);

  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  const int interruptId = digitalPinToInterrupt(PIN_FAN_TACH);
  if (pinSupportsExternalInterrupt(PIN_FAN_TACH)) {
    attachInterrupt(interruptId, tachISR, FALLING);
    tachInterruptAttached_ = true;
  } else {
    tachInterruptAttached_ = false;
    Serial.println(F("Fan tach pin does not support interrupts."));
  }

  lastRpmCalcMs_ = millis();

  Serial.print(F("FanController init: switch pin "));
  Serial.print(PIN_FAN_SWITCH);
  Serial.print(F("=LOW, autoMode="));
  Serial.print(autoMode_ ? F("ON") : F("OFF"));
  Serial.print(F(", manualState="));
  Serial.print(manualState_ ? F("ON") : F("OFF"));
  Serial.print(F(", effectiveState="));
  Serial.print(effectiveState_ ? F("ON") : F("OFF"));
  Serial.print(F(", rpm="));
  Serial.println(rpm_);
}

void FanController::update(uint32_t nowMs) {
  const bool desiredState = autoMode_ ? autoDemand_ : manualState_;
  if (desiredState != effectiveState_) {
    effectiveState_ = desiredState;
    digitalWrite(PIN_FAN_SWITCH, effectiveState_ ? HIGH : LOW);

    if (effectiveState_) {
      runningSinceMs_ = nowMs;
      fault_ = false;
      rpm_ = 0;
      lastWindowTachPulseCount_ = 0;
      noInterrupts();
      tachPulseCount_ = 0;
      interrupts();
      lastRpmCalcMs_ = nowMs;
    } else {
      rpm_ = 0;
      fault_ = false;
      lastWindowTachPulseCount_ = 0;
      noInterrupts();
      tachPulseCount_ = 0;
      interrupts();
      lastRpmCalcMs_ = nowMs;
      runningSinceMs_ = 0;
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
  lastWindowTachPulseCount_ = pulses;
  rpm_ = static_cast<uint16_t>((pulses * 60000UL) / (2UL * elapsed));
  lastRpmCalcMs_ = nowMs;

  if (pulses > 0 || rpm_ > 0) {
    fault_ = false;
  } else if ((nowMs - runningSinceMs_) >= FAN_FAULT_GRACE_MS) {
    fault_ = true;
  }
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

bool FanController::hasFault() const {
  return fault_;
}

uint16_t FanController::getRPM() const {
  return rpm_;
}

uint32_t FanController::getTotalTachPulses() const {
  noInterrupts();
  const uint32_t pulses = totalTachPulseCount_;
  interrupts();
  return pulses;
}

uint32_t FanController::getLastWindowTachPulses() const {
  return lastWindowTachPulseCount_;
}

bool FanController::isTachInterruptAttached() const {
  return tachInterruptAttached_;
}

int FanController::getTachInputLevel() const {
  return digitalRead(PIN_FAN_TACH);
}
void FanController::tachISR() {
  if (instance_ != nullptr) {
    instance_->tachPulseCount_++;
    instance_->totalTachPulseCount_++;
  }
}
