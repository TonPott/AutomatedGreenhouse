#pragma once
#include <Arduino.h>

class MoistureSensor {
public:
  void begin(int air, int water);
  int readPercent();

private:
  int airCal = 3000;
  int waterCal = 1500;
};
  