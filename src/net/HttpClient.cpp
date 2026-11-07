#include "HttpClient.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <vector>

#include "AppConfig.h"
#include "Log.h"
#include "TrustRoots.h"

namespace http {
namespace {

constexpr const char* kTag = "HTTP";

bool readLine(WiFiClient& client, String& out, uint32_t deadline) {
  out = "";
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    int ch = client.read();
    if (ch < 0) {
      if (!client.connected() && !client.available()) return false;
      delay(1);
      continue;
    }
    if (ch == '\n') {
      if (out.endsWith("\r")) out.remove(out.length() - 1);
      return true;
    }
    if (out.length() > 1024) return false;
    out += static_cast<char>(ch);
  }
  return false;
}

bool readExact(WiFiClient& client, uint8_t* dst, size_t len, uint32_t deadline) {
  size_t got = 0;
  while (got < len) {
    if (static_cast<int32_t>(millis() - deadline) >= 0) return false;
    int n = client.read(dst + got, len - got);
    if (n > 0) {
      got += static_cast<size_t>(n);
      continue;
    }
    if (!client.connected() && !client.available()) return false;
    delay(1);
  }
  return true;
}

String joinHostPort(const Url& u) {
  const bool defaultPort =
      (u.scheme == "https" && u.port == 443) || (u.scheme == "http" && u.port == 80);
  return defaultPort ? u.host : u.host + ":" + String(u.port);
}

String resolveLocation(const Url& base, const String& loc) {
  if (loc.startsWith("http://") || loc.startsWith("https://")) return loc;
  if (loc.startsWith("//")) return base.scheme + ":" + loc;
  if (loc.startsWith("/")) return base.scheme + "://" + joinHostPort(base) + loc;
  int slash = base.path.lastIndexOf('/');
  String dir = slash >= 0 ? base.path.substring(0, slash + 1) : String("/");
  return base.scheme + "://" + joinHostPort(base) + dir + loc;
}

const char* headerValue(const HttpOptions& opt, const char* name, String& storage) {
  for (const auto& h : opt.headers) {
    if (h.name.equalsIgnoreCase(name)) {
      storage = h.value;
      return storage.c_str();
    }
  }
  return nullptr;
}

HttpResult performOnce(const Url& url, const HttpOptions& opt, uint8_t depth) {
  HttpResult res;
  const uint32_t started = millis();

  WiFiClientSecure secure;
  WiFiClient plain;
  WiFiClient* client = nullptr;
  if (url.scheme == "https") {
    secure.setCACert(trust::kRootCAs);
    secure.setTimeout((opt.timeoutMs / 1000) + 1);
    client = &secure;
  } else if (url.scheme == "http") {
    client = &plain;
  } else {
    res.error = "unsupported scheme";
    return res;
  }

  client->setNoDelay(true);
  if (!client->connect(url.host.c_str(), url.port,
                       static_cast<int32_t>(opt.timeoutMs))) {
    res.error = "connect failed";
    res.elapsedMs = millis() - started;
    return res;
  }

  String req;
  req.reserve(512 + opt.body.length());
  req += opt.method;
  req += ' ';
  req += url.path;
  req += " HTTP/1.1\r\nHost: ";
  req += joinHostPort(url);
  req += "\r\n";

  bool hasUA = false;
  bool hasAccept = false;
  bool hasEncoding = false;
  for (const auto& h : opt.headers) {
    if (h.name.equalsIgnoreCase("User-Agent")) hasUA = true;
    if (h.name.equalsIgnoreCase("Accept")) hasAccept = true;
    if (h.name.equalsIgnoreCase("Accept-Encoding")) hasEncoding = true;
    req += h.name;
    req += ": ";
    req += h.value;
    req += "\r\n";
  }
  if (!hasUA) req += "User-Agent: NeteaseMusic/8.9.70 (iPhone; iOS 16.0)\r\n";
  if (!hasAccept) req += "Accept: */*\r\n";
  if (!hasEncoding) req += "Accept-Encoding: identity\r\n";
  req += "Connection: close\r\n";
  if (opt.contentType.length()) {
    req += "Content-Type: ";
    req += opt.contentType;
    req += "\r\n";
  }
  if (opt.body.length() || opt.method == "POST") {
    req += "Content-Length: ";
    req += String(opt.body.length());
    req += "\r\n";
  }
  req += "\r\n";
  req += opt.body;

  if (client->write(reinterpret_cast<const uint8_t*>(req.c_str()), req.length()) !=
      req.length()) {
    res.error = "write failed";
    client->stop();
    res.elapsedMs = millis() - started;
    return res;
  }

  const uint32_t deadline = millis() + opt.timeoutMs;

  String statusLine;
  if (!readLine(*client, statusLine, deadline)) {
    res.error = "no status line";
    client->stop();
    res.elapsedMs = millis() - started;
    return res;
  }
  int sp = statusLine.indexOf(' ');
  res.status = (sp > 0) ? statusLine.substring(sp + 1, sp + 4).toInt() : 0;

  bool chunked = false;
  int64_t contentLength = -1;
  String line;
  while (readLine(*client, line, deadline)) {
    if (line.length() == 0) break;
    int colon = line.indexOf(':');
    if (colon <= 0) continue;
    String name = line.substring(0, colon);
    String value = line.substring(colon + 1);
    value.trim();
    name.toLowerCase();
    if (name == "content-length") {
      contentLength = value.toInt();
    } else if (name == "transfer-encoding") {
      if (value.indexOf("chunked") >= 0) chunked = true;
    } else if (name == "location") {
      res.location = value;
    } else if (name == "set-cookie" && opt.collectCookies) {
      res.setCookies.push_back(value);
    }
  }
  res.contentLength = contentLength;

  // Redirects are handled without reading the body (connection is closed).
  if (opt.followRedirects && res.status >= 300 && res.status < 400 &&
      res.location.length() > 0 && depth < opt.maxRedirects) {
    client->stop();
    Url next = parseUrl(resolveLocation(url, res.location));
    if (!next.valid()) {
      res.error = "bad redirect";
      res.elapsedMs = millis() - started;
      return res;
    }
    HttpOptions nextOpt = opt;
    if (res.status == 303) {
      nextOpt.method = "GET";
      nextOpt.body = "";
    }
    HttpResult r2 = performOnce(next, nextOpt, depth + 1);
    r2.elapsedMs = millis() - started;
    return r2;
  }

  std::vector<uint8_t> raw;
  if (contentLength > 0 && static_cast<size_t>(contentLength) <= opt.maxBody) {
    raw.reserve(static_cast<size_t>(contentLength));
  }

  bool bodyOk = true;
  if (chunked) {
    while (true) {
      if (!readLine(*client, line, deadline)) {
        bodyOk = false;
        break;
      }
      int semi = line.indexOf(';');
      if (semi >= 0) line = line.substring(0, semi);
      line.trim();
      if (line.length() == 0) continue;
      long chunkLen = strtol(line.c_str(), nullptr, 16);
      if (chunkLen <= 0) {
        // Consume trailer headers up to the blank line.
        while (readLine(*client, line, deadline) && line.length() > 0) {
        }
        break;
      }
      if (raw.size() + static_cast<size_t>(chunkLen) > opt.maxBody) {
        res.error = "response too large";
        bodyOk = false;
        break;
      }
      size_t oldSize = raw.size();
      raw.resize(oldSize + static_cast<size_t>(chunkLen));
      if (!readExact(*client, raw.data() + oldSize, static_cast<size_t>(chunkLen),
                     deadline)) {
        bodyOk = false;
        break;
      }
      char crlf[2];
      if (!readExact(*client, reinterpret_cast<uint8_t*>(crlf), 2, deadline)) {
        bodyOk = false;
        break;
      }
    }
  } else if (contentLength >= 0) {
    if (static_cast<size_t>(contentLength) > opt.maxBody) {
      res.error = "response too large";
      bodyOk = false;
    } else {
      raw.resize(static_cast<size_t>(contentLength));
      if (contentLength > 0 &&
          !readExact(*client, raw.data(), static_cast<size_t>(contentLength), deadline)) {
        bodyOk = false;
      }
    }
  } else {
    uint8_t buf[1024];
    while (static_cast<int32_t>(millis() - deadline) < 0) {
      int n = client->read(buf, sizeof(buf));
      if (n > 0) {
        if (raw.size() + static_cast<size_t>(n) > opt.maxBody) {
          res.error = "response too large";
          bodyOk = false;
          break;
        }
        raw.insert(raw.end(), buf, buf + n);
      } else if (!client->connected() && !client->available()) {
        break;
      } else {
        delay(1);
      }
    }
  }

  client->stop();

  if (bodyOk) {
    res.body = String(reinterpret_cast<const char*>(raw.data()), raw.size());
    res.ok = true;
  } else if (res.error.length() == 0) {
    res.error = "body read failed";
  }
  res.elapsedMs = millis() - started;
  return res;
}

}  // namespace

