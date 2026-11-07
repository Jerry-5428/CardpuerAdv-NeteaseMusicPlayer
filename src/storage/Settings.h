#pragma once

#include <Arduino.h>

#include "model/Types.h"

// Small NVS-backed settings store.  Wi-Fi credentials are kept in NVS too
// (they are not written to the SD card).
class Settings {
 public:
  struct Data {
    String wifiSsid;
    String wifiPass;
    AudioQuality quality = AudioQuality::Standard;
    uint8_t volume = 130;  // 0..255
    uint16_t ringKb = 48;
    PlayMode playMode = PlayMode::LoopAll;
  };

  bool begin();
  const Data& data() const { return data_; }
  Data& mutableData() { return data_; }

  void saveWifi(const String& ssid, const String& pass);
  void saveAudio();
  void saveAll();

  bool hasWifi() const { return data_.wifiSsid.length() > 0; }

 private:
  void load();
  void store();

  Data data_;
};
