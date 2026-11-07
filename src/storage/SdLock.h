#pragma once

// Serializes microSD access between the worker task (cookie persistence) and
// the audio task (cache download).  The SD/SPI driver is not fully re-entrant,
// so every short file operation is wrapped in this mutex.
namespace storage {

void initSdLock();
void lockSd();
void unlockSd();

class SdGuard {
 public:
  SdGuard() { lockSd(); }
  ~SdGuard() { unlockSd(); }
  SdGuard(const SdGuard&) = delete;
  SdGuard& operator=(const SdGuard&) = delete;
};

}  // namespace storage
