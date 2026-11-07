#pragma once

#include <Arduino.h>

#include <vector>

#include "model/Types.h"
#include "net/CookieJar.h"
#include "net/NetEaseApi.h"
#include "storage/CookieStore.h"

// QR login state machine + session persistence.
// All methods perform blocking network I/O and must be called from the worker
// task, never from the UI task.
class AuthService {
 public:
  enum class QrState : uint8_t { Idle, WaitScan, Scanned, Success, Expired, Failed };
  enum class RestoreResult : uint8_t { NoSession, Restored, Expired, Offline, Error };

  AuthService(CookieJar& jar, CookieStore& store);

  RestoreResult restore();
  bool startQrLogin(String& qrContent, String& err);
  QrState pollQrLogin(String& err);
  bool logout();
  bool persist();

  bool loggedIn() const { return account_.valid || jar_.has("MUSIC_U"); }
  const AccountInfo& account() const { return account_; }
  const CookieJar& jar() const { return jar_; }
  NetEaseApi& api() { return api_; }

  uint32_t qrAgeMs() const { return qrStarted_ ? (millis() - qrStarted_) : 0; }

 private:
  CookieJar& jar_;
  CookieStore& store_;
  NetEaseApi api_;
  AccountInfo account_;
  String unikey_;
  uint32_t qrStarted_ = 0;
};
