#pragma once

#include <Arduino.h>

// 2.4 GHz Wi-Fi manager with auto-reconnect.
class WifiManager {
 public:
  void begin();
  bool connect(const String& ssid, const String& pass, uint32_t timeoutMs = 20000);
  void disconnect();
  void loop();

  bool isConnected() const;
  String ssid() const;
  String ip() const;
  int32_t rssi() const;

 private:
  bool busy_ = false;
};
