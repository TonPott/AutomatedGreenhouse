#include "LightSensor.h"

#include <math.h>

#include "Config.h"

LightSensor::LightSensor() : sensor_(2591) {
}

void LightSensor::begin(TwoWire& wire) {
  wire_ = &wire;
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);

  available_ = initializeSensor();
  fault_ = !available_;

  if (available_) {
    sampleNow();
  }

  lastReadMs_ = millis();

  Serial.print(F("LightSensor init: available="));
  Serial.print(available_ ? F("YES") : F("NO"));
  Serial.print(F(", intPin="));
  Serial.print(PIN_LIGHT_SENSOR_INT);
  if (lastSampleValid_) {
    Serial.print(F(", lux="));
    Serial.print(lastLux_, 1);
    Serial.print(F(", full="));
    Serial.print(lastFullSpectrum_);
    Serial.print(F(", ir="));
    Serial.print(lastInfrared_);
    Serial.print(F(", visible="));
    Serial.print(lastVisible_);
  }
  Serial.println();
}

void LightSensor::update(uint32_t nowMs) {
  if (nowMs - lastReadMs_ < LIGHT_SENSOR_PUBLISH_INTERVAL_MS) {
    return;
  }

  sampleNow();
  lastReadMs_ = nowMs;
}

void LightSensor::sampleNow() {
  if (wire_ == nullptr) {
    markReadFailure();
    return;
  }

  if (!available_ && !initializeSensor()) {
    markReadFailure();
    return;
  }

  const uint32_t luminosity = sensor_.getFullLuminosity();
  const uint16_t infrared = static_cast<uint16_t>(luminosity >> 16);
  const uint16_t fullSpectrum = static_cast<uint16_t>(luminosity & 0xFFFF);
  const float lux = sensor_.calculateLux(fullSpectrum, infrared);

  if (isnan(lux) || isinf(lux) || lux < 0.0f) {
    markReadFailure();
    return;
  }

  lastInfrared_ = infrared;
  lastFullSpectrum_ = fullSpectrum;
  lastVisible_ = (fullSpectrum >= infrared) ? (fullSpectrum - infrared) : 0;
  lastLux_ = lux;
  lastSampleValid_ = true;
  consecutiveReadFailures_ = 0;
  available_ = true;
  fault_ = false;
}

bool LightSensor::isAvailable() const {
  return available_;
}

bool LightSensor::hasFault() const {
  return fault_;
}

bool LightSensor::hasValidSample() const {
  return lastSampleValid_;
}

float LightSensor::getLastLux() const {
  return lastLux_;
}

uint16_t LightSensor::getLastFullSpectrum() const {
  return lastFullSpectrum_;
}

uint16_t LightSensor::getLastInfrared() const {
  return lastInfrared_;
}

uint16_t LightSensor::getLastVisible() const {
  return lastVisible_;
}

bool LightSensor::initializeSensor() {
  if (wire_ == nullptr) {
    return false;
  }

  if (!sensor_.begin(wire_, TSL2591_I2C_ADDRESS)) {
    available_ = false;
    return false;
  }

  sensor_.setGain(TSL2591_GAIN_LOW);
  sensor_.setTiming(TSL2591_INTEGRATIONTIME_100MS);
  sensor_.clearInterrupt();

  available_ = true;
  return true;
}

void LightSensor::markReadFailure() {
  lastSampleValid_ = false;
  available_ = false;

  if (consecutiveReadFailures_ < 255) {
    consecutiveReadFailures_++;
  }

  if (consecutiveReadFailures_ >= LIGHT_SENSOR_FAULT_FAILURE_COUNT) {
    fault_ = true;
  }
}
