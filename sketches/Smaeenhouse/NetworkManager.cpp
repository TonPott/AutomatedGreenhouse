#include "NetworkManager.h"

#include <WiFiNINA.h>

#include "Config.h"

#if defined(__has_include)
#if __has_include("Credentials.h")
#include "Credentials.h"
#else
#error "Missing Credentials.h in sketch folder. Copy Credentials.example.h to Credentials.h and fill values."
#endif
#else
#include "Credentials.h"
#endif

#ifndef WIFI_SSID
#error "Credentials.h must define WIFI_SSID."
#endif
#ifndef WIFI_PASSWORD
#error "Credentials.h must define WIFI_PASSWORD."
#endif

void NetworkManager::begin() {
  lastConnectionOkMs_ = millis();
  ensureWifiConnected(lastConnectionOkMs_, true);
  updateConnectionState(lastConnectionOkMs_);

  Serial.print(F("NetworkManager init: wifiConnected="));
  Serial.print(wifiConnected_ ? F("YES") : F("NO"));
  Serial.print(F(", mqttConnected="));
  Serial.print(mqttConnected_ ? F("YES") : F("NO"));
  Serial.print(F(", fallbackActive="));
  Serial.println(fallbackActive_ ? F("YES") : F("NO"));
}

void NetworkManager::update(uint32_t nowMs) {
  ensureWifiConnected(nowMs, false);
  updateConnectionState(nowMs);
}

void NetworkManager::setMqttConnected(bool connected, uint32_t nowMs) {
  mqttConnected_ = connected;
  updateConnectionState(nowMs);
}

bool NetworkManager::isWifiConnected() const {
  return wifiConnected_;
}

bool NetworkManager::isMqttConnected() const {
  return mqttConnected_;
}

bool NetworkManager::isOnline() const {
  return online_;
}

bool NetworkManager::isFallbackActive() const {
  return fallbackActive_;
}

void NetworkManager::ensureWifiConnected(uint32_t nowMs, bool force) {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiReconnectState_ != WifiReconnectState::Idle) {
      Serial.print(F("WiFi connected: ip="));
      Serial.print(WiFi.localIP());
      Serial.print(F(", rssi="));
      Serial.println(WiFi.RSSI());
    }

    wifiReconnectState_ = WifiReconnectState::Idle;
    wifiReconnectAttempt_ = 0;
    return;
  }

  if (force || wifiReconnectState_ == WifiReconnectState::Idle) {
    if (force || (nowMs - lastWifiAttemptMs_) >= WIFI_RECONNECT_INTERVAL_MS) {
      startWifiReconnect(nowMs);
    }
  }

  if (wifiReconnectState_ == WifiReconnectState::Settling) {
    if ((nowMs - wifiReconnectStateStartedMs_) < WIFI_DISCONNECT_SETTLE_MS) {
      return;
    }

    ++wifiReconnectAttempt_;
    Serial.print(F("WiFi connect attempt "));
    Serial.print(static_cast<unsigned int>(wifiReconnectAttempt_));
    Serial.print(F("/"));
    Serial.print(static_cast<unsigned int>(WIFI_CONNECT_ATTEMPTS));
    Serial.print(F(": ssid="));
    Serial.println(WIFI_SSID);

    const int beginStatus = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print(F("WiFi begin status="));
    Serial.println(beginStatus);

    wifiReconnectState_ = WifiReconnectState::Connecting;
    wifiReconnectStateStartedMs_ = nowMs;
    return;
  }

  if (wifiReconnectState_ != WifiReconnectState::Connecting) {
    return;
  }

  if ((nowMs - wifiReconnectStateStartedMs_) < WIFI_CONNECT_TIMEOUT_MS) {
    return;
  }

  Serial.print(F("WiFi connect timeout: attempt="));
  Serial.print(static_cast<unsigned int>(wifiReconnectAttempt_));
  Serial.print(F(", status="));
  Serial.println(WiFi.status());

  if (wifiReconnectAttempt_ < WIFI_CONNECT_ATTEMPTS) {
    WiFi.disconnect();
    wifiReconnectState_ = WifiReconnectState::Settling;
    wifiReconnectStateStartedMs_ = nowMs;
    return;
  }

  Serial.println(F("WiFi reconnect sequence exhausted."));
  wifiReconnectState_ = WifiReconnectState::Idle;
  wifiReconnectAttempt_ = 0;
  lastWifiAttemptMs_ = nowMs;
}

void NetworkManager::startWifiReconnect(uint32_t nowMs) {
  Serial.print(F("WiFi reconnect start: state="));
  Serial.print(static_cast<unsigned int>(wifiReconnectState_));
  Serial.print(F(", status="));
  Serial.println(WiFi.status());

  WiFi.disconnect();
  wifiReconnectState_ = WifiReconnectState::Settling;
  wifiReconnectAttempt_ = 0;
  wifiReconnectStateStartedMs_ = nowMs;
  lastWifiAttemptMs_ = nowMs;
}

void NetworkManager::updateConnectionState(uint32_t nowMs) {
  wifiConnected_ = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected_) {
    mqttConnected_ = false;
  }

  online_ = wifiConnected_ && mqttConnected_;

  if (online_) {
    fallbackActive_ = false;
    lastConnectionOkMs_ = nowMs;
  } else {
    fallbackActive_ = (nowMs - lastConnectionOkMs_) >= MQTT_FALLBACK_TIMEOUT_MS;
  }
}
