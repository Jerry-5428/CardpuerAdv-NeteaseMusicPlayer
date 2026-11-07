#pragma once

#include <Arduino.h>

#include <vector>

// In-memory cookie jar for the NetEase session.
//
// The jar is the only place where MUSIC_U lives; it is never printed.
class CookieJar {
 public:
  void clear();
  bool empty() const { return items_.empty(); }
  size_t size() const { return items_.size(); }

  // Merges the "Set-Cookie" header values returned by the login endpoint.
  void mergeSetCookies(const std::vector<String>& setCookies);

  // "a=b; c=d" (for the Cookie request header).  Empty when no cookies.
  String header() const;

  // "a=b\nc=d" for encrypted persistence.
  String serialize() const;
  bool deserialize(const String& text);

  String get(const String& name) const;
  bool has(const String& name) const;
  void set(const String& name, const String& value);
  void remove(const String& name);

  const std::vector<std::pair<String, String>>& items() const { return items_; }

 private:
  std::vector<std::pair<String, String>> items_;
};
