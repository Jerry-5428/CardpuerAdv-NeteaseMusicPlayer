#include "CookieStore.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <esp_system.h>

#include <vector>

#include "AppConfig.h"
#include "Log.h"
#include "SdCard.h"
#include "SdLock.h"
#include "util/Crypto.h"

namespace {
constexpr const char* kTag = "COOKIE";
constexpr char kMagic[4] = {'N', 'C', 'M', '1'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderLen = 4 + 1 + 16 + 4 + 32;
constexpr size_t kMaxFile = 32 * 1024;
constexpr const char* kTmpFile = "/netease/cookie.tmp";

void putU32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 24) & 0xFF);
}

uint32_t getU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

void CookieStore::deriveKey(uint8_t key[32]) const {
  const uint64_t mac = ESP.getEfuseMac();
  char seed[128];
  snprintf(seed, sizeof(seed), "CardputerAdv-NeteaseCookie-v1|%08lx%08lx|ncm-salt-2026",
           static_cast<unsigned long>(mac & 0xFFFFFFFFULL),
           static_cast<unsigned long>((mac >> 32) & 0xFFFFFFFFULL));
  const auto digest = crypto::sha256(reinterpret_cast<const uint8_t*>(seed), strlen(seed));
  memset(key, 0, 32);
  if (digest.size() == 32) memcpy(key, digest.data(), 32);
}

bool CookieStore::exists() const {
  if (!storage::sdMounted()) return false;
  storage::SdGuard guard;
  return SD.exists(cfg::COOKIE_FILE);
}

bool CookieStore::save(const CookieJar& jar, const AccountInfo& account) {
  error_.clear();
  if (!storage::sdMounted()) {
    error_ = "SD not mounted";
    LOGW(kTag, "cannot persist cookie: SD not mounted");
    return false;
  }
  storage::SdGuard guard;
  if (jar.empty()) {
    error_ = "empty cookie jar";
    return false;
  }
  if (!storage::ensureDir("/netease")) {
    error_ = "mkdir /netease failed";
    return false;
  }

  DynamicJsonDocument doc(8192);
  doc["cookies"] = jar.serialize();
  doc["uid"] = account.uid;
  doc["nickname"] = account.nickname;
  doc["ts"] = static_cast<uint32_t>(time(nullptr));
  String plain;
  serializeJson(doc, plain);

  uint8_t key[32];
  deriveKey(key);
  uint8_t iv[16];
  crypto::randomBytes(iv, sizeof(iv));

  const auto cipher = crypto::aesCbcEncrypt(key, sizeof(key), iv,
                                            reinterpret_cast<const uint8_t*>(plain.c_str()),
                                            plain.length());
  if (cipher.empty() || cipher.size() > 16 * 1024) {
    error_ = "encrypt failed";
    return false;
  }

  std::vector<uint8_t> header;
  header.insert(header.end(), kMagic, kMagic + 4);
  header.push_back(kVersion);
  header.insert(header.end(), iv, iv + sizeof(iv));
  putU32(header, static_cast<uint32_t>(cipher.size()));

  std::vector<uint8_t> macInput(header);
  macInput.insert(macInput.end(), cipher.begin(), cipher.end());
  const auto tag = crypto::hmacSha256(key, sizeof(key), macInput.data(), macInput.size());
  if (tag.size() != 32) {
    error_ = "hmac failed";
    return false;
  }

  File f = SD.open(kTmpFile, FILE_WRITE);
  if (!f) {
    error_ = "open temp failed";
    return false;
  }
  f.write(header.data(), header.size());
  f.write(tag.data(), tag.size());
  f.write(cipher.data(), cipher.size());
  f.close();

  if (SD.exists(cfg::COOKIE_FILE)) SD.remove(cfg::COOKIE_FILE);
  if (!SD.rename(kTmpFile, cfg::COOKIE_FILE)) {
    error_ = "rename failed";
    return false;
  }
  LOGI(kTag, "session persisted (%u bytes, encrypted)", (unsigned)(header.size() + 32 + cipher.size()));
  return true;
}

bool CookieStore::load(CookieJar& jar, AccountInfo& account) {
  error_.clear();
  jar.clear();
  account = AccountInfo();
  if (!storage::sdMounted()) {
    error_ = "SD not mounted";
    return false;
  }
  storage::SdGuard guard;
  File f = SD.open(cfg::COOKIE_FILE, FILE_READ);
  if (!f) {
    error_ = "no saved session";
    return false;
  }
  const size_t size = f.size();
  if (size < kHeaderLen + 16 || size > kMaxFile) {
    f.close();
    error_ = "bad file size";
    return false;
  }
  std::vector<uint8_t> buf(size);
  const size_t got = f.read(buf.data(), size);
  f.close();
  if (got != size) {
    error_ = "short read";
    return false;
  }

  if (memcmp(buf.data(), kMagic, 4) != 0) {
    error_ = "bad magic";
    return false;
  }
  if (buf[4] != kVersion) {
    error_ = "unsupported version";
    return false;
  }
  uint8_t iv[16];
  memcpy(iv, buf.data() + 5, 16);
  const uint32_t len = getU32(buf.data() + 21);
  if (len == 0 || len + kHeaderLen != size) {
    error_ = "corrupt length";
    return false;
  }
  const uint8_t* tag = buf.data() + 25;
  const uint8_t* cipher = buf.data() + kHeaderLen;

  uint8_t key[32];
  deriveKey(key);
  std::vector<uint8_t> macInput(buf.begin(), buf.begin() + 25 + 4);
  macInput.insert(macInput.end(), cipher, cipher + len);
  const auto expected = crypto::hmacSha256(key, sizeof(key), macInput.data(), macInput.size());
  if (expected.size() != 32 || !crypto::constantTimeEquals(expected.data(), tag, 32)) {
    error_ = "integrity check failed";
    LOGW(kTag, "cookie file integrity check failed");
    return false;
  }

  crypto::Bytes plain;
  if (!crypto::aesCbcDecrypt(key, sizeof(key), iv, cipher, len, plain)) {
    error_ = "decrypt failed";
    return false;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err =
      deserializeJson(doc, reinterpret_cast<const char*>(plain.data()), plain.size());
  if (err) {
    error_ = "json parse failed";
    return false;
  }
  const String cookies = doc["cookies"] | "";
  if (!jar.deserialize(cookies)) {
    error_ = "empty cookie";
    return false;
  }
  account.uid = doc["uid"] | 0;
  account.nickname = String(doc["nickname"] | "");
  account.valid = account.uid > 0;
  return true;
}

bool CookieStore::erase() {
  error_.clear();
  if (!storage::sdMounted()) {
    error_ = "SD not mounted";
    return false;
  }
  storage::SdGuard guard;
  if (SD.exists(cfg::COOKIE_FILE)) SD.remove(cfg::COOKIE_FILE);
  if (SD.exists(kTmpFile)) SD.remove(kTmpFile);
  LOGI(kTag, "saved session erased");
  return true;
}
