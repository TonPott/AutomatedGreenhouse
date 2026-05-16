#include "MoistureSensor.h"

#include <math.h>

#include "Config.h"

MoistureSensor::MoistureSensor(uint8_t analogPin) : analogPin_(analogPin) {
}

void MoistureSensor::begin(int air, int water, int depthMm) {
  setCalibration(air, water, depthMm);
  sampleNow();
  lastReadMs_ = millis();

  Serial.print(F("MoistureSensor init: raw="));
  Serial.print(lastRaw_);
  Serial.print(F(", percent="));
  Serial.print(lastPercent_);
  Serial.print(lastPercentValid_ ? F(" valid") : F(" invalid"));
  Serial.print(F(", air="));
  Serial.print(soilAir_);
  Serial.print(F(", water="));
  Serial.print(soilWater_);
  Serial.print(F(", depthMm="));
  Serial.println(soilDepthMm_);
}

void MoistureSensor::update(uint32_t nowMs) {
  if (nowMs - lastReadMs_ < SOIL_PUBLISH_INTERVAL_MS) {
    return;
  }

  sampleNow();
  lastReadMs_ = nowMs;
}

int MoistureSensor::readRawNow() const {
  return constrain(analogRead(analogPin_), SOIL_ADC_MIN, SOIL_ADC_MAX);
}

void MoistureSensor::sampleNow() {
  lastRaw_ = readRawNow();
  lastPercentValid_ = computePercent(lastRaw_, lastPercent_);
}

int MoistureSensor::getLastRaw() const {
  return lastRaw_;
}

uint8_t MoistureSensor::getLastPercent() const {
  return lastPercent_;
}

bool MoistureSensor::isLastPercentValid() const {
  return lastPercentValid_;
}

int MoistureSensor::getSoilAir() const {
  return soilAir_;
}

int MoistureSensor::getSoilWater() const {
  return soilWater_;
}

int MoistureSensor::getSoilDepthMm() const {
  return soilDepthMm_;
}

void MoistureSensor::setCalibration(int air, int water, int depthMm) {
  soilAir_ = constrain(air, SOIL_CAL_MIN, SOIL_CAL_MAX);
  soilWater_ = constrain(water, SOIL_CAL_MIN, SOIL_CAL_MAX);
  soilDepthMm_ = constrain(depthMm, SOIL_DEPTH_MIN_MM, SOIL_DEPTH_MAX_MM);
}

bool MoistureSensor::computePercent(int raw, uint8_t& percent) const {
  percent = 0;

  if (soilDepthMm_ < SOIL_MIN_VALID_DEPTH_MM || soilDepthMm_ <= 0 ||
      soilAir_ == soilWater_) {
    return false;
  }

  const float depthFactor =
      static_cast<float>(soilDepthMm_) / static_cast<float>(SOIL_REFERENCE_DEPTH_MM);
  const float denominator = static_cast<float>(soilAir_ - soilWater_) * depthFactor;
  if (depthFactor <= 0.0f || fabs(denominator) < 0.0001f) {
    return false;
  }

  const int constrainedRaw = constrain(raw, SOIL_ADC_MIN, SOIL_ADC_MAX);
  float calculated =
      (static_cast<float>(soilAir_ - constrainedRaw) / denominator) * 100.0f;
  calculated = constrain(calculated, 0.0f, 100.0f);

  percent = static_cast<uint8_t>(calculated + 0.5f);
  return true;
}
