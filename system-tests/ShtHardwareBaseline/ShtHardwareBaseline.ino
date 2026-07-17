#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <spi_drv.h>
#include <ArduinoOTA.h>
#include <InternalStorage.h>
#include <PubSubClient.h>
#include <SensirionI2cSht3x.h>

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

constexpr char TEST_ID[] = "sht_hardware_baseline";
constexpr char MQTT_STATUS_TOPIC[] = "smaeenhouse/test/sht_hardware_baseline/status";
constexpr char MQTT_EVENT_TOPIC[] = "smaeenhouse/test/sht_hardware_baseline/event";

constexpr uint8_t PIN_SHT_ALERT = 7;
constexpr uint8_t PIN_RTC_ALARM = 10;
constexpr uint8_t PIN_FAN_SWITCH = 2;
constexpr uint8_t PIN_FAN_TACH = A1;
constexpr uint8_t PIN_LIGHT_POWER_RELAY = 3;
constexpr uint8_t PIN_LIGHT_DIM_SHDN = 4;
constexpr uint8_t PIN_SOIL_MOISTURE = A0;
constexpr uint8_t PIN_LIGHT_SENSOR_INT = 9;

constexpr uint8_t FAN_OFF_LEVEL = LOW;
constexpr uint8_t LIGHT_RELAY_OPEN_LEVEL = LOW;
constexpr uint8_t LIGHT_DIM_SHDN_ASSERTED_LEVEL = LOW;

constexpr uint8_t I2C_ADDRESS_SHT31 = SHT30_I2C_ADDR_45;
constexpr uint8_t SHT_ADDRESS_LOW = SHT30_I2C_ADDR_44;
constexpr uint8_t SHT_ADDRESS_HIGH = SHT30_I2C_ADDR_45;

constexpr uint8_t SHT31_ALERT_READ = 0xE1;
constexpr uint8_t SHT31_ALERT_RHS = 0x1F;
constexpr uint8_t SHT31_ALERT_RHC = 0x14;
constexpr uint8_t SHT31_ALERT_RLC = 0x09;
constexpr uint8_t SHT31_ALERT_RLS = 0x02;

constexpr uint32_t NETWORK_OPERATION_TIMEOUT_MS = 1000UL;
constexpr uint32_t WIFI_SETTLE_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr uint8_t WIFI_TIMEOUTS_BEFORE_MODULE_RESET = 3;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 10000UL;
constexpr uint32_t OTA_MAX_POLL_GAP_MS = 2000UL;
constexpr uint32_t SHT_SAMPLE_INTERVAL_MS = 2000UL;
constexpr uint32_t SHT_LIMIT_INTERVAL_MS = 30000UL;
constexpr uint32_t STATUS_PUBLISH_INTERVAL_MS = 10000UL;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 10000UL;
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 896;

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

struct LimitValue {
  bool ok = false;
  uint16_t raw = 0;
  float temperature = NAN;
  float humidity = NAN;
};

struct ShtState {
  bool address44Present = false;
  bool address45Present = false;
  bool primaryPresent = false;
  bool initialized = false;
  bool measurementOk = false;
  bool statusOk = false;
  bool limitsOk = false;
  bool alertLineLow = false;
  bool alertInterruptSeen = false;
  uint16_t statusRegister = 0;
  int16_t lastMeasurementError = 0;
  int16_t lastStatusError = 0;
  uint8_t lastLimitError = 0;
  float temperature = NAN;
  float humidity = NAN;
  uint32_t sampleCount = 0;
  uint32_t limitReadCount = 0;
  LimitValue highSet;
  LimitValue highClear;
  LimitValue lowSet;
  LimitValue lowClear;
};

WiFiClient networkClient;
PubSubClient mqttClient(networkClient);
SensirionI2cSht3x shtSensor;

WifiConnectState wifiConnectState = WifiConnectState::Idle;
ShtState shtState;
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
uint32_t lastSampleMs = 0;
uint32_t lastLimitReadMs = 0;
uint32_t lastStatusPublishMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t safeStateEnforceCount = 0;
char errorMessage[96];

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

