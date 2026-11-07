#include "WifiManager.h"

#include <WiFi.h>

#include "Log.h"

namespace {
constexpr const char* kTag = "WIFI";
unsigned long s_lastRetry = 0;
}  // namespace

void WifiManager::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Disable modem sleep: required for glitch-free audio streaming.
  WiFi.setSleep(false);
}

bool WifiManager::connect(const String& ssid, const String& pass, uint32_t timeoutMs) {
  if (ssid.isEmpty()) return false;
  busy_ = true;
  LOGI(kTag, "connecting to %s", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(200);
  }
  busy_ = false;
  if (WiFi.status() == WL_CONNECTED) {
    LOGI(kTag, "connected, ip=%s rssi=%d", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    return true;
  }
  LOGW(kTag, "connect timeout/failed (status=%d)", (int)WiFi.status());
  return false;
}

void WifiManager::disconnect() { WiFi.disconnect(true); }

void WifiManager::loop() {
  if (busy_) return;
  if (WiFi.status() == WL_CONNECTED) return;
  const unsigned long now = millis();
  if (now - s_lastRetry < 15000) return;
  s_lastRetry = now;
  if (WiFi.getMode() == WIFI_STA) {
    LOGI(kTag, "reconnecting...");
    WiFi.reconnect();
  }
}

bool WifiManager::isConnected() const { return WiFi.status() == WL_CONNECTED; }
String WifiManager::ssid() const { return WiFi.SSID(); }
String WifiManager::ip() const { return WiFi.localIP().toString(); }
int32_t WifiManager::rssi() const { return WiFi.RSSI(); }
