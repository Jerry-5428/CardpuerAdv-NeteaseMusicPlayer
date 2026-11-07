#pragma once

#include <Arduino.h>

#include "model/Types.h"
#include "net/CookieJar.h"

// Encrypted persistence for the NetEase session.
//
// File layout (all integers little endian):
//   magic[4] = "NCM1"
//   version  = 1
//   iv[16]   (random per save)
//   len      (uint32)
//   tag[32]  HMAC-SHA256 over magic|version|iv|len|ciphertext
//   ciphertext[len]  AES-256-CBC(PKCS#7)
//
// The AES key is derived from the ESP32-S3 eFuse MAC + a compile-time salt.
// This protects the cookie from casual SD-card inspection and prevents
// accidental uploads of a plaintext credential.  It is NOT a hardware secure
// element: an attacker with physical access to the device and the SD card can
// recover the key.  Cookie material is never logged.
class CookieStore {
 public:
  bool save(const CookieJar& jar, const AccountInfo& account);
  bool load(CookieJar& jar, AccountInfo& account);
  bool erase();
  bool exists() const;

  const String& error() const { return error_; }

 private:
  void deriveKey(uint8_t key[32]) const;

  String error_;
};
