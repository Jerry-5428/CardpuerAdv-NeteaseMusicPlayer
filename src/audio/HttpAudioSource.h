#pragma once

#include <AudioFileSource.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

#include <atomic>

// Streaming HTTP(S) audio source.
//
// A dedicated download task reads the NetEase media URL into a byte ring
// buffer; the MP3 decoder pulls from that buffer in the audio task.  This
// decouples network jitter from decoding and never keeps a whole song in RAM.
//
// Features: redirects, chunked transfer, Range resume after a dropped
// connection, hard timeouts, and clean abort on track switch.
class HttpAudioSource : public AudioFileSource {
 public:
  HttpAudioSource();
  ~HttpAudioSource() override;

  bool open(const char* url) override;
  uint32_t read(void* data, uint32_t len) override;
  uint32_t readNonBlock(void* data, uint32_t len) override;
  bool close() override;
  bool isOpen() override;
  uint32_t getSize() override;
  uint32_t getPos() override;
  bool seek(int32_t pos, int dir) override;

  void setBufferBytes(size_t bytes) { bufferBytes_ = bytes; }
  void setCookie(const String& cookie) { cookie_ = cookie; }
  const String& lastError() const { return error_; }
  bool failed() const { return failed_.load(); }
  bool eof() const { return eof_.load(); }

  // Approximate amount of downloaded-but-not-yet-decoded audio in bytes.
  uint32_t bufferedBytes() const;

 private:
  static void taskEntry(void* arg);
  void downloadLoop();
  bool feed(const uint8_t* data, size_t len);
  bool feedByte(uint8_t b);
  bool readHttpBody(class WiFiClient& client, uint32_t deadlineMs);

  RingbufHandle_t ring_ = nullptr;
  TaskHandle_t task_ = nullptr;
  size_t bufferBytes_ = 48 * 1024;

  std::atomic<bool> abort_{false};
  std::atomic<bool> eof_{false};
  std::atomic<bool> failed_{false};
  std::atomic<bool> running_{false};
  std::atomic<uint32_t> downloaded_{0};
  std::atomic<uint32_t> totalSize_{0};
  std::atomic<uint32_t> buffered_{0};

  volatile uint32_t pos_ = 0;
  volatile uint32_t lastDataMs_ = 0;

  String url_;
  String error_;
  String cookie_;
};
