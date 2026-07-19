#include <Arduino.h>
#include <WiFiNINA.h>
#include <spi_drv.h>
#include <ArduinoOTA.h>
#include <InternalStorage.h>
#include <PubSubClient.h>

#include "Credentials.h"

#ifndef WIFI_SSID
#error "Credentials.h must define WIFI_SSID."
#endif
#ifndef WIFI_PASSWORD
#error "Credentials.h must define WIFI_PASSWORD."
#endif
#ifndef MQTT_HOST
#error "Credentials.h must define MQTT_HOST."
#endif
#ifndef MQTT_PORT
#error "Credentials.h must define MQTT_PORT."
#endif
#ifndef MQTT_USERNAME
#error "Credentials.h must define MQTT_USERNAME."
#endif
#ifndef MQTT_PASSWORD
#error "Credentials.h must define MQTT_PASSWORD."
#endif
#ifndef OTA_NAME
#error "Credentials.h must define OTA_NAME."
#endif
#ifndef OTA_PASSWORD
#error "Credentials.h must define OTA_PASSWORD."
#endif

namespace {

constexpr char TEST_ID[] = "safe_installed_baseline";
constexpr char MQTT_STATUS_TOPIC[] = "smaeenhouse/test/safe_installed_baseline/status";
constexpr char MQTT_EVENT_TOPIC[] = "smaeenhouse/test/safe_installed_baseline/event";

constexpr uint8_t PIN_SHT_ALERT = 7;
constexpr uint8_t PIN_RTC_ALARM = 10;
constexpr uint8_t PIN_FAN_SWITCH = 2;
constexpr uint8_t PIN_FAN_TACH = A1;
constexpr uint8_t PIN_LIGHT_POWER_RELAY = 3;
constexpr uint8_t PIN_LIGHT_DIM_SHDN = 4;
constexpr uint8_t PIN_SOIL_MOISTURE = A0;
constexpr uint8_t PIN_LIGHT_SENSOR_INT = 9;

// Safe-state assumptions follow the production wiring documentation: the relay
// and fan switch are de-energized by LOW, while AD5263 SHDN remains asserted by
// LOW and the external pull-down during reset/boot.
constexpr uint8_t FAN_OFF_LEVEL = LOW;
constexpr uint8_t LIGHT_RELAY_OPEN_LEVEL = LOW;
constexpr uint8_t LIGHT_DIM_SHDN_ASSERTED_LEVEL = LOW;

constexpr uint32_t NETWORK_OPERATION_TIMEOUT_MS = 1000UL;
constexpr uint32_t WIFI_SETTLE_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr uint8_t WIFI_TIMEOUTS_BEFORE_MODULE_RESET = 3;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 10000UL;
constexpr uint32_t OTA_MAX_POLL_GAP_MS = 2000UL;
constexpr uint32_t STATUS_PUBLISH_INTERVAL_MS = 30000UL;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 30000UL;
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 512;

volatile bool shtAlertPending = false;
volatile bool rtcAlarmPending = false;
volatile uint32_t fanTachPulseCount = 0;

void onShtAlert() {
  shtAlertPending = true;
}

void onRtcAlarm() {
  rtcAlarmPending = true;
}

void onFanTachPulse() {
  fanTachPulseCount++;
}

enum class WifiConnectState : uint8_t {
  Idle,
  Settling,
  Connecting
};

WiFiClient networkClient;
PubSubClient mqttClient(networkClient);

WifiConnectState wifiConnectState = WifiConnectState::Idle;
bool wifiWasConnected = false;
bool mqttWasConnected = false;
bool otaInitialized = false;
uint32_t wifiStateStartedMs = 0;
uint32_t nextWifiAttemptMs = 0;
uint32_t wifiConnectionCount = 0;
uint32_t wifiConnectTimeoutCount = 0;
uint32_t wifiModuleResetCount = 0;
uint8_t consecutiveWifiConnectTimeouts = 0;
uint32_t nextMqttAttemptMs = 0;
uint32_t lastOtaPollMs = 0;
uint32_t otaPollGapViolations = 0;
uint32_t lastStatusPublishMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t safeStateEnforceCount = 0;

bool serialAvailable() {
  return static_cast<bool>(Serial);
}

void enforceSafeOutputs() {
  digitalWrite(PIN_LIGHT_POWER_RELAY, LIGHT_RELAY_OPEN_LEVEL);
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  safeStateEnforceCount++;
}

void configurePinsForSafeState() {
  digitalWrite(PIN_LIGHT_POWER_RELAY, LIGHT_RELAY_OPEN_LEVEL);
  pinMode(PIN_LIGHT_POWER_RELAY, OUTPUT);

  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  pinMode(PIN_FAN_SWITCH, OUTPUT);

  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);

