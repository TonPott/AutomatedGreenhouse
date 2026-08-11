#include "LightSensor.h"

#include "Config.h"

LightSensor::LightSensor() : sensor_(2591) {
}

void LightSensor::begin(TwoWire& wire) {
  wire_ = &wire;
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);
  present_ = initialize();
  fault_ = !present_;
  if (!present_) {
    fail("not_found");
  }
}

bool LightSensor::sample() {
  if (!present_ && !initialize()) {
    fail("not_found");
    return false;
  }
  if (!probe()) {
    present_ = false;
    fail("read_failed");
    return false;
  }
  const uint32_t luminosity = sensor_.getFullLuminosity();
  infrared_ = static_cast<uint16_t>(luminosity >> 16);
  fullSpectrum_ = static_cast<uint16_t>(luminosity & 0xFFFFU);
  visible_ = fullSpectrum_ >= infrared_ ? fullSpectrum_ - infrared_ : 0;
  sampleValid_ = true;
  present_ = true;
  fault_ = false;
  status_ = (fullSpectrum_ == 0xFFFFU || infrared_ == 0xFFFFU) ? "saturated" : "ok";
  return true;
}

bool LightSensor::initialize() {
  if (wire_ == nullptr || !probe() || !sensor_.begin(wire_, I2C_ADDRESS_TSL2591)) {
    present_ = false;
    return false;
  }
  sensor_.setGain(TSL2591_GAIN_LOW);
  sensor_.setTiming(TSL2591_INTEGRATIONTIME_100MS);
  sensor_.clearInterrupt();
  present_ = true;
  fault_ = false;
  status_ = "ok";
  return true;
}

bool LightSensor::probe() {
  if (wire_ == nullptr) {
    return false;
  }
  wire_->beginTransmission(I2C_ADDRESS_TSL2591);
  return wire_->endTransmission() == 0;
}

void LightSensor::fail(const char* status) {
  errorCount_++;
  sampleValid_ = false;
  fault_ = true;
  status_ = status;
}
