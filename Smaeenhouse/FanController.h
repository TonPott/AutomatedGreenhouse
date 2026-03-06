#pragma once
#include <Arduino.h>

class FanController {
public:
  void begin();
  void update(uint32_t now);

  void setAutoMode(bool en);
  void manualOn();
  void manualOff();

  void alertTriggered();

  uint16_t getRPM() const;
  bool isOn() const;

private:
  static void tachISR();
  volatile uint32_t pulses = 0;

  uint32_t lastRPMCalc = 0;
  uint16_t rpm = 0;

  bool autoMode = true;
  bool desiredOn = false;

  static FanController* self;
};
