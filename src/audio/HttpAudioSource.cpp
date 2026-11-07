#include "HttpAudioSource.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <algorithm>

#include "AppConfig.h"
#include "net/HttpClient.h"
#include "Log.h"
#include "TrustRoots.h"

namespace {
constexpr const char* kTag = "AUDIO";
constexpr uint32_t kIdleTimeoutMs = 15000;

bool readLine(WiFiClient& c, String& out, uint32_t deadline) {
  out = "";
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    int ch = c.read();
    if (ch < 0) {
      if (!c.connected() && !c.available()) return false;
      delay(1);
      continue;
    }
    if (ch == '\n') {
      if (out.endsWith("\r")) out.remove(out.length() - 1);
      return true;
    }
    if (out.length() > 800) return false;
    out += static_cast<char>(ch);
  }
  return false;
}

String resolveUrl(const http::Url& base, const String& loc) {
  if (loc.startsWith("http://") || loc.startsWith("https://")) return loc;
  if (loc.startsWith("//")) return base.scheme + ":" + loc;
  if (loc.startsWith("/")) return base.scheme + "://" + base.host + loc;
  const int slash = base.path.lastIndexOf('/');
  return base.scheme + "://" + base.host +
         (slash >= 0 ? base.path.substring(0, slash + 1) : String("/")) + loc;
}

}  // namespace

HttpAudioSource::HttpAudioSource() {}

HttpAudioSource::~HttpAudioSource() { close(); }