  pinMode(PIN_SHT_ALERT, INPUT_PULLUP);
  pinMode(PIN_RTC_ALARM, INPUT_PULLUP);
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);
  pinMode(PIN_SOIL_MOISTURE, INPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_SHT_ALERT), onShtAlert, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_RTC_ALARM), onRtcAlarm, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), onFanTachPulse, FALLING);
}

void printIpAddress(const __FlashStringHelper* label, const IPAddress& address) {
  if (!serialAvailable()) {
    return;
  }

  Serial.print(label);
  Serial.println(address);
}

void printWifiConnected() {
  if (!serialAvailable()) {
    return;
  }

  Serial.print(F("[WiFi] Connected to "));
  Serial.println(WiFi.SSID());
  printIpAddress(F("[WiFi] IP: "), WiFi.localIP());
  Serial.print(F("[WiFi] RSSI: "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
}

void beginOta() {
  ArduinoOTA.begin(WiFi.localIP(), OTA_NAME, OTA_PASSWORD, InternalStorage);
  otaInitialized = true;
  lastOtaPollMs = millis();

  if (serialAvailable()) {
    Serial.print(F("[OTA] Enabled as "));
    Serial.println(OTA_NAME);
  }
}

void publishStatus(uint32_t nowMs, bool force) {
  if (!mqttClient.connected()) {
    return;
  }

  if (!force && (nowMs - lastStatusPublishMs) < STATUS_PUBLISH_INTERVAL_MS) {
    return;
  }

  lastStatusPublishMs = nowMs;

  noInterrupts();
  const bool shtPending = shtAlertPending;
  const bool rtcPending = rtcAlarmPending;
  const uint32_t tachPulses = fanTachPulseCount;
  interrupts();

  char payload[384];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"uptime_s\":%lu,\"wifi\":%s,\"mqtt\":true,\"ota\":%s,\"ota_gap_violations\":%lu,\"network\":{\"joins\":%lu,\"timeouts\":%lu,\"module_resets\":%lu},\"safe_state\":{\"fan\":\"off\",\"light_relay\":\"open\",\"ad5263_shdn\":\"asserted\",\"enforce_count\":%lu},\"isr_flags\":{\"sht_alert\":%s,\"rtc_alarm\":%s,\"fan_tach_pulses\":%lu}}",
           TEST_ID,
           static_cast<unsigned long>(nowMs / 1000UL),
           WiFi.status() == WL_CONNECTED ? "true" : "false",
           otaInitialized ? "true" : "false",
           static_cast<unsigned long>(otaPollGapViolations),
           static_cast<unsigned long>(wifiConnectionCount),
           static_cast<unsigned long>(wifiConnectTimeoutCount),
           static_cast<unsigned long>(wifiModuleResetCount),
           static_cast<unsigned long>(safeStateEnforceCount),
           shtPending ? "true" : "false",
           rtcPending ? "true" : "false",
           static_cast<unsigned long>(tachPulses));
  const bool published = mqttClient.publish(MQTT_STATUS_TOPIC, payload, true);

  if (serialAvailable()) {
    Serial.print(F("[MQTT] Published safe-state status="));
    Serial.println(published ? F("YES") : F("NO"));
  }
}

void publishEvent(const char* eventName) {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[192];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"uptime_s\":%lu,\"event\":\"%s\",\"safe_state\":\"enforced\"}",
           TEST_ID,
           static_cast<unsigned long>(millis() / 1000UL),
           eventName);
  const bool published = mqttClient.publish(MQTT_EVENT_TOPIC, payload, false);

  if (serialAvailable()) {
    Serial.print(F("[MQTT] Published event "));
    Serial.print(eventName);
    Serial.print(F("="));
    Serial.println(published ? F("YES") : F("NO"));
  }
}

