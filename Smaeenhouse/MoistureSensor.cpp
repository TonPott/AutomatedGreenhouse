#include "MoistureSensor.h"

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
  return analogRead(analogPin_);
}

void MoistureSensor::sampleNow() {
  lastRaw_ = readRawNow();
  lastPercent_ = computePercent(lastRaw_);
}

int MoistureSensor::getLastRaw() const {
  return lastRaw_;
}

uint8_t MoistureSensor::getLastPercent() const {
  return lastPercent_;
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

uint8_t MoistureSensor::computePercent(int raw) const {
  if (soilAir_ == soilWater_) {
    return 0;
  }

  const long mapped = map(raw, soilAir_, soilWater_, 0, 100);
  return static_cast<uint8_t>(constrain(mapped, 0, 100));
}