bool HttpAudioSource::open(const char* url) {
  close();
  url_ = url ? url : "";
  error_ = "";
  abort_ = false;
  eof_ = false;
  failed_ = false;
  downloaded_ = 0;
  totalSize_ = 0;
  buffered_ = 0;
  pos_ = 0;
  lastDataMs_ = millis();

  if (!url_.startsWith("http://") && !url_.startsWith("https://")) {
    error_ = "bad audio url";
    failed_ = true;
    return false;
  }

  ring_ = xRingbufferCreate(bufferBytes_, RINGBUF_TYPE_BYTEBUF);
  if (!ring_) {
    error_ = "ring buffer alloc failed";
    failed_ = true;
    return false;
  }

  running_ = true;
  if (xTaskCreatePinnedToCore(taskEntry, "audio_http", 10240, this, 4, &task_, 0) != pdPASS) {
    running_ = false;
    vRingbufferDelete(ring_);
    ring_ = nullptr;
    error_ = "download task failed";
    failed_ = true;
    return false;
  }

  // Wait for the first audio bytes (or a hard failure) so the caller can
  // report a useful error instead of a silent 0-byte stream.
  const uint32_t start = millis();
  while (millis() - start < 12000) {
    if (downloaded_.load() >= 2048 || eof_.load() || failed_.load()) break;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (failed_.load()) return false;
  if (downloaded_.load() == 0 && !eof_.load()) {
    error_ = "audio stream timeout";
    close();
    return false;
  }
  return true;
}

uint32_t HttpAudioSource::read(void* data, uint32_t len) {
  if (!ring_ || !data || len == 0) return 0;
  uint32_t got = 0;
  const uint32_t started = millis();
  while (got < len) {
    size_t n = 0;
    void* p = xRingbufferReceiveUpTo(ring_, &n, pdMS_TO_TICKS(100), len - got);
    if (p && n > 0) {
      memcpy(static_cast<uint8_t*>(data) + got, p, n);
      vRingbufferReturnItem(ring_, p);
      got += n;
      pos_ += n;
      const uint32_t b = buffered_.load();
      buffered_.store(b >= n ? b - n : 0);
      continue;
    }
    if (eof_.load() || failed_.load() || abort_.load()) break;
    if (millis() - started > cfg::AUDIO_READ_TIMEOUT_MS) break;
    if (!running_.load()) break;
  }
  return got;
}

uint32_t HttpAudioSource::readNonBlock(void* data, uint32_t len) {
  if (!ring_ || !data || len == 0) return 0;
  size_t n = 0;
  void* p = xRingbufferReceiveUpTo(ring_, &n, 0, len);
  if (!p || n == 0) return 0;
  memcpy(data, p, n);
  vRingbufferReturnItem(ring_, p);
  pos_ += n;
  return static_cast<uint32_t>(n);
}

bool HttpAudioSource::close() {
  abort_ = true;
  if (task_) {
    for (int i = 0; i < 300 && running_.load(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
  }
  if (ring_) {
    vRingbufferDelete(ring_);
    ring_ = nullptr;
  }
  running_ = false;
  buffered_ = 0;
  return true;
}

bool HttpAudioSource::isOpen() { return ring_ != nullptr && !failed_.load(); }

uint32_t HttpAudioSource::getSize() { return totalSize_.load(); }

uint32_t HttpAudioSource::getPos() { return pos_; }

bool HttpAudioSource::seek(int32_t pos, int dir) {
  (void)pos;
  (void)dir;
  return false;
}

uint32_t HttpAudioSource::bufferedBytes() const { return buffered_.load(); }

bool HttpAudioSource::feed(const uint8_t* data, size_t len) {
  if (!ring_) return false;
  size_t sent = 0;
  while (sent < len) {
    if (abort_.load()) return false;
    if (xRingbufferSend(ring_, data + sent, len - sent, pdMS_TO_TICKS(500)) == pdTRUE) {
      sent = len;
    } else {
      // Ring full: normal backpressure while the decoder is paused or slower
      // than the network.  Only abort matters here.
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  downloaded_.fetch_add(static_cast<uint32_t>(len));
  buffered_.fetch_add(static_cast<uint32_t>(len));
  lastDataMs_ = millis();
  return true;
}

bool HttpAudioSource::feedByte(uint8_t b) { return feed(&b, 1); }

void HttpAudioSource::taskEntry(void* arg) {
  static_cast<HttpAudioSource*>(arg)->downloadLoop();
}

void HttpAudioSource::downloadLoop() {
  int attempt = 0;
  while (!abort_.load() && attempt < 3 && !eof_.load()) {
    if (attempt > 0) {
      LOGW(kTag, "stream retry %d at %u bytes", attempt, (unsigned)downloaded_.load());
      vTaskDelay(pdMS_TO_TICKS(600));
    }
    String url = url_;
    bool complete = true;
    int redirects = 0;

    while (redirects < 5 && !abort_.load()) {
      const http::Url u = http::parseUrl(url);
      if (!u.valid()) {
        error_ = "bad url";
        complete = false;
        break;
      }
      WiFiClientSecure secure;
      WiFiClient plain;
      WiFiClient* client = nullptr;
      if (u.scheme == "https") {
        secure.setCACert(trust::kRootCAs);
        secure.setTimeout(10);
        client = &secure;
      } else {
        client = &plain;
      }
      if (!client->connect(u.host.c_str(), u.port, 10000)) {
        error_ = "connect failed";
        complete = false;
        break;
      }

      String req;
      req.reserve(512);
      req += "GET ";
      req += u.path;
      req += " HTTP/1.1\r\nHost: ";
      req += u.host;
      req += "\r\nUser-Agent: NeteaseMusic/8.9.70 (iPhone; iOS 16.0)\r\n";
      req += "Accept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n";
      if (cookie_.length()) {
        req += "Cookie: ";
        req += cookie_;
        req += "\r\n";
      }
      const uint32_t have = downloaded_.load();
      if (have > 0) {
        req += "Range: bytes=";
        req += String(have);
        req += "-\r\n";
      }
      req += "\r\n";
      client->print(req);

      const uint32_t deadline = millis() + 15000;
      String line;
      if (!readLine(*client, line, deadline)) {
        error_ = "no response";
        complete = false;
        client->stop();
        break;
      }
      int sp = line.indexOf(' ');
      int status = sp > 0 ? line.substring(sp + 1, sp + 4).toInt() : 0;

      bool chunked = false;
      int64_t contentLength = -1;
      String location;
      while (readLine(*client, line, deadline)) {
        if (line.length() == 0) break;
        int colon = line.indexOf(':');
        if (colon <= 0) continue;
        String n = line.substring(0, colon);
        String v = line.substring(colon + 1);
        v.trim();
        n.toLowerCase();
        if (n == "content-length") contentLength = v.toInt();
        else if (n == "transfer-encoding" && v.indexOf("chunked") >= 0) chunked = true;
        else if (n == "location") location = v;
      }

      if (status >= 300 && status < 400 && location.length()) {
        url = resolveUrl(u, location);
        client->stop();
        ++redirects;
        continue;
      }
      if (status != 200 && status != 206) {
        error_ = String("HTTP ") + status;
        complete = false;
        client->stop();
        break;
      }
      if (status == 200 && have > 0) {
        // Server ignored Range; refusing would duplicate audio.  Treat as error.
        error_ = "range not supported";
        complete = false;
        client->stop();
        break;
      }
      if (contentLength >= 0) {
        totalSize_.store(have + static_cast<uint32_t>(contentLength));
      }

      // Stream body.
      uint8_t buf[cfg::AUDIO_HTTP_CHUNK];
      lastDataMs_ = millis();
      if (chunked) {
        bool done = false;
        while (!done && !abort_.load()) {
          if (!readLine(*client, line, deadline)) {
            complete = false;
            break;
          }
          int semi = line.indexOf(';');
          if (semi >= 0) line = line.substring(0, semi);
          line.trim();
          if (!line.length()) continue;
          long chunk = strtol(line.c_str(), nullptr, 16);
          if (chunk <= 0) {
            done = true;
            break;
          }
          long remaining = chunk;
          while (remaining > 0 && !abort_.load()) {
            const size_t want = std::min<size_t>(sizeof(buf), remaining);
            const int n = client->read(buf, want);
            if (n > 0) {
              if (!feed(buf, n)) {
                complete = false;
                break;
              }
              remaining -= n;
            } else if (!client->connected() && !client->available()) {
              complete = false;
              break;
            } else {
              if (millis() - lastDataMs_ > kIdleTimeoutMs) {
                complete = false;
                break;
              }
              delay(1);
            }
          }
          char crlf[2];
          if (remaining == 0) client->readBytes(crlf, 2);
        }
      } else if (contentLength >= 0) {
        int64_t remaining = contentLength;
        while (remaining > 0 && !abort_.load()) {
          const size_t want = std::min<int64_t>(sizeof(buf), remaining);
          const int n = client->read(buf, want);
          if (n > 0) {
            if (!feed(buf, n)) {
              complete = false;
              break;
            }
            remaining -= n;
          } else if (!client->connected() && !client->available()) {
            complete = false;
            break;
          } else {
            if (millis() - lastDataMs_ > kIdleTimeoutMs) {
              complete = false;
              break;
            }
            delay(1);
          }
        }
      } else {
        while (!abort_.load()) {
          const int n = client->read(buf, sizeof(buf));
          if (n > 0) {
            if (!feed(buf, n)) {
              complete = false;
              break;
            }
          } else if (!client->connected() && !client->available()) {
            break;
          } else {
            if (millis() - lastDataMs_ > kIdleTimeoutMs) {
              complete = false;
              break;
            }
            delay(1);
          }
        }
      }
      client->stop();
      if (complete && !abort_.load()) {
        eof_ = true;
        LOGI(kTag, "stream complete, %u bytes", (unsigned)downloaded_.load());
        running_ = false;
        vTaskDelete(nullptr);
        return;
      }
      break;
    }
    if (complete && !abort_.load() && !eof_.load()) {
      // Header phase finished without body (should not happen) - retry once.
    }
    ++attempt;
  }

  if (!abort_.load() && !eof_.load()) {
    failed_ = true;
    if (error_.isEmpty()) error_ = "stream stopped";
    LOGW(kTag, "stream failed: %s", error_.c_str());
  }
  running_ = false;
  vTaskDelete(nullptr);
}