void onWifiConnected() {
  printWifiConnected();
  beginOta();
  nextMqttAttemptMs = millis();
}

void onWifiDisconnected() {
  otaInitialized = false;

  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  mqttWasConnected = false;

  if (serialAvailable()) {
    Serial.println(F("[WiFi] Connection lost; reconnect scheduled with outputs safe."));
  }
}

void startWifiReconnect(uint32_t nowMs) {
  if (serialAvailable()) {
    Serial.print(F("[WiFi] Connecting to "));
    Serial.println(WIFI_SSID);
  }

  WiFi.disconnect();
  wifiConnectState = WifiConnectState::Settling;
  wifiStateStartedMs = nowMs;
}

void serviceWifi(uint32_t nowMs) {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;

  if (wifiConnected) {
    wifiConnectState = WifiConnectState::Idle;
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      wifiConnectionCount++;
      consecutiveWifiConnectTimeouts = 0;
      onWifiConnected();
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    onWifiDisconnected();
    wifiConnectState = WifiConnectState::Idle;
    nextWifiAttemptMs = nowMs;
  }

  switch (wifiConnectState) {
    case WifiConnectState::Idle:
      if (static_cast<int32_t>(nowMs - nextWifiAttemptMs) >= 0) {
        startWifiReconnect(nowMs);
      }
      break;

    case WifiConnectState::Settling:
      if ((nowMs - wifiStateStartedMs) >= WIFI_SETTLE_MS) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiConnectState = WifiConnectState::Connecting;
        wifiStateStartedMs = millis();
      }
      break;

    case WifiConnectState::Connecting:
      if ((nowMs - wifiStateStartedMs) >= WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect();
        wifiConnectTimeoutCount++;
        consecutiveWifiConnectTimeouts++;

        if (consecutiveWifiConnectTimeouts >= WIFI_TIMEOUTS_BEFORE_MODULE_RESET) {
          networkClient.stop();
          SpiDrv::begin(true);
          WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
          wifiModuleResetCount++;
          consecutiveWifiConnectTimeouts = 0;
          wifiConnectState = WifiConnectState::Settling;
          wifiStateStartedMs = millis();

          if (serialAvailable()) {
            Serial.println(F("[WiFi] Reinitialized NINA after repeated connect timeouts; retry scheduled with outputs safe."));
          }
        } else {
          wifiConnectState = WifiConnectState::Idle;
          nextWifiAttemptMs = nowMs + WIFI_RETRY_INTERVAL_MS;

          if (serialAvailable()) {
            Serial.println(F("[WiFi] Connect timeout; retry scheduled with outputs safe."));
          }
        }
      }
      break;
  }
}

void serviceOta() {
  if (!otaInitialized || WiFi.status() != WL_CONNECTED) {
    return;
  }

  const uint32_t beforePollMs = millis();
  const uint32_t pollGapMs = beforePollMs - lastOtaPollMs;
  if (pollGapMs > OTA_MAX_POLL_GAP_MS) {
    otaPollGapViolations++;
    if (serialAvailable()) {
      Serial.print(F("[OTA] WARNING: poll gap was "));
      Serial.print(pollGapMs);
      Serial.println(F(" ms."));
    }
  }

  ArduinoOTA.poll();
  lastOtaPollMs = millis();
}

