#pragma once

#include <Arduino.h>

// NetEase EAPI request encoder.
//
// eapi is the encrypted API flavour used by the mobile/desktop clients:
//   digest = MD5("nobody" + path + "use" + json + "md5forencrypt")
//   text   = path + "-36cd479b6b5-" + json + "-36cd479b6b5-" + digest
//   params = AES-128-ECB(text, key="e82ckenh8dichen8") as UPPERCASE hex
//
// The resulting form body is: params=<HEX>
// The HTTP path is /eapi + <path>.
namespace eapi {

// Returns the "params" value (uppercase hex) for the given JSON payload.
String encode(const String& path, const String& json);

// Convenience: builds "params=<hex>".
String body(const String& path, const String& json);

constexpr const char* kKey = "e82ckenh8dichen8";

}  // namespace eapi
