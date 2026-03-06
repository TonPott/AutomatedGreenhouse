#include "FanController.h"
#include "Config.h"

FanController* FanController::self = nullptr;

void FanController::begin() {
  self = this;
  pinMode(PIN_FAN_SWITCH, OUTPUT);
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), tachISR, FALLING);
}

void FanController::tachISR() {
  if (self) self->pulses++;
}

void FanController::alertTriggered() {
  if (autoMode) desiredOn = true;
}

void FanController::update(uint32_t now) {
  digitalWrite(PIN_FAN_SWITCH, desiredOn ? HIGH : LOW);

  if (desiredOn && now - lastRPMCalc >= FAN_RPM_INTERVAL_MS) {
    uint32_t p = pulses;
    pulses = 0;
    rpm = (p / 2) * (60000UL / FAN_RPM_INTERVAL_MS);
    lastRPMCalc = now;
  }
}

void FanController::setAutoMode(bool en) { autoMode = en; }
void FanController::manualOn() { desiredOn = true; }
void FanController::manualOff() { desiredOn = false; }

uint16_t FanController::getRPM() const { return rpm; }
bool FanController::isOn() const { return desiredOn; }
