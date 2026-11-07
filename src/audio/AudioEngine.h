#pragma once

#include <AudioGeneratorMP3.h>

#include <atomic>

#include "AudioOutputM5Speaker.h"
#include "HttpAudioSource.h"
#include "model/Types.h"

// Owns the decode pipeline:
//   HTTPS stream -> HttpAudioSource ring buffer -> libmad MP3 -> M5 Speaker
// The decoder lives on core 1 in its own task; play()/stop() only post a
// request so the UI thread never blocks on network or decoding.
class AudioEngine {
 public:
  bool begin(uint8_t volume, size_t ringBytes);
  bool play(const String& url, const Track& track);
  void stop();
  void pause();
  void resume();
  void togglePause();
  void setVolume(uint8_t volume);
  void setRingBytes(size_t bytes);

  bool isPlaying() const { return playing_; }
  bool isPaused() const { return paused_; }
  uint32_t positionMs() const;
  uint32_t durationMs() const { return durationMs_; }
  uint32_t bufferedBytes() const { return source_.bufferedBytes(); }
  bool consumeEnded();    // natural end of track (advance the queue)
  String consumeError();  // last pipeline error, cleared on read
  void setCookie(const String& cookie) { cookie_ = cookie; }

  static constexpr uint8_t kMaxVolume = 255;

 private:
  static void taskEntry(void* arg);
  void run();
  void teardown();
  void setError(const String& e);

  AudioOutputM5Speaker output_{&M5.Speaker, 0};
  AudioGeneratorMP3 mp3_;
  HttpAudioSource source_;

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;

  std::atomic<bool> playRequest_{false};
  std::atomic<bool> stopRequest_{false};
  std::atomic<bool> ended_{false};
  std::atomic<bool> playing_{false};
  std::atomic<bool> paused_{false};
  std::atomic<uint32_t> durationMs_{0};

  String pendingUrl_;
  Track pendingTrack_;
  String cookie_;
  String error_;
  size_t ringBytes_ = 48 * 1024;
  uint8_t volume_ = 130;
};
