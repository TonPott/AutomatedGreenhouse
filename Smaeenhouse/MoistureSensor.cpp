#include "MoistureSensor.h"
#include "Config.h"

void MoistureSensor::begin(int air, int water) {
  airCal = air;
  waterCal = water;
}

int MoistureSensor::readPercent() {
  int raw = analogRead(PIN_SOIL_SENSOR);
  return constrain(map(raw, airCal, waterCal, 0, 100), 0, 100);
}
