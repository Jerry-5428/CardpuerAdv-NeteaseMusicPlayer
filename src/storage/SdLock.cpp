#include "SdLock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace storage {
namespace {
SemaphoreHandle_t s_lock = nullptr;
}  // namespace

void initSdLock() {
  if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
}

void lockSd() {
  if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

void unlockSd() {
  if (s_lock) xSemaphoreGiveRecursive(s_lock);
}

}  // namespace storage
