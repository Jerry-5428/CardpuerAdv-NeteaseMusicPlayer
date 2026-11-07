#pragma once

#include <M5Unified.h>
#include <qrcode.h>

#include <vector>

#include "model/Types.h"

// All drawing for the 240x135 ST7789V2 panel.
//
// The UI never performs network, SD or decoding work; it only renders the
// state that App hands over.  Screens are small and cheap, so the App redraws
// only when something changed (lists) or at 2 Hz (progress).
class Ui {
 public:
  static constexpr int kListRows = 6;

  void begin();
  void boot(const String& line1, const String& line2);

  void list(const String& title, const std::vector<String>& items, int sel, int top,
            const String& hint);
  void message(const String& title, const std::vector<String>& lines);
  void input(const String& title, const String& buffer, const String& tabs,
             const String& hint);
  void qr(const String& payload, const String& status, const String& hint);
  void player(const Track& track, bool paused, bool liked, uint32_t posMs, uint32_t durMs,
              int volume, bool downloading, uint8_t dlPercent, const String& modeText);
  void lyrics(const String& title, const String& artist, const std::vector<LyricLine>& lines,
              int cur, bool paused);

 private:
  void row(int i, const String& text, bool selected, int y0);
  void header(const String& title, const String& right);
  void footer(const String& left, const String& right = String());
  String clip(const String& text, int maxWidth) const;
  static String mmss(uint32_t ms);

  QRCode* qr_ = nullptr;
  uint8_t* qrData_ = nullptr;
  String qrPayload_;
  String lastTrackName_;
};
