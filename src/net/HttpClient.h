#pragma once

#include <Arduino.h>

#include <vector>

// Minimal HTTP/1.1 client tuned for this project:
//  * HTTPS always verifies the certificate chain (see TrustRoots.h).
//  * Hard request timeout + hard response size limit.
//  * Chunked transfer decoding, redirects, Set-Cookie collection.
//  * No keep-alive: one connection per request (predictable memory usage).
struct HttpHeader {
  String name;
  String value;
};

struct HttpOptions {
  String method = "GET";
  String contentType;
  String body;
  std::vector<HttpHeader> headers;
  uint32_t timeoutMs = 12000;
  size_t maxBody = 384 * 1024;
  bool followRedirects = true;
  uint8_t maxRedirects = 4;
  bool collectCookies = false;
};

struct HttpResult {
  bool ok = false;
  int status = 0;
  String body;
  String error;
  String location;
  int64_t contentLength = -1;
  std::vector<String> setCookies;
  uint32_t elapsedMs = 0;
};

namespace http {

// Simple absolute-URL parser shared with the audio streaming code.
struct Url {
  String scheme;  // "http" / "https"
  String host;
  uint16_t port = 443;
  String path;  // includes query, starts with '/'

  bool valid() const { return host.length() > 0 && path.length() > 0; }
};

Url parseUrl(const String& url);

HttpResult request(const String& url, const HttpOptions& opt);

// Convenience wrapper for the NetEase EAPI endpoints.
HttpResult postForm(const String& url, const String& body,
                    const std::vector<HttpHeader>& headers, uint32_t timeoutMs,
                    size_t maxBody, bool collectCookies = false);

}  // namespace http
