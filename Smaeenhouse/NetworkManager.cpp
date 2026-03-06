#include "NetworkManager.h"
#include <WiFiNINA.h>

const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASS";

void NetworkManager::begin() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void NetworkManager::update() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}
