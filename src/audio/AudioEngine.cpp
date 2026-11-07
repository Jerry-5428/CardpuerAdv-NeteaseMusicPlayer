#include "AudioEngine.h"

#include <M5Unified.h>

#include "AppConfig.h"
#include "Log.h"

namespace {
constexpr const char* kTag = "AUDIO";
}

bool AudioEngine::begin(uint8_t volume, size_t ringBytes) {
  ringBytes_ = ringBytes;
  volume_ = volume;
  mutex_ = xSemaphoreCreateMutex();

  auto spk_cfg = M5.Speaker.config();
  spk_cfg.sample_rate = 48000;
  M5.Speaker.config(spk_cfg);
  M5.Speaker.begin();
  M5.Speaker.setVolume(volume_);
  output_.setup();

  if (xTaskCreatePinnedToCore(taskEntry, "audio_dec", cfg::AUDIO_TASK_STACK, this,
                              cfg::AUDIO_TASK_PRIO, &task_, cfg::AUDIO_TASK_CORE) != pdPASS) {
    LOGE(kTag, "cannot start decode task");
    return false;
  }
  LOGI(kTag, "engine ready, ring=%u KB", (unsigned)(ringBytes_ / 1024));
  return true;
}

void AudioEngine::setVolume(uint8_t volume) {
  volume_ = volume;
  M5.Speaker.setVolume(volume);
}

void AudioEngine::setRingBytes(size_t bytes) { ringBytes_ = bytes; }

bool AudioEngine::play(const String& url, const Track& track) {
  if (url.isEmpty()) return false;
  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
  pendingUrl_ = url;
  pendingTrack_ = track;
  if (mutex_) xSemaphoreGive(mutex_);
  ended_ = false;
  playRequest_ = true;
  return true;
}

void AudioEngine::stop() { stopRequest_ = true; }

void AudioEngine::pause() {
  if (playing_) paused_ = true;
}

void AudioEngine::resume() { paused_ = false; }

void AudioEngine::togglePause() {
  if (!playing_) return;
  paused_ = !paused_;
}

uint32_t AudioEngine::positionMs() const {
  const uint32_t rate = output_.getRate();
  if (rate == 0) return 0;
  const uint64_t ms = static_cast<uint64_t>(output_.getFrames()) * 1000ULL / rate;
  const uint32_t dur = durationMs_;
  return (dur && ms > dur) ? dur : static_cast<uint32_t>(ms);
}

bool AudioEngine::consumeEnded() { return ended_.exchange(false); }

String AudioEngine::consumeError() {
  if (!mutex_) return String();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  String e = error_;
  error_ = "";
  xSemaphoreGive(mutex_);
  return e;
}

void AudioEngine::setError(const String& e) {
  if (!mutex_) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  error_ = e;
  xSemaphoreGive(mutex_);
  LOGW(kTag, "pipeline error: %s", e.c_str());
}

void AudioEngine::teardown() {
  if (mp3_.isRunning()) mp3_.stop();
  output_.stop();
  source_.close();
  playing_ = false;
  paused_ = false;
}

void AudioEngine::taskEntry(void* arg) { static_cast<AudioEngine*>(arg)->run(); }

void AudioEngine::run() {
  for (;;) {
    if (stopRequest_.exchange(false)) {
      teardown();
      ended_ = false;
    }

    if (playRequest_.exchange(false)) {
      teardown();
      String url;
      Track track;
      if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
      url = pendingUrl_;
      track = pendingTrack_;
      if (mutex_) xSemaphoreGive(mutex_);

      output_.resetFrames();
      durationMs_ = track.durationMs;
      ended_ = false;
      source_.setBufferBytes(ringBytes_);
      source_.setCookie(cookie_);

      LOGI(kTag, "opening stream for %lld (%ukbps target)", (long long)track.id,
           (unsigned)(durationMs_ / 1000));
      if (!source_.open(url.c_str())) {
        setError(source_.lastError());
        ended_ = true;
      } else if (!mp3_.begin(&source_, &output_)) {
        setError("MP3 decoder init failed");
        source_.close();
        ended_ = true;
      } else {
        playing_ = true;
        paused_ = false;
      }
    }

    if (playing_ && !paused_) {
      if (!mp3_.loop()) {
        const String err = source_.lastError();
        teardown();
        if (source_.failed() && err.length()) setError(err);
        ended_ = true;
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}
