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
  ensureWifiConnected(0, true);

  Serial.print(F("NetworkManager init: wifiConnected="));
  Serial.print(wifiConnected_ ? F("YES") : F("NO"));
  Serial.print(F(", mqttConnected="));
  Serial.print(mqttConnected_ ? F("YES") : F("NO"));
  Serial.print(F(", fallbackActive="));
  Serial.println(fallbackActive_ ? F("YES") : F("NO"));
}

void NetworkManager::update(uint32_t nowMs) {
  ensureWifiConnected(nowMs, false);

  wifiConnected_ = (WiFi.status() == WL_CONNECTED);
  online_ = wifiConnected_ && mqttConnected_;

  if (online_) {
    fallbackActive_ = false;
    lastConnectionOkMs_ = nowMs;
  } else {
    fallbackActive_ = (nowMs - lastConnectionOkMs_) >= MQTT_FALLBACK_TIMEOUT_MS;
  }
}

void NetworkManager::setMqttConnected(bool connected, uint32_t nowMs) {
  mqttConnected_ = connected;
  online_ = wifiConnected_ && mqttConnected_;

  if (online_) {
    fallbackActive_ = false;
    lastConnectionOkMs_ = nowMs;
  }
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
  wifiConnected_ = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected_) {
    return;
  }

  if (!force && (nowMs - lastWifiAttemptMs_) < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  lastWifiAttemptMs_ = nowMs;

  Serial.print(F("Connecting WiFi SSID: "));
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}