bool probeI2cAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

uint8_t crc8(const uint8_t* data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1) ^ 0x31U) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

float decodeAlertTemp(uint16_t raw) {
  const uint16_t temp9 = raw & 0x01FFU;
  return (static_cast<float>(temp9) * 175.0f / 511.0f) - 45.0f;
}

float decodeAlertHum(uint16_t raw) {
  const uint16_t hum7 = (raw >> 9) & 0x7FU;
  return static_cast<float>(hum7) * 100.0f / 127.0f;
}

bool readLimit(uint8_t lsb, LimitValue& limit) {
  Wire.beginTransmission(I2C_ADDRESS_SHT31);
  Wire.write(SHT31_ALERT_READ);
  Wire.write(lsb);
  uint8_t result = Wire.endTransmission();
  if (result != 0) {
    shtState.lastLimitError = result;
    limit.ok = false;
    return false;
  }

  delayMicroseconds(50);

  if (Wire.requestFrom(static_cast<uint8_t>(I2C_ADDRESS_SHT31), static_cast<uint8_t>(3)) != 3) {
    shtState.lastLimitError = 255;
    limit.ok = false;
    return false;
  }

  uint8_t bytes[2];
  bytes[0] = Wire.read();
  bytes[1] = Wire.read();
  const uint8_t crc = Wire.read();
  if (crc8(bytes, 2) != crc) {
    shtState.lastLimitError = 254;
    limit.ok = false;
    return false;
  }

  limit.raw = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
  limit.temperature = decodeAlertTemp(limit.raw);
  limit.humidity = decodeAlertHum(limit.raw);
  limit.ok = true;
  shtState.lastLimitError = 0;
  return true;
}

void readLimits(uint32_t nowMs, bool force) {
  if (!force && (nowMs - lastLimitReadMs) < SHT_LIMIT_INTERVAL_MS) {
    return;
  }

  lastLimitReadMs = nowMs;
  shtState.limitReadCount++;
  const bool highSetOk = readLimit(SHT31_ALERT_RHS, shtState.highSet);
  const bool highClearOk = readLimit(SHT31_ALERT_RHC, shtState.highClear);
  const bool lowSetOk = readLimit(SHT31_ALERT_RLS, shtState.lowSet);
  const bool lowClearOk = readLimit(SHT31_ALERT_RLC, shtState.lowClear);
  shtState.limitsOk = highSetOk && highClearOk && lowSetOk && lowClearOk;
}

void printError(const __FlashStringHelper* context, int16_t error) {
  if (!serialAvailable() || error == NO_ERROR) {
    return;
  }

  errorToString(error, errorMessage, sizeof(errorMessage));
  Serial.print(context);
  Serial.print(F(" failed: "));
  Serial.println(errorMessage);
}

void initializeSht() {
  shtState.address44Present = probeI2cAddress(SHT_ADDRESS_LOW);
  shtState.address45Present = probeI2cAddress(SHT_ADDRESS_HIGH);
  shtState.primaryPresent = probeI2cAddress(I2C_ADDRESS_SHT31);

  shtSensor.begin(Wire, I2C_ADDRESS_SHT31);
  shtSensor.stopMeasurement();
  delay(1);
  int16_t error = shtSensor.softReset();
  printError(F("[SHT] softReset"), error);
  delay(10);
  error = shtSensor.startPeriodicMeasurement(REPEATABILITY_MEDIUM, MPS_ONE_PER_SECOND);
  printError(F("[SHT] startPeriodicMeasurement"), error);
  shtState.initialized = error == NO_ERROR;
}

void readShtStatus() {
  uint16_t status = 0;
  const int16_t error = shtSensor.readStatusRegister(status);
  shtState.lastStatusError = error;
  if (error != NO_ERROR) {
    shtState.statusOk = false;
    printError(F("[SHT] readStatusRegister"), error);
    return;
  }

  shtState.statusRegister = status;
  shtState.statusOk = true;
}

