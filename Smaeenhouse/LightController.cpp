#include "LightController.h"
#include "Config.h"

void LightController::begin() {
  pinMode(PIN_LIGHT_PWM, OUTPUT);
  pinMode(PIN_LIGHT_POWER, OUTPUT);
  digitalWrite(PIN_LIGHT_POWER, LOW);
  analogWrite(PIN_LIGHT_PWM, PWM_OFF_THRESHOLD);
}

uint8_t LightController::mapBrightness(uint8_t percent) {
  if (percent == 0) return PWM_OFF_THRESHOLD;
  uint8_t pwm = map(percent, 1, 100, PWM_MAX_ACTIVE, PWM_MIN_ACTIVE);
  if (pwm > PWM_MAX_ACTIVE) pwm = PWM_MAX_ACTIVE;
  return pwm;
}

void LightController::setBrightness(uint8_t percent) {
  brightness = percent;
  targetPWM = mapBrightness(percent);
  if (percent > 0) digitalWrite(PIN_LIGHT_POWER, HIGH);
}

void LightController::update(uint32_t now) {
  if (now - lastStep < DIM_INTERVAL_MS) return;
  lastStep = now;

  if (currentPWM < targetPWM) currentPWM += DIM_STEP;
  else if (currentPWM > targetPWM) currentPWM -= DIM_STEP;

  if (currentPWM >= PWM_OFF_THRESHOLD) {
    analogWrite(PIN_LIGHT_PWM, PWM_OFF_THRESHOLD);
    digitalWrite(PIN_LIGHT_POWER, LOW);
  } else {
    digitalWrite(PIN_LIGHT_POWER, HIGH);
    analogWrite(PIN_LIGHT_PWM, currentPWM);
  }
}

uint8_t LightController::getBrightness() const { return brightness; }
void LightController::setAutoMode(bool en) { autoMode = en; }
void LightController::forceOn() { setBrightness(100); }
void LightController::forceOff() { setBrightness(0); }
