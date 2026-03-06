#pragma once
#include <Arduino.h>

class LightController {
public:
  void begin();
  void update(uint32_t now);

  void setAutoMode(bool en);
  void setBrightness(uint8_t percent);
  uint8_t getBrightness() const;

  void forceOn();
  void forceOff();

private:
  uint8_t brightness = 0;
  uint8_t targetPWM = PWM_OFF_THRESHOLD;
  uint8_t currentPWM = PWM_OFF_THRESHOLD;

  bool autoMode = true;
  uint32_t lastStep = 0;

  uint8_t mapBrightness(uint8_t percent);
};
