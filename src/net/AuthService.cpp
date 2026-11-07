#include "AuthService.h"

#include "AppConfig.h"
#include "Log.h"

namespace {
constexpr const char* kTag = "AUTH";
}  // namespace

AuthService::AuthService(CookieJar& jar, CookieStore& store) : jar_(jar), store_(store), api_(jar) {}

AuthService::RestoreResult AuthService::restore() {
  CookieJar loaded;
  AccountInfo acct;
  if (!store_.load(loaded, acct)) {
    if (store_.exists()) LOGW(kTag, "saved session unreadable: %s", store_.error().c_str());
    return RestoreResult::NoSession;
  }

  jar_ = loaded;
  account_ = acct;
  LOGI(kTag, "restored session for uid=%lld (cookies=%u)", (long long)acct.uid,
       (unsigned)loaded.size());

  // Validate the session online.  Network failures keep the cookie for later.
  AccountInfo fresh;
  String err;
  if (api_.account(fresh, err)) {
    account_ = fresh;
    persist();
    return RestoreResult::Restored;
  }
  if (err == "未登录") {
    LOGW(kTag, "session expired");
    jar_.clear();
    account_ = AccountInfo();
    store_.erase();
    return RestoreResult::Expired;
  }
  LOGW(kTag, "session validation failed offline: %s", err.c_str());
  return RestoreResult::Offline;
}

bool AuthService::startQrLogin(String& qrContent, String& err) {
  unikey_ = "";
  qrStarted_ = 0;
  if (!api_.qrKey(unikey_, err)) return false;
  qrStarted_ = millis();
  qrContent = String("https://music.163.com/login?codekey=") + unikey_;
  LOGI(kTag, "QR key acquired");
  return true;
}

AuthService::QrState AuthService::pollQrLogin(String& err) {
  if (!unikey_.length()) {
    err = "no qr key";
    return QrState::Failed;
  }
  std::vector<String> setCookies;
  const int code = api_.qrCheck(unikey_, setCookies, err);
  switch (code) {
    case 801:
      return QrState::WaitScan;
    case 802:
      return QrState::Scanned;
    case 803: {
      jar_.mergeSetCookies(setCookies);
      if (!jar_.has("MUSIC_U")) {
        err = "登录响应缺少 MUSIC_U";
        return QrState::Failed;
      }
      if (!jar_.has("os")) jar_.set("os", "pc");
      if (!jar_.has("appver")) jar_.set("appver", "8.9.70");

      AccountInfo fresh;
      String aerr;
      if (api_.account(fresh, aerr)) {
        account_ = fresh;
      } else {
        account_.valid = true;
        account_.uid = 0;
        account_.nickname = "";
      }
      persist();
      unikey_ = "";
      return QrState::Success;
    }
    case 800:
      return QrState::Expired;
    default:
      if (err.isEmpty()) err = String("code ") + code;
      return QrState::Failed;
  }
}

bool AuthService::persist() {
  if (jar_.empty()) return false;
  if (!store_.save(jar_, account_)) {
    LOGW(kTag, "persist failed: %s", store_.error().c_str());
    return false;
  }
  return true;
}

bool AuthService::logout() {
  jar_.clear();
  account_ = AccountInfo();
  unikey_ = "";
  store_.erase();
  LOGI(kTag, "logged out");
  return true;
}
