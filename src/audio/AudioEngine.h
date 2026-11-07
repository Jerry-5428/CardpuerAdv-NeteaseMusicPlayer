#pragma once

#include <AudioGeneratorMP3.h>

#include <atomic>

#include "AudioOutputM5Speaker.h"
#include "HttpAudioSource.h"
#include "model/Types.h"

class AudioFileSourceFS;

// Owns the playback pipeline.
//
//  1. The track is downloaded completely to the SD card (streamed in 4 KB
//     chunks, never held in RAM as a whole).  Download progress is published
//     so the UI can show it.
//  2. The decoder then reads the cached file from SD and feeds libmad.
//  3. PCM goes to M5.Speaker (ES8311 -> NS4150B -> built-in speaker).
//
// If no SD card is mounted the engine falls back to decoding the HTTPS stream
// directly (the previous behaviour), so the player still works.
//
// The whole pipeline lives on core 1 in its own task; play()/stop() only post
// a request, so the UI thread never blocks on network, SD or decoding.
class AudioEngine {
 public:
  enum class Stage : uint8_t { Idle, Downloading, Playing, Paused, Failed };

  bool begin(uint8_t volume, size_t ringBytes);
  bool play(const String& url, const Track& track);
  void stop();
  void pause();
  void resume();
  void togglePause();
  void setVolume(uint8_t volume);
  void setRingBytes(size_t bytes);

  Stage stage() const { return stage_.load(); }
  bool isPlaying() const { return stage_.load() == Stage::Playing; }
  bool isPaused() const { return stage_.load() == Stage::Paused; }
  bool isDownloading() const { return stage_.load() == Stage::Downloading; }

  // Download progress (valid while isDownloading(); 0 when the size is not
  // known up front).
  uint8_t downloadPercent() const;
  uint32_t downloadBytes() const { return dlBytes_.load(); }
  uint32_t downloadTotal() const { return dlTotal_.load(); }

  uint32_t positionMs() const;
  uint32_t durationMs() const { return durationMs_; }
  bool consumeEnded();    // natural end of track (advance the queue)
  String consumeError();  // last pipeline error, cleared on read
  void setCookie(const String& cookie) { cookie_ = cookie; }

  static constexpr uint8_t kMaxVolume = 255;

 private:
  static void taskEntry(void* arg);
  void run();
  void teardown();
  void setError(const String& e);

  bool playFromSd(const String& url, const Track& track);
  bool playFromStream(const String& url);
  bool downloadToSd(const String& url, const String& finalPath);
  static String cachePath(int64_t id);
  static void ensureCacheDir();
  static size_t fileSize(const String& path);
  static void pruneCache(const String& keep);

  AudioOutputM5Speaker output_{&M5.Speaker, 0};
  AudioGeneratorMP3 mp3_;
  HttpAudioSource source_;
  AudioFileSourceFS* fileSource_ = nullptr;

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;

  std::atomic<bool> playRequest_{false};
  std::atomic<bool> stopRequest_{false};
  std::atomic<bool> ended_{false};
  std::atomic<bool> playing_{false};
  std::atomic<bool> paused_{false};
  std::atomic<Stage> stage_{Stage::Idle};
  std::atomic<uint32_t> dlBytes_{0};
  std::atomic<uint32_t> dlTotal_{0};
  std::atomic<uint32_t> durationMs_{0};

  String pendingUrl_;
  Track pendingTrack_;
  String cookie_;
  String error_;
  size_t ringBytes_ = 48 * 1024;
  uint8_t volume_ = 130;
};
