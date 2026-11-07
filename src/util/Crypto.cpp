#include "Crypto.h"

#include <esp_system.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>

#include <cstring>

namespace crypto {
namespace {

const mbedtls_md_info_t* mdInfo(mbedtls_md_type_t type) {
  return mbedtls_md_info_from_type(type);
}

size_t pkcs7PadLen(size_t len, size_t block) {
  return block - (len % block);
}

}  // namespace

void randomBytes(uint8_t* dst, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    dst[i] = static_cast<uint8_t>(esp_random() & 0xFF);
  }
}

Bytes md5(const uint8_t* data, size_t len) {
  Bytes out(16);
  const mbedtls_md_info_t* info = mdInfo(MBEDTLS_MD_MD5);
  if (!info || mbedtls_md(info, data, len, out.data()) != 0) out.clear();
  return out;
}

String md5Hex(const uint8_t* data, size_t len) {
  Bytes d = md5(data, len);
  return bytesToHex(d.data(), d.size(), false);
}

String md5Hex(const String& s) {
  return md5Hex(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

Bytes sha256(const uint8_t* data, size_t len) {
  Bytes out(32);
  const mbedtls_md_info_t* info = mdInfo(MBEDTLS_MD_SHA256);
  if (!info || mbedtls_md(info, data, len, out.data()) != 0) out.clear();
  return out;
}

String sha256Hex(const String& s) {
  Bytes d = sha256(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
  return bytesToHex(d.data(), d.size(), false);
}

Bytes hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t len) {
  Bytes out(32);
  const mbedtls_md_info_t* info = mdInfo(MBEDTLS_MD_SHA256);
  if (!info || mbedtls_md_hmac(info, key, keyLen, data, len, out.data()) != 0) out.clear();
  return out;
}

String aesEcbEncryptHex(const uint8_t* key, size_t keyLen, const uint8_t* plain, size_t len) {
  if (keyLen != 16 && keyLen != 24 && keyLen != 32) return String();

  const size_t pad = pkcs7PadLen(len, 16);
  Bytes buf(len + pad);
  if (len) memcpy(buf.data(), plain, len);
  memset(buf.data() + len, static_cast<int>(pad), pad);

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (mbedtls_aes_setkey_enc(&ctx, key, static_cast<unsigned int>(keyLen * 8)) != 0) {
    mbedtls_aes_free(&ctx);
    return String();
  }

  Bytes out(buf.size());
  for (size_t off = 0; off < buf.size(); off += 16) {
    if (mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, buf.data() + off,
                              out.data() + off) != 0) {
      mbedtls_aes_free(&ctx);
      return String();
    }
  }
  mbedtls_aes_free(&ctx);
  return bytesToHex(out.data(), out.size(), true);
}

Bytes aesCbcEncrypt(const uint8_t* key, size_t keyLen, const uint8_t iv[16],
                    const uint8_t* plain, size_t len) {
  if (keyLen != 16 && keyLen != 24 && keyLen != 32) return Bytes();

  const size_t pad = pkcs7PadLen(len, 16);
  Bytes buf(len + pad);
  if (len) memcpy(buf.data(), plain, len);
  memset(buf.data() + len, static_cast<int>(pad), pad);

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (mbedtls_aes_setkey_enc(&ctx, key, static_cast<unsigned int>(keyLen * 8)) != 0) {
    mbedtls_aes_free(&ctx);
    return Bytes();
  }
  uint8_t ivCopy[16];
  memcpy(ivCopy, iv, 16);
  Bytes out(buf.size());
  int rc = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, buf.size(), ivCopy,
                                 buf.data(), out.data());
  mbedtls_aes_free(&ctx);
  if (rc != 0) return Bytes();
  return out;
}

bool aesCbcDecrypt(const uint8_t* key, size_t keyLen, const uint8_t iv[16],
                   const uint8_t* cipher, size_t len, Bytes& out) {
  out.clear();
  if (len == 0 || (len % 16) != 0) return false;
  if (keyLen != 16 && keyLen != 24 && keyLen != 32) return false;

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (mbedtls_aes_setkey_dec(&ctx, key, static_cast<unsigned int>(keyLen * 8)) != 0) {
    mbedtls_aes_free(&ctx);
    return false;
  }
  uint8_t ivCopy[16];
  memcpy(ivCopy, iv, 16);
  Bytes buf(len);
  int rc = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, len, ivCopy, cipher, buf.data());
  mbedtls_aes_free(&ctx);
  if (rc != 0) return false;

  const uint8_t pad = buf.back();
  if (pad == 0 || pad > 16 || pad > buf.size()) return false;
  for (size_t i = buf.size() - pad; i < buf.size(); ++i) {
    if (buf[i] != pad) return false;
  }
  buf.resize(buf.size() - pad);
  out.swap(buf);
  return true;
}

Bytes hexToBytes(const String& hex) {
  Bytes out;
  if (hex.length() % 2 != 0) return out;
  out.reserve(hex.length() / 2);
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < hex.length(); i += 2) {
    int hi = nib(hex[i]);
    int lo = nib(hex[i + 1]);
    if (hi < 0 || lo < 0) return Bytes();
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

String bytesToHex(const uint8_t* data, size_t len, bool uppercase) {
  static const char* kLower = "0123456789abcdef";
  static const char* kUpper = "0123456789ABCDEF";
  const char* tbl = uppercase ? kUpper : kLower;
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += tbl[data[i] >> 4];
    out += tbl[data[i] & 0x0F];
  }
  return out;
}

bool constantTimeEquals(const uint8_t* a, const uint8_t* b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

}  // namespace crypto
