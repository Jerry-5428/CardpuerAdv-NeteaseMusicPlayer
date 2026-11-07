#include "Log.h"

namespace logx {

void begin(unsigned long baud) {
  Serial.begin(baud);
  delay(50);
}

static void vlog(char level, const char* tag, const char* fmt, va_list ap) {
  char buf[256];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  Serial.printf("[%8lu][%c][%s] %s\n", (unsigned long)millis(), level, tag, buf);
  Serial.flush();
}

void info(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog('I', tag, fmt, ap);
  va_end(ap);
}

void warn(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog('W', tag, fmt, ap);
  va_end(ap);
}

void error(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog('E', tag, fmt, ap);
  va_end(ap);
}

bool looksSensitive(const String& s) {
  return s.indexOf("MUSIC_U=") >= 0 || s.indexOf("__csrf=") >= 0 ||
         s.indexOf("MUSIC_A=") >= 0 || s.indexOf("NMTID=") >= 0 ||
         s.indexOf("MUSIC_R_T=") >= 0;
}

String redact(const String& s) {
  if (looksSensitive(s)) return String("<redacted>");
  return s;
}

}  // namespace logx
