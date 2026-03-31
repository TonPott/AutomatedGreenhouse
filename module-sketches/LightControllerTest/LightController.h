#pragma once

#include <Arduino.h>

enum LightControlSource : uint8_t {
  ARDUINO_AUTO_SOURCE = 0,
  HA_CONTROL_SOURCE = 1
};

class LightController {
public:
  void begin();
  void update(uint32_t nowMs);

  void setAutoMode(bool enabled);
  bool isAutoMode() const;

  void applyManualBrightness(uint8_t percent);
  void applyManualOnOff(bool on);
  void applyHardPowerOff(bool off);

  void startArduinoDimJob(uint8_t targetPercent, uint32_t durationMs);
  void startHADimJob(uint8_t targetPercent, uint32_t durationMs);

  uint8_t getCurrentBrightness() const;
  bool isPowerOn() const;
  bool isHardPowerOffActive() const;

private:
  struct DimJob {
    bool active;
    LightControlSource source;
    uint8_t startPercent;
    uint8_t targetPercent;
    uint32_t startMs;
    uint32_t durationMs;
  };

  uint8_t brightnessToPwm(uint8_t percent) const;
  void setBrightnessInternal(uint8_t percent);
  void startDimJob(LightControlSource source, uint8_t targetPercent, uint32_t durationMs);
  void stopDimJob();
  void applyOutput();

  bool lightAutoMode_ = true;
  bool hardPowerOffActive_ = false;

  uint8_t currentBrightnessPercent_ = 0;
  uint8_t lastNonZeroBrightnessPercent_ = 100;

  uint8_t lastAppliedPwm_ = 255;
  bool lastAppliedPowerPin_ = false;

  DimJob activeDimJob_{};
};
