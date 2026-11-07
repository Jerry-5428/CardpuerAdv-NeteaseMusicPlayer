#include "CookieJar.h"

namespace {
constexpr size_t kMaxCookies = 24;
constexpr size_t kMaxValueLen = 4096;
}  // namespace

void CookieJar::clear() { items_.clear(); }

void CookieJar::set(const String& name, const String& value) {
  if (name.length() == 0 || name.length() > 64) return;
  if (value.length() > kMaxValueLen) return;
  for (auto& kv : items_) {
    if (kv.first == name) {
      kv.second = value;
      return;
    }
  }
  if (items_.size() >= kMaxCookies) return;
  items_.push_back({name, value});
}

void CookieJar::remove(const String& name) {
  for (auto it = items_.begin(); it != items_.end(); ++it) {
    if (it->first == name) {
      items_.erase(it);
      return;
    }
  }
}

String CookieJar::get(const String& name) const {
  for (const auto& kv : items_) {
    if (kv.first == name) return kv.second;
  }
  return String();
}

bool CookieJar::has(const String& name) const {
  for (const auto& kv : items_) {
    if (kv.first == name) return true;
  }
  return false;
}

void CookieJar::mergeSetCookies(const std::vector<String>& setCookies) {
  for (const String& raw : setCookies) {
    int semi = raw.indexOf(';');
    String pair = (semi >= 0) ? raw.substring(0, semi) : raw;
    int eq = pair.indexOf('=');
    if (eq <= 0) continue;
    String name = pair.substring(0, eq);
    String value = pair.substring(eq + 1);
    name.trim();
    value.trim();
    // Ignore cookie deletion markers.
    if (value.length() == 0 || value.equalsIgnoreCase("deleted")) {
      remove(name);
      continue;
    }
    set(name, value);
  }
}

String CookieJar::header() const {
  String out;
  for (const auto& kv : items_) {
    if (out.length()) out += "; ";
    out += kv.first;
    out += '=';
    out += kv.second;
  }
  return out;
}

String CookieJar::serialize() const {
  String out;
  for (const auto& kv : items_) {
    out += kv.first;
    out += '=';
    out += kv.second;
    out += '\n';
  }
  return out;
}

bool CookieJar::deserialize(const String& text) {
  items_.clear();
  int start = 0;
  while (start < static_cast<int>(text.length())) {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    start = (nl < 0) ? text.length() : nl + 1;
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    set(line.substring(0, eq), line.substring(eq + 1));
  }
  return !items_.empty();
}
