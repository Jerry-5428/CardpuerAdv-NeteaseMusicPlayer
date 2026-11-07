#include "SdCard.h"

#include <SD.h>
#include <SPI.h>

#include "AppConfig.h"
#include "Log.h"

namespace storage {
namespace {

constexpr const char* kTag = "SD";
SPIClass s_spi(HSPI);
bool s_mounted = false;

}  // namespace

bool mountSd() {
  if (s_mounted && SD.cardType() != CARD_NONE) return true;

  pinMode(cfg::PIN_SD_CS, OUTPUT);
  digitalWrite(cfg::PIN_SD_CS, HIGH);

  s_spi.begin(cfg::PIN_SD_SCK, cfg::PIN_SD_MISO, cfg::PIN_SD_MOSI, cfg::PIN_SD_CS);

  // 20 MHz is conservative and reliable on the Cardputer-Adv wiring.
  if (!SD.begin(cfg::PIN_SD_CS, s_spi, 20000000, cfg::SD_MOUNT, 5, false)) {
    LOGW(kTag, "SD mount failed (is a card inserted?)");
    s_mounted = false;
    return false;
  }
  s_mounted = true;
  LOGI(kTag, "SD mounted, type=%d size=%lluMB", (int)SD.cardType(),
       (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool sdMounted() { return s_mounted; }

bool ensureDir(const String& path) {
  if (!s_mounted) return false;
  if (SD.exists(path.c_str())) return true;
  return SD.mkdir(path.c_str());
}

String sdDescription() {
  if (!s_mounted) return "not mounted";
  return String((unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL))) + " MB card";
}

uint64_t sdFreeMB() {
  if (!s_mounted) return 0;
  return static_cast<uint64_t>(SD.totalBytes() - SD.usedBytes()) / (1024ULL * 1024ULL);
}

}  // namespace storage
