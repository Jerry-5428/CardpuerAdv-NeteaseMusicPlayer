#include "Ui.h"

#include <algorithm>
#include <cstdlib>

#include "AppConfig.h"

namespace {

constexpr int kHeaderH = 18;
constexpr int kRowH = 16;
constexpr int kBodyY = kHeaderH + 1;
constexpr int kFootY = 121;
constexpr int kWidth = cfg::SCREEN_W;

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kHeaderBg = 0x10A2;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kDim = 0x8410;
constexpr uint16_t kAccent = 0x07FF;
constexpr uint16_t kSelBg = 0x02A6;
constexpr uint16_t kDanger = 0xF800;
constexpr uint16_t kOk = 0x07E0;

}  // namespace

void Ui::begin() {
  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
  M5.Display.fillScreen(kBg);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextWrap(false);

  if (!qrData_) {
    const uint16_t need = qrcode_getBufferSize(5);
    qrData_ = static_cast<uint8_t*>(malloc(need));
    qr_ = static_cast<QRCode*>(malloc(sizeof(QRCode)));
  }
}

String Ui::clip(const String& text, int maxWidth) const {
  if (M5.Display.textWidth(text) <= maxWidth) return text;
  String out;
  size_t i = 0;
  while (i < text.length()) {
    const uint8_t c = static_cast<uint8_t>(text[i]);
    size_t len = 1;
    if (c >= 0xF0) {
      len = 4;
    } else if (c >= 0xE0) {
      len = 3;
    } else if (c >= 0xC0) {
      len = 2;
    }
    if (i + len > text.length()) break;
    String next = out + text.substring(i, i + len);
    if (M5.Display.textWidth(next) > maxWidth - 6) break;
    out = next;
    i += len;
  }
  out += "..";
  return out;
}

String Ui::mmss(uint32_t ms) {
  const uint32_t total = ms / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(total / 60), (unsigned)(total % 60));
  return String(buf);
}

void Ui::header(const String& title, const String& right) {
  M5.Display.fillRect(0, 0, kWidth, kHeaderH, kHeaderBg);
  M5.Display.setTextColor(kText, kHeaderBg);
  M5.Display.drawString(clip(title, right.length() ? 150 : 232), 4, 3);
  if (right.length()) {
    M5.Display.setTextColor(kAccent, kHeaderBg);
    const String r = clip(right, 84);
    M5.Display.drawString(r, kWidth - M5.Display.textWidth(r) - 4, 3);
  }
}

void Ui::footer(const String& left, const String& right) {
  M5.Display.fillRect(0, kFootY, kWidth, cfg::SCREEN_H - kFootY, kBg);
  M5.Display.setTextColor(kDim, kBg);
  if (left.length()) M5.Display.drawString(clip(left, right.length() ? 140 : 232), 4, kFootY + 2);
  if (right.length()) {
    const String r = clip(right, 86);
    M5.Display.drawString(r, kWidth - M5.Display.textWidth(r) - 4, kFootY + 2);
  }
}

void Ui::row(int i, const String& text, bool selected, int y0) {
  const int y = y0 + i * kRowH;
  if (selected) {
    M5.Display.fillRect(0, y, kWidth, kRowH, kSelBg);
  } else {
    M5.Display.fillRect(0, y, kWidth, kRowH, kBg);
  }
  M5.Display.setTextColor(selected ? kText : kDim, selected ? kSelBg : kBg);
  M5.Display.drawString(clip(text, kWidth - 12), 6, y + 2);
}

void Ui::boot(const String& line1, const String& line2) {
  M5.Display.fillScreen(kBg);
  M5.Display.setTextColor(kAccent, kBg);
  M5.Display.drawString("网易云音乐", 8, 24);
  M5.Display.setTextColor(kText, kBg);
  M5.Display.drawString(clip(line1, kWidth - 16), 8, 52);
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.drawString(clip(line2, kWidth - 16), 8, 72);
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.drawString("Cardputer-Adv", 8, 112);
}

void Ui::list(const String& title, const std::vector<String>& items, int sel, int top,
              const String& hint) {
  M5.Display.fillScreen(kBg);
  header(title, String(items.size()) + " 项");
  const int n = static_cast<int>(items.size());
  for (int i = 0; i < kListRows; ++i) {
    const int idx = top + i;
    if (idx >= n) {
      M5.Display.fillRect(0, kBodyY + i * kRowH, kWidth, kRowH, kBg);
      continue;
    }
    row(i, items[idx], idx == sel, kBodyY);
  }
  if (n > kListRows) {
    const int trackH = kListRows * kRowH;
    M5.Display.fillRect(kWidth - 3, kBodyY, 3, trackH, 0x2104);
    const int barH = std::max(12, trackH * kListRows / n);
    const int barY = kBodyY + (trackH - barH) * (top + sel) / std::max(1, n - 1);
    M5.Display.fillRect(kWidth - 3, barY, 3, barH, kAccent);
  }
  footer(hint);
}

void Ui::message(const String& title, const std::vector<String>& lines) {
  M5.Display.fillScreen(kBg);
  header(title, "");
  int y = kBodyY + 4;
  for (const String& l : lines) {
    M5.Display.setTextColor(y == kBodyY + 4 ? kText : kDim, kBg);
    M5.Display.drawString(clip(l, kWidth - 12), 6, y);
    y += 17;
    if (y > kFootY - 12) break;
  }
  footer("Enter / Tab 返回");
}

