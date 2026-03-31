#pragma once

#include <Arduino.h>

class FanController {
public:
  void begin();
  void update(uint32_t nowMs);

  void setAutoMode(bool enabled);
  void setManualState(bool on);
  void setAutoDemand(bool on);

  bool isAutoMode() const;
  bool getManualState() const;
  bool isOn() const;
  uint16_t getRPM() const;

private:
  static void tachISR();

  static FanController* instance_;

  volatile uint32_t tachPulseCount_ = 0;

  uint32_t lastRpmCalcMs_ = 0;
  uint16_t rpm_ = 0;

  bool autoMode_ = true;
  bool manualState_ = false;
  bool autoDemand_ = false;
  bool effectiveState_ = false;
};
