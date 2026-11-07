#include "Eapi.h"

#include "../util/Crypto.h"

namespace eapi {

String encode(const String& path, const String& json) {
  const String digest =
      crypto::md5Hex(String("nobody") + path + "use" + json + "md5forencrypt");
  const String text = path + "-36cd479b6b5-" + json + "-36cd479b6b5-" + digest;
  return crypto::aesEcbEncryptHex(reinterpret_cast<const uint8_t*>(kKey), 16,
                                  reinterpret_cast<const uint8_t*>(text.c_str()),
                                  text.length());
}

String body(const String& path, const String& json) {
  return String("params=") + encode(path, json);
}

}  // namespace eapi
