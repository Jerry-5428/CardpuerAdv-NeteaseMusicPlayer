#include "AudioEngine.h"

#include <AudioFileSourceFS.h>
#include <M5Unified.h>
#include <SD.h>

#include <algorithm>
#include <vector>

#include "AppConfig.h"
#include "Log.h"
#include "storage/SdCard.h"
#include "storage/SdLock.h"

namespace {
constexpr const char* kTag = "AUDIO";
constexpr const char* kCacheDir = "/netease/cache";
constexpr size_t kDownloadChunk = 4096;
constexpr uint32_t kStallTimeoutMs = 20000;
constexpr size_t kMaxCacheFiles = 4;
}  // namespace

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

uint8_t AudioEngine::downloadPercent() const {
  const uint32_t total = dlTotal_.load();
  if (!total) return 0;
  const uint32_t done = dlBytes_.load();
  if (done >= total) return 100;
  return static_cast<uint8_t>(static_cast<uint64_t>(done) * 100ULL / total);
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
  if (fileSource_) {
    fileSource_->close();
    delete fileSource_;
    fileSource_ = nullptr;
  }
  playing_ = false;
  paused_ = false;
}

void AudioEngine::taskEntry(void* arg) { static_cast<AudioEngine*>(arg)->run(); }

void AudioEngine::run() {
  for (;;) {
    if (stopRequest_.exchange(false)) {
      teardown();
      stage_ = Stage::Idle;
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

      bool ok = false;
      if (sourceMode_ == SourceMode::SdCache && storage::sdMounted()) {
        ok = playFromSd(url, track);
      } else {
        if (sourceMode_ == SourceMode::SdCache) LOGW(kTag, "no SD card, using direct streaming");
        ok = playFromStream(url);
      }

      if (!ok) {
        ended_ = true;
        stage_ = Stage::Failed;
      } else {
        playing_ = true;
        paused_ = false;
        stage_ = Stage::Playing;
      }
    }

    if (playing_) {
      stage_ = paused_ ? Stage::Paused : Stage::Playing;
      if (!paused_) {
        if (!mp3_.loop()) {
          const bool netFailed = source_.failed();
          const String err = source_.lastError();
          teardown();
          if (netFailed && err.length()) setError(err);
          ended_ = true;
          stage_ = Stage::Idle;
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

String AudioEngine::cachePath(int64_t id) {
  return String(kCacheDir) + "/" + String((long long)id) + ".mp3";
}

void AudioEngine::ensureCacheDir() {
  storage::ensureDir("/netease");
  storage::ensureDir(kCacheDir);
}

size_t AudioEngine::fileSize(const String& path) {
  storage::SdGuard guard;
  File f = SD.open(path);
  if (!f) return 0;
  const size_t s = f.size();
  f.close();
  return s;
}

void AudioEngine::pruneCache(const String& keep) {
  storage::SdGuard guard;
  File dir = SD.open(kCacheDir);
  if (!dir) return;

  std::vector<String> files;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) files.push_back(f.path());
    f.close();
  }
  dir.close();
  size_t count = files.size();
  if (count <= kMaxCacheFiles) return;

  for (const String& p : files) {
    if (count <= kMaxCacheFiles) break;
    if (p == keep) continue;
    if (SD.remove(p)) {
      --count;
      LOGI(kTag, "cache pruned: %s", p.c_str());
    }
  }
}

bool AudioEngine::downloadToSd(const String& url, const String& finalPath) {
  ensureCacheDir();
  pruneCache(finalPath);

  const String part = finalPath + ".part";
  File out;
  {
    storage::SdGuard guard;
    SD.remove(part);
    out = SD.open(part, FILE_WRITE);
  }
  if (!out) {
    setError("SD 卡写入失败");
    return false;
  }

  source_.setBufferBytes(ringBytes_);
  source_.setCookie(cookie_);
  if (!source_.open(url.c_str())) {
    setError(source_.lastError().length() ? source_.lastError() : "音频连接失败");
    {
      storage::SdGuard guard;
      out.close();
      SD.remove(part);
    }
    return false;
  }

  dlBytes_ = 0;
  dlTotal_ = source_.totalBytes();

  uint8_t* buf = static_cast<uint8_t*>(malloc(kDownloadChunk));
  if (!buf) {
    setError("内存不足");
    source_.close();
    {
      storage::SdGuard guard;
      out.close();
      SD.remove(part);
    }
    return false;
  }

  bool ok = true;
  String why;
  uint32_t lastData = millis();
  while (!source_.eof()) {
    if (stopRequest_.exchange(false)) {
      why = "已取消";
      ok = false;
      break;
    }
    const uint32_t n = source_.read(buf, kDownloadChunk);
    if (n > 0) {
      size_t written = 0;
      {
        storage::SdGuard guard;
        written = out.write(buf, n);
      }
      if (written != n) {
        why = "SD 卡写入失败";
        ok = false;
        break;
      }
      dlBytes_.fetch_add(n);
      lastData = millis();
    } else if (source_.failed()) {
      why = source_.lastError();
      if (!why.length()) why = "下载中断";
      ok = false;
      break;
    } else if (millis() - lastData > kStallTimeoutMs) {
      why = "下载超时";
      ok = false;
      break;
    }
  }

  const uint32_t total = dlTotal_.load();
  if (ok && total && dlBytes_.load() < total) {
    why = "下载不完整";
    ok = false;
  }

  source_.close();
  {
    storage::SdGuard guard;
    out.close();
  }
  free(buf);

  if (!ok) {
    storage::SdGuard guard;
    SD.remove(part);
    setError(why.length() ? why : "下载失败");
    return false;
  }

  {
    storage::SdGuard guard;
    SD.remove(finalPath);
    if (!SD.rename(part, finalPath)) {
      setError("缓存文件保存失败");
      return false;
    }
  }
  LOGI(kTag, "cached %s (%u bytes)", finalPath.c_str(), (unsigned)dlBytes_.load());
  return true;
}

bool AudioEngine::playFromSd(const String& url, const Track& track) {
  ensureCacheDir();
  const String path = cachePath(track.id);

  bool cached = false;
  {
    storage::SdGuard guard;
    cached = SD.exists(path) && fileSize(path) > 32 * 1024;
  }
  if (!cached) {
    stage_ = Stage::Downloading;
    dlBytes_ = 0;
    dlTotal_ = 0;
    if (!downloadToSd(url, path)) return false;
    stage_ = Stage::Playing;
  } else {
    LOGI(kTag, "cache hit: %s", path.c_str());
  }

  fileSource_ = new AudioFileSourceFS(SD, path.c_str());
  if (!fileSource_->isOpen()) {
    setError("缓存文件打开失败");
    delete fileSource_;
    fileSource_ = nullptr;
    return false;
  }
  if (!mp3_.begin(fileSource_, &output_)) {
    setError("MP3 解码失败");
    fileSource_->close();
    delete fileSource_;
    fileSource_ = nullptr;
    return false;
  }
  LOGI(kTag, "playing cached track %lld", (long long)track.id);
  return true;
}

bool AudioEngine::playFromStream(const String& url) {
  if (!source_.open(url.c_str())) {
    setError(source_.lastError());
    return false;
  }
  if (!mp3_.begin(&source_, &output_)) {
    setError("MP3 解码失败");
    source_.close();
    return false;
  }
  return true;
}