void readMeasurementAndStatus(uint32_t nowMs, bool force) {
  if (!force && (nowMs - lastSampleMs) < SHT_SAMPLE_INTERVAL_MS) {
    return;
  }

  lastSampleMs = nowMs;
  shtState.sampleCount++;
  shtState.alertLineLow = digitalRead(PIN_SHT_ALERT) == LOW;

  if (shtAlertPending) {
    noInterrupts();
    shtAlertPending = false;
    interrupts();
    shtState.alertInterruptSeen = true;
  }

  float temperature = NAN;
  float humidity = NAN;
  const int16_t error = shtSensor.blockingReadMeasurement(temperature, humidity);
  shtState.lastMeasurementError = error;
  if (error == NO_ERROR) {
    shtState.temperature = temperature;
    shtState.humidity = humidity;
    shtState.measurementOk = true;
  } else {
    shtState.measurementOk = false;
    printError(F("[SHT] blockingReadMeasurement"), error);
  }

  readShtStatus();
}

bool statusBit(uint8_t bit) {
  return (shtState.statusRegister & (1U << bit)) != 0;
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

void publishEvent(const char* eventName) {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[160];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"uptime_s\":%lu,\"event\":\"%s\"}",
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

void appendLimitJson(char* payload, size_t payloadSize, const char* name, const LimitValue& limit) {
  char part[96];
  snprintf(part,
           sizeof(part),
           ",\"%s\":{\"ok\":%s,\"raw\":%u,\"t\":%.2f,\"rh\":%.1f}",
           name,
           limit.ok ? "true" : "false",
           limit.raw,
           static_cast<double>(limit.temperature),
           static_cast<double>(limit.humidity));
  strncat(payload, part, payloadSize - strlen(payload) - 1);
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
  const bool rtcPending = rtcAlarmPending;
  const uint32_t tachPulses = fanTachPulseCount;
  interrupts();

  char payload[820];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"uptime_s\":%lu,\"wifi\":%s,\"mqtt\":true,\"ota\":%s,\"ota_gap\":%lu,\"network\":{\"joins\":%lu,\"timeouts\":%lu,\"module_resets\":%lu},\"safe\":{\"fan\":\"off\",\"relay\":\"open\",\"shdn\":\"asserted\",\"count\":%lu},\"addr\":{\"44\":%s,\"45\":%s,\"primary\":\"0x%02X\"},\"measurement\":{\"ok\":%s,\"t\":%.2f,\"rh\":%.1f,\"err\":%d,\"samples\":%lu},\"status\":{\"ok\":%s,\"raw\":%u,\"alert\":%s,\"rh_alert\":%s,\"temp_alert\":%s,\"reset\":%s,\"cmd_err\":%s,\"crc_err\":%s,\"line_low\":%s,\"irq_seen\":%s,\"err\":%d},\"limits\":{\"ok\":%s,\"reads\":%lu,\"err\":%u",
           TEST_ID,
           static_cast<unsigned long>(nowMs / 1000UL),
           WiFi.status() == WL_CONNECTED ? "true" : "false",
           otaInitialized ? "true" : "false",
           static_cast<unsigned long>(otaPollGapViolations),
           static_cast<unsigned long>(wifiConnectionCount),
           static_cast<unsigned long>(wifiConnectTimeoutCount),
           static_cast<unsigned long>(wifiModuleResetCount),
           static_cast<unsigned long>(safeStateEnforceCount),
           shtState.address44Present ? "true" : "false",
           shtState.address45Present ? "true" : "false",
           I2C_ADDRESS_SHT31,
           shtState.measurementOk ? "true" : "false",
           static_cast<double>(shtState.temperature),
           static_cast<double>(shtState.humidity),
           shtState.lastMeasurementError,
           static_cast<unsigned long>(shtState.sampleCount),
           shtState.statusOk ? "true" : "false",
           shtState.statusRegister,
           statusBit(15) ? "true" : "false",
           statusBit(11) ? "true" : "false",
           statusBit(10) ? "true" : "false",
           statusBit(4) ? "true" : "false",
           statusBit(1) ? "true" : "false",
           statusBit(0) ? "true" : "false",
           shtState.alertLineLow ? "true" : "false",
           shtState.alertInterruptSeen ? "true" : "false",
           shtState.lastStatusError,
           shtState.limitsOk ? "true" : "false",
           static_cast<unsigned long>(shtState.limitReadCount),
           shtState.lastLimitError);
  appendLimitJson(payload, sizeof(payload), "high_set", shtState.highSet);
  appendLimitJson(payload, sizeof(payload), "high_clear", shtState.highClear);
  appendLimitJson(payload, sizeof(payload), "low_set", shtState.lowSet);
  appendLimitJson(payload, sizeof(payload), "low_clear", shtState.lowClear);
  char tail[96];
  snprintf(tail,
           sizeof(tail),
           "},\"isr\":{\"rtc\":%s,\"tach\":%lu}}",
           rtcPending ? "true" : "false",
           static_cast<unsigned long>(tachPulses));
  strncat(payload, tail, sizeof(payload) - strlen(payload) - 1);

  const bool published = mqttClient.publish(MQTT_STATUS_TOPIC, payload, true);

  if (serialAvailable()) {
    Serial.print(F("[MQTT] Published SHT status="));
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
    Serial.println(F("[WiFi] Connection lost; reconnect scheduled."));
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
            Serial.println(F("[WiFi] Reinitialized NINA after repeated connect timeouts; retry scheduled."));
          }
        } else {
          wifiConnectState = WifiConnectState::Idle;
          nextWifiAttemptMs = nowMs + WIFI_RETRY_INTERVAL_MS;

          if (serialAvailable()) {
            Serial.println(F("[WiFi] Connect timeout; retry scheduled."));
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
  Serial.print(F(", addr44="));
  Serial.print(shtState.address44Present ? F("YES") : F("NO"));
  Serial.print(F(", addr45="));
  Serial.print(shtState.address45Present ? F("YES") : F("NO"));
  Serial.print(F(", temp="));
  Serial.print(shtState.temperature, 2);
  Serial.print(F(", hum="));
  Serial.print(shtState.humidity, 1);
  Serial.print(F(", alert_line_low="));
  Serial.print(shtState.alertLineLow ? F("YES") : F("NO"));
  Serial.print(F(", irq_seen="));
  Serial.print(shtState.alertInterruptSeen ? F("YES") : F("NO"));
  Serial.print(F(", status=0x"));
  Serial.print(shtState.statusRegister, HEX);
  Serial.print(F(", ota_gap_violations="));
  Serial.println(otaPollGapViolations);
}

}  // namespace

void setup() {
  configurePinsForSafeState();
  enforceSafeOutputs();

  Serial.begin(115200);
  // Native USB Serial is optional. Never wait for a host to open the port.

  Wire.begin();
  initializeSht();
  readLimits(millis(), true);
  readMeasurementAndStatus(millis(), true);

  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setSocketTimeout(NETWORK_OPERATION_TIMEOUT_MS / 1000UL);
  mqttClient.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

  nextWifiAttemptMs = millis();

  if (serialAvailable()) {
    Serial.println();
    Serial.println(F("Grow Controller SHT Hardware Baseline Test"));
    Serial.println(F("Runtime order: safe outputs -> SHT init/read -> WiFi -> OTA -> direct MQTT test topics"));
  }
}

void loop() {
  serviceOta();

  uint32_t nowMs = millis();
  readMeasurementAndStatus(nowMs, false);
  readLimits(nowMs, false);
  serviceWifi(nowMs);
  serviceOta();

  nowMs = millis();
  serviceMqtt(nowMs);
  serviceOta();

  printStatus(millis());
}
