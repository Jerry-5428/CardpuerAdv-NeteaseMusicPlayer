#pragma once

#include <Arduino.h>

namespace storage {

// Mounts the microSD card on the Cardputer-Adv SPI pins.
// Safe to call more than once.
bool mountSd();
bool sdMounted();

bool ensureDir(const String& path);

// Human readable diagnostics (never contains credentials).
String sdDescription();
uint64_t sdFreeMB();

}  // namespace storage