void Ui::input(const String& title, const String& buffer, const String& tabs,
               const String& hint) {
  M5.Display.fillScreen(kBg);
  header(title, tabs);
  M5.Display.drawRect(4, 26, kWidth - 8, 22, kAccent);
  M5.Display.setTextColor(kText, kBg);
  M5.Display.drawString(clip(buffer.length() ? buffer : String("_"), kWidth - 18), 8, 30);
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.drawString(clip(hint, kWidth - 12), 6, 58);
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.drawString("Enter 确认   Del 退格   Tab 返回", 6, 78);
}

void Ui::qr(const String& payload, const String& status, const String& hint) {
  if (!qr_ || !qrData_) {
    message("二维码", {"内存不足"});
    return;
  }

  M5.Display.fillScreen(0xFFFF);
  M5.Display.setTextColor(0x0000, 0xFFFF);
  M5.Display.drawString("网易云扫码登录", 128, 6);

  if (payload != qrPayload_) {
    if (qrcode_initText(qr_, qrData_, 5, ECC_LOW, payload.c_str()) == 0) {
      qrPayload_ = payload;
    } else {
      qrPayload_ = "";
    }
  }

  if (qrPayload_.length()) {
    constexpr int scale = 3;
    constexpr int quiet = 2;
    const int size = qr_->size * scale;
    const int x0 = 4 + quiet * scale;
    const int y0 = 18;
    M5.Display.fillRect(x0 - quiet * scale, y0 - quiet * scale, size + quiet * 2 * scale,
                        size + quiet * 2 * scale, 0xFFFF);
    M5.Display.startWrite();
    for (int y = 0; y < qr_->size; ++y) {
      for (int x = 0; x < qr_->size; ++x) {
        if (qrcode_getModule(qr_, x, y)) {
          M5.Display.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, 0x0000);
        }
      }
    }
    M5.Display.endWrite();
  }

  M5.Display.setTextColor(0x0000, 0xFFFF);
  M5.Display.drawString(clip(status, 105), 128, 34);
  M5.Display.setTextColor(0x4208, 0xFFFF);
  M5.Display.drawString(clip(hint, 105), 128, 58);
}

void Ui::player(const Track& track, bool paused, bool liked, uint32_t posMs, uint32_t durMs,
                int volume, bool downloading, uint8_t dlPercent, const String& modeText) {
  M5.Display.fillScreen(kBg);
  header(paused ? "已暂停" : "正在播放", modeText);

  M5.Display.setTextColor(kText, kBg);
  M5.Display.drawString(clip(track.name, kWidth - 12), 6, 22);

  M5.Display.setTextColor(kDim, kBg);
  String sub = track.artists;
  if (track.album.length()) sub += " / " + track.album;
  M5.Display.drawString(clip(sub, kWidth - 12), 6, 38);

  // Progress bar.
  const int barX = 8, barY = 56, barW = kWidth - 16, barH = 6;
  M5.Display.fillRect(barX, barY, barW, barH, 0x2104);
  uint32_t pos = posMs;
  const uint32_t dur = durMs ? durMs : 1;
  if (pos > dur) pos = dur;
  const int fillW = static_cast<int>((static_cast<uint64_t>(pos) * barW) / dur);
  M5.Display.fillRect(barX, barY, fillW, barH, kAccent);

  M5.Display.setTextColor(kDim, kBg);
  M5.Display.drawString(mmss(pos), barX, barY + 9);
  const String total = mmss(durMs);
  M5.Display.drawString(total, kWidth - M5.Display.textWidth(total) - barX, barY + 9);

  String state;
  uint16_t stateColor = kOk;
  if (downloading) {
    state = "缓存到 SD: " + String((unsigned)dlPercent) + "%";
    stateColor = kAccent;
  } else if (paused) {
    state = "已暂停";
    stateColor = kDim;
  } else {
    state = "播放中";
  }
  M5.Display.setTextColor(stateColor, kBg);
  M5.Display.drawString(state, 8, 78);

  M5.Display.setTextColor(liked ? 0xF800 : kDim, kBg);
  const String like = liked ? "* 已喜欢" : "- 未喜欢";
  M5.Display.drawString(like, 8, 93);

  M5.Display.setTextColor(kDim, kBg);
  const String vol = "音量 " + String(volume);
  M5.Display.drawString(vol, kWidth - M5.Display.textWidth(vol) - 8, 93);

  M5.Display.setTextColor(kDim, kBg);
  M5.Display.drawString("Space 播放/暂停  N/P 切歌", 6, 106);
  M5.Display.drawString("Enter 歌词  L 喜欢  Q 队列", 6, kFootY);
}

void Ui::lyrics(const String& title, const String& artist,
                const std::vector<LyricLine>& lines, int cur, bool paused) {
  M5.Display.fillScreen(kBg);
  header("歌词", paused ? "暂停" : "");
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.drawString(clip(title + " - " + artist, kWidth - 12), 6, 20);

  if (lines.empty()) {
    M5.Display.setTextColor(kDim, kBg);
    M5.Display.drawString("暂无歌词", 6, 52);
    footer("Tab 返回");
    return;
  }

  constexpr int kLyricRows = 5;
  int first = cur - 2;
  if (first < 0) first = 0;
  if (first > static_cast<int>(lines.size()) - kLyricRows) {
    first = std::max(0, static_cast<int>(lines.size()) - kLyricRows);
  }
  int y = 38;
  for (int i = 0; i < kLyricRows; ++i) {
    const int idx = first + i;
    if (idx >= static_cast<int>(lines.size())) break;
    const bool active = idx == cur;
    M5.Display.setTextColor(active ? kAccent : kDim, kBg);
    M5.Display.drawString(clip(lines[idx].text, kWidth - 12), 6, y);
    y += 16;
  }
  footer("Tab 返回  Space 播放/暂停");
}
