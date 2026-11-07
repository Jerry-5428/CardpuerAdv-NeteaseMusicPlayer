#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

// Small mbedtls wrapper used by the EAPI encoder and the encrypted cookie
// store.  Kept free of project-specific logic so it can be unit-tested on the
// host.
namespace crypto {

using Bytes = std::vector<uint8_t>;

void randomBytes(uint8_t* dst, size_t len);

Bytes md5(const uint8_t* data, size_t len);
String md5Hex(const uint8_t* data, size_t len);
String md5Hex(const String& s);

Bytes sha256(const uint8_t* data, size_t len);
String sha256Hex(const String& s);

Bytes hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t len);

// AES-ECB with PKCS#7 padding, returned as uppercase hex (EAPI requirement).
String aesEcbEncryptHex(const uint8_t* key, size_t keyLen, const uint8_t* plain, size_t len);

// AES-CBC with PKCS#7 padding.  Returns an empty vector on failure.
Bytes aesCbcEncrypt(const uint8_t* key, size_t keyLen, const uint8_t iv[16],
                    const uint8_t* plain, size_t len);
// AES-CBC decrypt, removing PKCS#7 padding.  Returns false on bad padding.
bool aesCbcDecrypt(const uint8_t* key, size_t keyLen, const uint8_t iv[16],
                   const uint8_t* cipher, size_t len, Bytes& out);

Bytes hexToBytes(const String& hex);
String bytesToHex(const uint8_t* data, size_t len, bool uppercase = false);
// Constant-time comparison for tags.
bool constantTimeEquals(const uint8_t* a, const uint8_t* b, size_t len);

}  // namespace crypto