void serviceMqtt(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    mqttWasConnected = false;
    return;
  }

  if (!mqttClient.connected()) {
    if (mqttWasConnected) {
      mqttWasConnected = false;
      if (serialAvailable()) {
        Serial.println(F("[MQTT] Disconnected."));
      }
    }

    if (static_cast<int32_t>(nowMs - nextMqttAttemptMs) < 0) {
      return;
    }

    nextMqttAttemptMs = nowMs + MQTT_RECONNECT_INTERVAL_MS;

    if (serialAvailable()) {
      Serial.print(F("[MQTT] Connecting to "));
      Serial.print(MQTT_HOST);
      Serial.print(':');
      Serial.println(MQTT_PORT);
    }

    const bool connected = mqttClient.connect(TEST_ID, MQTT_USERNAME, MQTT_PASSWORD);
    if (!connected) {
      if (serialAvailable()) {
        Serial.print(F("[MQTT] Connect failed, state="));
        Serial.println(mqttClient.state());
      }
      return;
    }

    mqttWasConnected = true;
    if (serialAvailable()) {
      Serial.println(F("[MQTT] Connected."));
    }
    publishEvent("mqtt_connected");
    publishStatus(nowMs, true);
  }

  mqttClient.loop();
  publishStatus(nowMs, false);
}

void printStatus(uint32_t nowMs) {
  if (!serialAvailable() || (nowMs - lastStatusPrintMs) < STATUS_PRINT_INTERVAL_MS) {
    return;
  }

  lastStatusPrintMs = nowMs;

  noInterrupts();
  const bool shtPending = shtAlertPending;
  const bool rtcPending = rtcAlarmPending;
  const uint32_t tachPulses = fanTachPulseCount;
  interrupts();

  Serial.print(F("[Status] uptime="));
  Serial.print(nowMs / 1000UL);
  Serial.print(F(" s, wifi="));
  Serial.print(WiFi.status() == WL_CONNECTED ? F("UP") : F("DOWN"));
  Serial.print(F(", wifi_joins="));
  Serial.print(wifiConnectionCount);
  Serial.print(F(", wifi_timeouts="));
  Serial.print(wifiConnectTimeoutCount);
  Serial.print(F(", wifi_module_resets="));
  Serial.print(wifiModuleResetCount);
  Serial.print(F(", mqtt="));
  Serial.print(mqttClient.connected() ? F("UP") : F("DOWN"));
  Serial.print(F(", ota="));
  Serial.print(otaInitialized ? F("READY") : F("WAITING_FOR_WIFI"));
  Serial.print(F(", safe=fan_off relay_open shdn_asserted"));
  Serial.print(F(", enforce_count="));
  Serial.print(safeStateEnforceCount);
  Serial.print(F(", ota_gap_violations="));
  Serial.print(otaPollGapViolations);
  Serial.print(F(", sht_alert_pending="));
  Serial.print(shtPending ? F("YES") : F("NO"));
  Serial.print(F(", rtc_alarm_pending="));
  Serial.print(rtcPending ? F("YES") : F("NO"));
  Serial.print(F(", fan_tach_pulses="));
  Serial.println(tachPulses);
}

}  // namespace

void setup() {
  configurePinsForSafeState();
  enforceSafeOutputs();

  Serial.begin(115200);
  // Native USB Serial is optional. Never wait for a host to open the port.

  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setSocketTimeout(NETWORK_OPERATION_TIMEOUT_MS / 1000UL);
  mqttClient.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

  nextWifiAttemptMs = millis();

  if (serialAvailable()) {
    Serial.println();
    Serial.println(F("Grow Controller Safe Installed-System Baseline Test"));
    Serial.println(F("Runtime order: safe outputs -> WiFi -> OTA -> direct MQTT test topics"));
    Serial.println(F("Safe state: fan off, light relay open, AD5263 SHDN asserted."));
  }
}

void loop() {
  serviceOta();

  uint32_t nowMs = millis();
  serviceWifi(nowMs);
  serviceOta();

  nowMs = millis();
  serviceMqtt(nowMs);
  serviceOta();

  printStatus(millis());
}
