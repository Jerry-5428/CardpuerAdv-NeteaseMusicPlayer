#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Compile-time configuration for Cardputer-Adv NetEase Music Player.
//
// Hardware facts verified against:
//   https://docs.m5stack.com/en/core/Cardputer-Adv
//   https://docs.m5stack.com/en/arduino/m5cardputer/program
//
//  - SoC      : ESP32-S3FN8, 8 MB flash, NO PSRAM
//  - Display  : ST7789V2 1.14" 240x135
//  - Keyboard : 56 keys behind TCA8418 I2C expander (INT = GPIO11)
//  - Codec    : ES8311 (I2C 8/9, I2S 41/43/46/42), amp NS4150B
//  - microSD  : CS 12, MOSI 14, SCK 40, MISO 39
// ---------------------------------------------------------------------------

namespace cfg {

// ---- Pins (must not be changed without re-checking the schematic) ----
constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;
constexpr int PIN_KEYBOARD_INT = 11;

constexpr int PIN_SD_CS = 12;
constexpr int PIN_SD_MOSI = 14;
constexpr int PIN_SD_SCK = 40;
constexpr int PIN_SD_MISO = 39;

constexpr int PIN_I2S_BCLK = 41;
constexpr int PIN_I2S_LRCK = 43;
constexpr int PIN_I2S_DOUT = 46;
constexpr int PIN_I2S_DIN = 42;

// ---- Display ----
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;

// ---- Network ----
constexpr const char* API_HOST = "music.163.com";
// EAPI endpoints answer on interface3 (same wildcard certificate).
constexpr const char* EAPI_HOST = "interface3.music.163.com";
constexpr uint16_t API_PORT = 443;
constexpr uint32_t HTTP_TIMEOUT_MS = 12000;
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 8000;
// Hard cap for a single API response (protects the heap).
constexpr size_t HTTP_MAX_RESPONSE = 384 * 1024;

// ---- Storage ----
constexpr const char* SD_MOUNT = "/sd";
// NOTE: cookie file is encrypted (AES-256-CBC + HMAC-SHA256).
constexpr const char* COOKIE_FILE = "/netease/cookie.enc";
constexpr const char* CACHE_DIR = "/netease/cache";

// ---- Audio ----
// 48 KB ring buffer: the device has no PSRAM, so this value is deliberately
// conservative.  The settings screen allows 32/48/64/96 KB.
constexpr size_t AUDIO_RING_DEFAULT = 48 * 1024;
constexpr size_t AUDIO_RING_MIN = 32 * 1024;
constexpr size_t AUDIO_RING_MAX = 96 * 1024;
constexpr uint32_t AUDIO_READ_TIMEOUT_MS = 3000;
constexpr size_t AUDIO_HTTP_CHUNK = 2048;
constexpr int AUDIO_TASK_CORE = 1;
constexpr int AUDIO_TASK_STACK = 6 * 1024;
constexpr int AUDIO_TASK_PRIO = 5;

// ---- Worker ----
constexpr int WORKER_TASK_CORE = 0;
constexpr int WORKER_TASK_STACK = 10 * 1024;
constexpr int WORKER_TASK_PRIO = 2;
constexpr size_t WORKER_QUEUE_DEPTH = 8;

// ---- Session ----
constexpr uint32_t QR_POLL_INTERVAL_MS = 2500;
constexpr uint32_t QR_TTL_MS = 180 * 1000;
constexpr size_t MAX_QUEUE_TRACKS = 64;
constexpr size_t MAX_SEARCH_RESULTS = 30;

}  // namespace cfg