Url parseUrl(const String& url) {
  Url u;
  int schemeEnd = url.indexOf("://");
  if (schemeEnd <= 0) return u;
  u.scheme = url.substring(0, schemeEnd);
  u.scheme.toLowerCase();
  int hostStart = schemeEnd + 3;
  int pathStart = url.indexOf('/', hostStart);
  String authority = pathStart < 0 ? url.substring(hostStart)
                                   : url.substring(hostStart, pathStart);
  u.path = pathStart < 0 ? String("/") : url.substring(pathStart);

  int colon = authority.lastIndexOf(':');
  if (colon > 0) {
    u.host = authority.substring(0, colon);
    u.port = static_cast<uint16_t>(authority.substring(colon + 1).toInt());
  } else {
    u.host = authority;
    u.port = (u.scheme == "http") ? 80 : 443;
  }
  if (u.port == 0) u.port = 443;
  return u;
}

HttpResult request(const String& url, const HttpOptions& opt) {
  Url u = parseUrl(url);
  if (!u.valid()) {
    HttpResult r;
    r.error = "bad url";
    return r;
  }
  return performOnce(u, opt, 0);
}

HttpResult postForm(const String& url, const String& body,
                    const std::vector<HttpHeader>& headers, uint32_t timeoutMs,
                    size_t maxBody, bool collectCookies) {
  HttpOptions opt;
  opt.method = "POST";
  opt.contentType = "application/x-www-form-urlencoded";
  opt.body = body;
  opt.headers = headers;
  opt.timeoutMs = timeoutMs;
  opt.maxBody = maxBody;
  opt.collectCookies = collectCookies;
  return request(url, opt);
}

}  // namespace http
