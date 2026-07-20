#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "ClockService.h"
#include "PersistentConfig.h"

enum LightControlSource : uint8_t {
  ARDUINO_AUTO_SOURCE = 0,
  HA_CONTROL_SOURCE = 1
};

class LightController {
public:
  LightController(PersistentConfigManager& configManager,
                  PersistentConfigData& configData,
                  ClockService& clockService);

  void begin(TwoWire& wire = Wire);
  void restoreOnBoot(bool preferAutoScheduleState, uint8_t scheduleTargetPercent);
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
  bool hasLightFault() const;
  const char* getLightFaultReason() const;

private:
  struct DimJob {
    bool active;
    LightControlSource source;
    uint8_t startPercent;
    uint8_t targetPercent;
    uint32_t startMs;
    uint32_t durationMs;
    uint32_t lastStepMs;
  };

  struct RdacTarget {
    uint8_t w1;
    uint8_t w2;
  };

  uint8_t clampPercent(uint8_t value) const;
  RdacTarget brightnessToRdac(uint8_t percent) const;

  void setHardwareOutputs(bool shdnReleased, bool relayOn);
  void syncOutputsToState();
  void stopDimJob(bool clearResumeJob);
  void startDimJob(LightControlSource source, uint8_t targetPercent, uint32_t durationMs);

  bool applyBrightnessInternal(uint8_t percent, bool verifyReadback);
  bool writeTargetOnce(const RdacTarget& target);
  bool verifyTargetOnce(const RdacTarget& target);
  bool writeTargetWithStrategy(const RdacTarget& target, bool verifyReadback);
  bool writeRdacChannel(uint8_t channel, uint8_t value);
  bool selectRdacChannel(uint8_t channel);
  bool readSelectedRdac(uint8_t& value);

  void persistResumeState(bool haDimJobActive,
                          uint8_t effectiveBrightnessPercent,
                          uint8_t dimJobStartPercent,
                          uint8_t dimJobTargetPercent,
                          uint32_t dimJobDurationMs,
                          uint32_t dimJobStartEpoch);
  uint32_t currentEpoch();

  void setLightFault(const char* reason);
  void clearLightFault();

  PersistentConfigManager& configManager_;
  PersistentConfigData& configData_;
  ClockService& clockService_;
  TwoWire* wire_ = nullptr;

  bool lightAutoMode_ = true;
  bool hardPowerOffActive_ = false;
  bool lightFault_ = false;

  uint8_t currentBrightnessPercent_ = 0;
  uint8_t lastNonZeroBrightnessPercent_ = 100;

  bool shdnReleased_ = false;
  bool relayOn_ = false;
  bool initialized_ = false;

  const char* lightFaultReason_ = "";
  const char* pendingFailureReason_ = "";
  DimJob activeDimJob_{};
};
