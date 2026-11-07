#pragma once

#include <Arduino.h>

// Central logger.  Cookies/tokens must never be printed; use redact() when
// there is any doubt about the content.
namespace logx {

void begin(unsigned long baud = 115200);

// Prints "TAG: message".  Never pass cookie material here.
void info(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void warn(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void error(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// True when the value looks like sensitive material (MUSIC_U, __csrf, ...).
bool looksSensitive(const String& s);

// Returns "<hidden>" when sensitive, otherwise the string unchanged.
String redact(const String& s);

}  // namespace logx

#define LOGI(tag, ...) ::logx::info(tag, __VA_ARGS__)
#define LOGW(tag, ...) ::logx::warn(tag, __VA_ARGS__)
#define LOGE(tag, ...) ::logx::error(tag, __VA_ARGS__)
