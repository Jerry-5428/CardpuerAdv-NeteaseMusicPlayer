#include "Settings.h"

#include <Preferences.h>

#include "AppConfig.h"
#include "Log.h"

namespace {
constexpr const char* kTag = "SET";
constexpr const char* kNamespace = "ncmplayer";
Preferences s_prefs;
}  // namespace

bool Settings::begin() {
  if (!s_prefs.begin(kNamespace, false)) {
    LOGE(kTag, "NVS open failed");
    return false;
  }
  load();
  return true;
}

void Settings::load() {
  data_.wifiSsid = s_prefs.getString("ssid", "");
  data_.wifiPass = s_prefs.getString("pass", "");
  data_.quality = static_cast<AudioQuality>(s_prefs.getUChar("quality", 0));
  data_.volume = s_prefs.getUChar("volume", 130);
  const uint16_t ring = s_prefs.getUShort("ringkb", cfg::AUDIO_RING_DEFAULT / 1024);
  data_.ringKb = (ring < cfg::AUDIO_RING_MIN / 1024) ? cfg::AUDIO_RING_MIN / 1024 : ring;
  if (data_.ringKb > cfg::AUDIO_RING_MAX / 1024) data_.ringKb = cfg::AUDIO_RING_MAX / 1024;
  data_.playMode = static_cast<PlayMode>(s_prefs.getUChar("playmode", 1));
  data_.sdCache = s_prefs.getBool("sdcache", true);
}

void Settings::store() {
  s_prefs.putString("ssid", data_.wifiSsid);
  s_prefs.putString("pass", data_.wifiPass);
  s_prefs.putUChar("quality", static_cast<uint8_t>(data_.quality));
  s_prefs.putUChar("volume", data_.volume);
  s_prefs.putUShort("ringkb", data_.ringKb);
  s_prefs.putUChar("playmode", static_cast<uint8_t>(data_.playMode));
  s_prefs.putBool("sdcache", data_.sdCache);
}

void Settings::saveWifi(const String& ssid, const String& pass) {
  data_.wifiSsid = ssid;
  data_.wifiPass = pass;
  store();
}

void Settings::saveAudio() { store(); }

void Settings::saveAll() { store(); }
