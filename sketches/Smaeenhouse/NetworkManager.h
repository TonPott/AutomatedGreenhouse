#pragma once

#include <Arduino.h>

class NetworkManager {
public:
  void begin();
  void update(uint32_t nowMs);

  void setMqttConnected(bool connected, uint32_t nowMs);

  bool isWifiConnected() const;
  bool isMqttConnected() const;
  bool isOnline() const;
  bool isFallbackActive() const;

private:
  enum class WifiReconnectState : uint8_t {
    Idle = 0,
    Settling,
    Connecting
  };

  void ensureWifiConnected(uint32_t nowMs, bool force);
  void startWifiReconnect(uint32_t nowMs);
  void updateConnectionState(uint32_t nowMs);

  bool wifiConnected_ = false;
  bool mqttConnected_ = false;
  bool online_ = false;
  bool fallbackActive_ = false;

  WifiReconnectState wifiReconnectState_ = WifiReconnectState::Idle;
  uint8_t wifiReconnectAttempt_ = 0;
  uint32_t wifiReconnectStateStartedMs_ = 0;
  uint32_t lastWifiAttemptMs_ = 0;
  uint32_t lastConnectionOkMs_ = 0;
};
