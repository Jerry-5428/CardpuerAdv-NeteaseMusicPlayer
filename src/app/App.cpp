#include "App.h"

#include <M5Cardputer.h>

#include <algorithm>

#include "AppConfig.h"
#include "Log.h"
#include "storage/SdCard.h"

namespace {

constexpr const char* kTag = "APP";

const char* modeName(PlayMode m) {
  switch (m) {
    case PlayMode::Sequential:
      return "顺序播放";
    case PlayMode::RepeatOne:
      return "单曲循环";
    case PlayMode::Shuffle:
      return "随机播放";
    default:
      return "列表循环";
  }
}

const char* qualityName(AudioQuality q) {
  switch (q) {
    case AudioQuality::Higher:
      return "较高";
    case AudioQuality::ExHigh:
      return "极高";
    default:
      return "标准";
  }
}

const char* searchTypeName(int t) {
  switch (t) {
    case 100:
      return "歌手";
    case 10:
      return "专辑";
    case 1000:
      return "歌单";
    default:
      return "歌曲";
  }
}

void utf8Pop(String& s) {
  while (s.length() && (static_cast<uint8_t>(s[s.length() - 1]) & 0xC0) == 0x80) {
    s.remove(s.length() - 1);
  }
  if (s.length()) s.remove(s.length() - 1);
}

std::vector<LyricLine> parseLrc(const String& raw) {
  std::vector<LyricLine> out;
  int pos = 0;
  while (pos < static_cast<int>(raw.length())) {
    int end = raw.indexOf('\n', pos);
    if (end < 0) end = raw.length();
    String line = raw.substring(pos, end);
    pos = end + 1;
    line.trim();
    if (!line.startsWith("[")) continue;
    const int close = line.indexOf(']');
    if (close < 3) continue;
    const String tag = line.substring(1, close);
    const int colon = tag.indexOf(':');
    if (colon <= 0) continue;
    const int mm = tag.substring(0, colon).toInt();
    String rest = tag.substring(colon + 1);
    const int dot = rest.indexOf('.');
    const int ss = (dot >= 0) ? rest.substring(0, dot).toInt() : rest.toInt();
    int ms = 0;
    if (dot >= 0) {
      String frac = rest.substring(dot + 1);
      frac.trim();
      if (frac.length() == 2) {
        ms = frac.toInt() * 10;
      } else if (frac.length() == 1) {
        ms = frac.toInt() * 100;
      } else if (frac.length() >= 3) {
        ms = frac.substring(0, 3).toInt();
      }
    }
    String text = line.substring(close + 1);
    text.trim();
    if (!text.length()) continue;
    LyricLine l;
    l.timeMs = static_cast<uint32_t>(mm) * 60000U + static_cast<uint32_t>(ss) * 1000U +
               static_cast<uint32_t>(ms);
    l.text = text;
    out.push_back(l);
  }
  std::sort(out.begin(), out.end(),
            [](const LyricLine& a, const LyricLine& b) { return a.timeMs < b.timeMs; });
  return out;
}

String trackLabel(const Track& t) {
  String s = t.name;
  if (t.artists.length()) {
    s += " - ";
    s += t.artists;
  }
  return s;
}

}  // namespace

App::App() : auth_(jar_, cookieStore_) {}

void App::begin() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  ui_.begin();
  ui_.boot("正在启动...", "初始化存储与网络");

  settings_.begin();
  storage::mountSd();

  player_.setMode(settings_.data().playMode);
  audio_.setRingBytes(static_cast<size_t>(settings_.data().ringKb) * 1024);
  audio_.setSourceMode(settings_.data().sdCache ? AudioEngine::SourceMode::SdCache
                                                : AudioEngine::SourceMode::Stream);
  if (!audio_.begin(settings_.data().volume, static_cast<size_t>(settings_.data().ringKb) * 1024)) {
    LOGW(kTag, "audio engine failed to start");
  }

  lock_ = xSemaphoreCreateMutex();
  if (xTaskCreatePinnedToCore(workerEntry, "worker", cfg::WORKER_TASK_STACK, this,
                              cfg::WORKER_TASK_PRIO, &worker_, cfg::WORKER_TASK_CORE) != pdPASS) {
    LOGE(kTag, "cannot start worker task");
  }

  wifi_.begin();

  if (settings_.hasWifi()) {
    screen_ = Screen::Boot;
    status_ = "正在连接 Wi-Fi...";
    Cmd c;
    c.kind = CmdConnectWifi;
    pushCmd(c);
  } else {
    screen_ = Screen::WifiSsid;
    title_ = "Wi-Fi 名称";
    inputBuf_ = "";
    dirty_ = true;
  }

  lastBootMs_ = millis();
}

void App::loop() {
  M5Cardputer.update();
  handleKeys();

  Event ev;
  while (popEvent(ev)) handleEvent(ev);

  tick();

  if (dirty_) {
    draw();
    dirty_ = false;
    lastDrawMs_ = millis();
  }
  delay(5);
}

// ---------------------------------------------------------------------------
// Worker side
// ---------------------------------------------------------------------------

void App::workerEntry(void* arg) { static_cast<App*>(arg)->workerLoop(); }

void App::workerLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      Cmd c;
      xSemaphoreTake(lock_, portMAX_DELAY);
      if (cmds_.empty()) {
        xSemaphoreGive(lock_);
        break;
      }
      c = cmds_.front();
      cmds_.pop_front();
      xSemaphoreGive(lock_);
      runCmd(c);
    }
  }
}

void App::pushCmd(const Cmd& cmd) {
  if (!lock_) return;
  xSemaphoreTake(lock_, portMAX_DELAY);
  if (cmds_.size() < 16) cmds_.push_back(cmd);
  xSemaphoreGive(lock_);
  if (worker_) xTaskNotifyGive(worker_);
}

void App::pushEvent(Event&& ev) {
  if (!lock_) return;
  xSemaphoreTake(lock_, portMAX_DELAY);
  if (events_.size() < 16) events_.push_back(std::move(ev));
  xSemaphoreGive(lock_);
}

bool App::popEvent(Event& ev) {
  if (!lock_) return false;
  bool any = false;
  xSemaphoreTake(lock_, portMAX_DELAY);
  if (!events_.empty()) {
    ev = std::move(events_.front());
    events_.pop_front();
    any = true;
  }
  xSemaphoreGive(lock_);
  return any;
}

void App::runCmd(const Cmd& c) {
  Event ev;
  ev.kind = 0;
  String err;
  NetEaseApi& api = auth_.api();

  switch (c.kind) {
    case CmdConnectWifi: {
      const bool ok = wifi_.connect(settings_.data().wifiSsid, settings_.data().wifiPass);
      ev.kind = ok ? EvWifiOk : EvWifiFail;
      ev.ok = ok;
      if (!ok) ev.msg = "Wi-Fi 连接失败，请检查密码/信号";
      break;
    }
    case CmdRestore: {
      const AuthService::RestoreResult r = auth_.restore();
      if (r == AuthService::RestoreResult::Restored) {
        ev.kind = EvRestored;
        ev.ok = true;
        ev.account = auth_.account();
      } else if (r == AuthService::RestoreResult::NoSession) {
        ev.kind = EvNoSession;
      } else if (r == AuthService::RestoreResult::Expired) {
        ev.kind = EvSessionExpired;
      } else {
        ev.kind = EvOffline;
        ev.msg = "网络不可用，稍后自动重试";
      }
      break;
    }
    case CmdQrStart: {
      String content;
      if (auth_.startQrLogin(content, err)) {
        ev.kind = EvQrReady;
        ev.ok = true;
        ev.aux = content;
      } else {
        ev.kind = EvLoginFail;
        ev.msg = err;
      }
      break;
    }
    case CmdQrPoll: {
      const AuthService::QrState st = auth_.pollQrLogin(err);
      if (st == AuthService::QrState::Success) {
        ev.kind = EvLoginOk;
        ev.ok = true;
        ev.account = auth_.account();
        auth_.persist();
      } else if (st == AuthService::QrState::WaitScan || st == AuthService::QrState::Scanned ||
                 st == AuthService::QrState::Expired) {
        ev.kind = EvQrStatus;
        ev.code = (st == AuthService::QrState::WaitScan)
                      ? 801
                      : (st == AuthService::QrState::Scanned ? 802 : 800);
      } else {
        ev.kind = EvLoginFail;
        ev.msg = err;
      }
      break;
    }
    case CmdLogout: {
      auth_.logout();
      ev.kind = EvLoggedOut;
      ev.ok = true;
      break;
    }
    case CmdAccount: {
      AccountInfo a;
      ev.kind = EvAccount;
      ev.ok = api.account(a, err);
      ev.account = a;
      ev.msg = err;
      break;
    }
    case CmdSearch: {
      ev.kind = EvSearch;
      ev.ok = api.search(c.text, c.type, static_cast<int>(cfg::MAX_SEARCH_RESULTS), 0, ev.search,
                         err);
      ev.msg = err;
      break;
    }
    case CmdPlaylists: {
      ev.kind = EvPlaylists;
      int64_t uid = auth_.account().uid;
      if (!uid) {
        AccountInfo a;
        String e2;
        if (api.account(a, e2)) uid = a.uid;
      }
      if (!uid) {
        ev.msg = "未登录";
        break;
      }
      ev.ok = api.userPlaylists(uid, ev.playlists, err);
      ev.msg = err;
      break;
    }
    case CmdPlaylist: {
      ev.kind = EvPlaylist;
      ev.ok = api.playlistDetail(c.id, ev.playlist, ev.tracks, err);
      ev.msg = err;
      break;
    }
    case CmdLiked: {
      ev.kind = EvLiked;
      const int64_t uid = auth_.account().uid;
      if (!uid) {
        ev.msg = "未登录";
        break;
      }
      ev.ok = api.likedSongs(uid, ev.tracks, err);
      ev.msg = err;
      break;
    }
    case CmdRecommend: {
      ev.kind = EvRecommend;
      ev.ok = api.recommendSongs(ev.tracks, err);
      ev.msg = err;
      break;
    }
    case CmdRecent: {
      ev.kind = EvRecent;
      const int64_t uid = auth_.account().uid;
      if (!uid) {
        ev.msg = "未登录";
        break;
      }
      ev.ok = api.recentSongs(uid, ev.tracks, err);
      ev.msg = err;
      break;
    }
    case CmdFm: {
      ev.kind = EvFm;
      ev.ok = api.personalFm(ev.tracks, err);
      ev.msg = err;
      ev.code = c.flag ? 1 : 0;
      break;
    }
    case CmdPlay: {
      ev.kind = EvTrackUrl;
      ev.track = c.track;
      String url;
      String type;
      int br = 0;
      int64_t size = 0;
      bool ok = api.songUrl(c.id, settings_.data().quality, url, type, br, size, err);
      if (ok && (type.indexOf("flac") >= 0 || type.indexOf("ape") >= 0)) {
        // libmad only decodes MP3; fall back to standard quality.
        ok = api.songUrl(c.id, AudioQuality::Standard, url, type, br, size, err);
      }
      ev.ok = ok && url.length();
      ev.aux = url;
      if (!ev.ok && !err.length()) err = "无可用播放地址";
      ev.msg = err;
      break;
    }
    case CmdLike: {
      ev.kind = EvLike;
      ev.track = c.track;
      ev.track.liked = c.flag;
      const int64_t uid = auth_.account().uid;
      ev.ok = uid && api.likeSong(c.id, uid, c.flag, err);
      if (!uid) err = "未登录";
      ev.msg = err;
      break;
    }
    case CmdLyric: {
      ev.kind = EvLyric;
      ev.track.id = c.id;
      ev.ok = api.lyric(c.id, ev.aux, err);
      ev.msg = err;
      break;
    }
    default:
      return;
  }

  pushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// UI side
// ---------------------------------------------------------------------------

void App::handleKeys() {
  if (!M5Cardputer.Keyboard.isChange()) return;
  auto& st = M5Cardputer.Keyboard.keysState();

  const bool textInput =
      screen_ == Screen::SearchInput || screen_ == Screen::WifiSsid || screen_ == Screen::WifiPass;
  if (textInput) {
    for (char ch : st.word) {
      if (ch >= 32 && ch < 127) inputBuf_ += ch;
    }
    if (st.del) utf8Pop(inputBuf_);
    if (st.tab) {
      goBack();
    } else if (st.enter) {
      submitInput();
    }
    dirty_ = true;
    return;
  }

  for (char raw : st.word) {
    const char ch = (raw >= 'A' && raw <= 'Z') ? static_cast<char>(raw - 'A' + 'a') : raw;
    switch (ch) {
      case ';':
        moveSel(-1);
        break;
      case '.':
        moveSel(1);
        break;
      case ',':
        adjust(-1);
        break;
      case '/':
        adjust(1);
        break;
      case 'n':
        nextTrack();
        break;
      case 'p':
        prevTrack();
        break;
      case 'l':
        toggleLike();
        break;
      case 'q':
        if (screen_ == Screen::Player) {
          player_.setIndex(player_.index());
          items_.clear();
          const auto& q = player_.queue();
          for (size_t i = 0; i < q.size(); ++i) {
            items_.push_back(String((int)(i + 1)) + ". " + trackLabel(q[i]));
          }
          sel_ = std::max(0, player_.index());
          top_ = std::max(0, sel_ - Ui::kListRows + 1);
          openList(ListKind::QueueList, "播放队列");
        }
        break;
      default:
        break;
    }
  }

  if (st.space) {
    audio_.togglePause();
    dirty_ = true;
  }
  if (st.tab) {
    goBack();
  } else if (st.enter) {
    confirm();
  }
}

void App::handleEvent(const Event& ev) {
  switch (ev.kind) {
    case EvWifiOk: {
      status_ = "Wi-Fi 已连接，恢复登录...";
      Cmd c;
      c.kind = CmdRestore;
      pushCmd(c);
      dirty_ = true;
      break;
    }
    case EvWifiFail: {
      showMessage("网络", {"连接失败", settings_.data().wifiSsid, ev.msg}, "Enter 重试  Tab 返回");
      break;
    }
    case EvRestored: {
      loggedIn_ = true;
      status_ = ev.account.nickname.length() ? ("已登录: " + ev.account.nickname) : "已登录";
      showHome();
      break;
    }
    case EvNoSession: {
      loggedIn_ = false;
      startLogin();
      break;
    }
    case EvSessionExpired: {
      loggedIn_ = false;
      showMessage("登录", {"登录已过期，请重新扫码"}, "Enter 重新登录  Tab 返回");
      messageTitle_ = "登录";
      break;
    }
    case EvOffline: {
      loggedIn_ = jar_.has("MUSIC_U");
      status_ = "离线模式";
      showHome();
      break;
    }
    case EvQrReady: {
      qrPayload_ = ev.aux;
      qrStatus_ = "等待扫码";
      screen_ = Screen::Qr;
      qrActive_ = true;
      lastQrPollMs_ = millis();
      dirty_ = true;
      break;
    }
    case EvQrStatus: {
      if (ev.code == 801) {
        qrStatus_ = "等待扫码";
      } else if (ev.code == 802) {
        qrStatus_ = "已扫码，请确认";
      } else {
        qrStatus_ = "二维码已过期";
        qrActive_ = false;
        qrRestartMs_ = millis() + 2000;
      }
      dirty_ = true;
      break;
    }
    case EvLoginOk: {
      loggedIn_ = true;
      qrActive_ = false;
      status_ = ev.account.nickname.length() ? ("登录成功: " + ev.account.nickname) : "登录成功";
      showHome();
      break;
    }
    case EvLoginFail: {
      qrStatus_ = ev.msg.length() ? ev.msg : "登录失败";
      qrActive_ = false;
      dirty_ = true;
      break;
    }
    case EvSearch: {
      if (!ev.ok) {
        showMessage("搜索失败", {ev.msg});
        break;
      }
      search_ = ev.search;
      items_.clear();
      if (searchType_ == 1) {
        songs_ = search_.songs;
        for (const Track& t : songs_) items_.push_back(trackLabel(t));
        openList(ListKind::Songs, "歌曲: " + search_.keyword);
      } else if (searchType_ == 100) {
        for (const ArtistInfo& a : search_.artists) items_.push_back(a.name);
        openList(ListKind::Artists, "歌手: " + search_.keyword);
      } else if (searchType_ == 10) {
        for (const AlbumInfo& a : search_.albums) items_.push_back(a.name + " - " + a.artist);
        openList(ListKind::Albums, "专辑: " + search_.keyword);
      } else {
        playlists_ = search_.playlists;
        for (const PlaylistInfo& p : playlists_) items_.push_back(p.name);
        openList(ListKind::Playlists, "歌单: " + search_.keyword);
      }
      break;
    }
    case EvPlaylists: {
      if (!ev.ok) {
        showMessage("歌单", {ev.msg});
        break;
      }
      playlists_ = ev.playlists;
      items_.clear();
      for (const PlaylistInfo& p : playlists_) {
        items_.push_back(p.name + " (" + String(p.trackCount) + ")");
      }
      openList(ListKind::Playlists, "我的歌单");
      break;
    }
    case EvPlaylist: {
      if (!ev.ok) {
        showMessage("歌单", {ev.msg});
        break;
      }
      songs_ = ev.tracks;
      items_.clear();
      for (const Track& t : songs_) items_.push_back(trackLabel(t));
      openList(ListKind::Songs, ev.playlist.name);
      break;
    }
    case EvLiked: {
      if (!ev.ok) {
        showMessage("喜欢", {ev.msg});
        break;
      }
      player_.markLikedSet(ev.tracks);
      if (pendingPlayLiked_) {
        pendingPlayLiked_ = false;
        fmMode_ = false;
        songs_ = ev.tracks;
        player_.setQueue(songs_, 0);
        playIndex(0);
      } else {
        songs_ = ev.tracks;
        items_.clear();
        for (const Track& t : songs_) items_.push_back(trackLabel(t));
        openList(ListKind::Songs, "我的收藏");
      }
      break;
    }
    case EvRecommend:
    case EvRecent: {
      if (!ev.ok) {
        showMessage(ev.kind == EvRecommend ? "每日推荐" : "最近播放", {ev.msg});
        break;
      }
      songs_ = ev.tracks;
      items_.clear();
      for (const Track& t : songs_) items_.push_back(trackLabel(t));
      openList(ListKind::Songs, ev.kind == EvRecommend ? "每日推荐" : "最近播放");
      break;
    }
    case EvFm: {
      if (!ev.ok) {
        fmMode_ = false;
        fmAppendPending_ = false;
        showMessage("私人 FM", {ev.msg});
        break;
      }
      if (fmMode_ && !player_.empty() && fmAppendPending_) {
        fmAppendPending_ = false;
        const int next = player_.index() + 1;
        if (player_.size() + ev.tracks.size() > 200) {
          player_.setQueue(ev.tracks, 0);
          playIndex(0);
        } else {
          player_.append(ev.tracks);
          playIndex(next);
        }
      } else if (fmMode_) {
        fmAppendPending_ = false;
        player_.setQueue(ev.tracks, 0);
        playIndex(0);
      }
      break;
    }
    case EvTrackUrl: {
      const Track* cur = player_.current();
      if (!cur || cur->id != ev.track.id) break;
      if (!ev.ok) {
        showMessage("无法播放", {cur->name, ev.msg});
        break;
      }
      audio_.setCookie(jar_.header());
      audio_.play(ev.aux, ev.track);
      screen_ = Screen::Player;
      status_.clear();
      lyrics_.clear();
      lyricTrackId_ = ev.track.id;
      lyricIndex_ = 0;
      Cmd c;
      c.kind = CmdLyric;
      c.id = ev.track.id;
      pushCmd(c);
      dirty_ = true;
      break;
    }
    case EvTrackFail: {
      showMessage("播放失败", {ev.track.name, ev.msg});
      break;
    }
    case EvLike: {
      if (ev.ok) {
        player_.setLiked(ev.track.id, ev.track.liked);
        status_ = ev.track.liked ? "已添加喜欢" : "已取消喜欢";
      } else {
        status_ = "操作失败: " + ev.msg;
      }
      dirty_ = true;
      break;
    }
    case EvLyric: {
      const Track* cur = player_.current();
      if (!cur || cur->id != ev.track.id) break;
      lyrics_ = ev.ok ? parseLrc(ev.aux) : std::vector<LyricLine>();
      lyricIndex_ = 0;
      if (screen_ == Screen::Lyrics) dirty_ = true;
      break;
    }
    case EvAccount: {
      if (ev.ok) {
        loggedIn_ = true;
        dirty_ = true;
      }
      break;
    }
    case EvLoggedOut: {
      loggedIn_ = false;
      fmMode_ = false;
      player_.clear();
      audio_.stop();
      showHome();
      status_ = "已退出登录";
      break;
    }
    default:
      break;
  }
}

void App::tick() {
  wifi_.loop();

  if (screen_ == Screen::Qr && !qrActive_ && qrRestartMs_ &&
      static_cast<int32_t>(millis() - qrRestartMs_) >= 0) {
    qrRestartMs_ = 0;
    if (wifi_.isConnected()) startLogin();
  }

  if (screen_ == Screen::Qr && qrActive_ &&
      millis() - lastQrPollMs_ >= cfg::QR_POLL_INTERVAL_MS) {
    lastQrPollMs_ = millis();
    Cmd c;
    c.kind = CmdQrPoll;
    pushCmd(c);
  }

  const bool ended = audio_.consumeEnded();
  const String audioError = audio_.consumeError();
  if (ended && audioError.length()) {
    LOGW(kTag, "audio: %s", audioError.c_str());
    showMessage("播放失败", {audioError}, "Enter 重试  Tab 返回");
  } else if (ended) {
    if (!player_.empty()) {
      if (fmMode_ && player_.index() + 1 >= static_cast<int>(player_.size())) {
        if (!fmAppendPending_) {
          fmAppendPending_ = true;
          Cmd c;
          c.kind = CmdFm;
          c.flag = true;
          pushCmd(c);
        }
      } else {
        const int ni = player_.nextIndex();
        if (ni >= 0) {
          player_.setIndex(ni);
          playCurrent();
        }
      }
    }
  }

  if (screen_ == Screen::Lyrics && !lyrics_.empty()) {
    const int idx = currentLyricIndex();
    if (idx != lyricIndex_) {
      lyricIndex_ = idx;
      dirty_ = true;
    }
  }

  const bool playingScreen = screen_ == Screen::Player || screen_ == Screen::Lyrics;
  if (playingScreen) {
    const uint32_t interval = audio_.isDownloading() ? 250 : 500;
    if (millis() - lastDrawMs_ >= interval) dirty_ = true;
  }
}

int App::currentLyricIndex() const {
  if (lyrics_.empty()) return -1;
  const uint32_t pos = audio_.positionMs();
  int idx = 0;
  for (size_t i = 0; i < lyrics_.size(); ++i) {
    if (lyrics_[i].timeMs <= pos) {
      idx = static_cast<int>(i);
    } else {
      break;
    }
  }
  return idx;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void App::buildMenu() {
  items_ = {"搜索",      "我的歌单", "私人 FM", "红心模式", "每日推荐",
            "最近播放",  "我的收藏", "账户",    "网络",     "设置"};
}

void App::showHome() {
  screen_ = Screen::Home;
  buildMenu();
  sel_ = 0;
  top_ = 0;
  listKind_ = ListKind::Menu;
  dirty_ = true;
}

void App::openList(ListKind kind, const String& title) {
  listKind_ = kind;
  title_ = title;
  screen_ = Screen::List;
  sel_ = 0;
  top_ = 0;
  dirty_ = true;
}

void App::moveSel(int dir) {
  if (screen_ != Screen::List && screen_ != Screen::Settings && screen_ != Screen::Home) return;
  const int n = static_cast<int>(items_.size());
  if (n == 0) return;
  sel_ += dir;
  if (sel_ < 0) sel_ = 0;
  if (sel_ >= n) sel_ = n - 1;
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + Ui::kListRows) top_ = sel_ - Ui::kListRows + 1;
  dirty_ = true;
}

void App::confirm() {
  switch (screen_) {
    case Screen::Home: {
      switch (sel_) {
        case 0:
          screen_ = Screen::SearchInput;
          inputBuf_ = "";
          searchType_ = 1;
          dirty_ = true;
          break;
        case 1:
        case 3:
        case 4:
        case 5:
        case 6: {
          if (needLogin()) break;
          Cmd c;
          if (sel_ == 1) {
            c.kind = CmdPlaylists;
            status_ = "加载歌单...";
          } else if (sel_ == 3) {
            c.kind = CmdLiked;
            pendingPlayLiked_ = true;
            status_ = "加载红心歌曲...";
          } else if (sel_ == 4) {
            c.kind = CmdRecommend;
            status_ = "获取每日推荐...";
          } else if (sel_ == 5) {
            c.kind = CmdRecent;
            status_ = "获取最近播放...";
          } else {
            c.kind = CmdLiked;
            pendingPlayLiked_ = false;
            status_ = "加载我的收藏...";
          }
          pushCmd(c);
          dirty_ = true;
          break;
        }
        case 2: {
          if (needLogin()) break;
          fmMode_ = true;
          fmAppendPending_ = false;
          player_.clear();
          Cmd c;
          c.kind = CmdFm;
          pushCmd(c);
          status_ = "私人 FM...";
          dirty_ = true;
          break;
        }
        case 7: {
          screen_ = Screen::Account;
          Cmd c;
          c.kind = CmdAccount;
          pushCmd(c);
          dirty_ = true;
          break;
        }
        case 8: {
          std::vector<String> lines;
          lines.push_back(String("SSID: ") + wifi_.ssid());
          lines.push_back(String("IP: ") + wifi_.ip());
          lines.push_back(String("RSSI: ") + String((int)wifi_.rssi()) + " dBm");
          lines.push_back(storage::sdDescription());
          showMessage("网络", lines, "Tab 返回");
          break;
        }
        case 9:
          openSettings();
          break;
        default:
          break;
      }
      break;
    }
    case Screen::List:
    case Screen::Settings: {
      if (listKind_ == ListKind::Menu) break;
      if (screen_ == Screen::Settings) {
        if (sel_ == 0 || sel_ == 1 || sel_ == 4) adjust(1);
        break;
      }
      if (listKind_ == ListKind::Songs) {
        if (sel_ < static_cast<int>(songs_.size())) {
          fmMode_ = false;
          player_.setQueue(songs_, sel_);
          playIndex(sel_);
        }
      } else if (listKind_ == ListKind::QueueList) {
        if (sel_ < static_cast<int>(player_.size())) {
          fmMode_ = false;
          player_.setIndex(sel_);
          playCurrent();
        }
      } else if (listKind_ == ListKind::Playlists) {
        if (sel_ < static_cast<int>(playlists_.size())) {
          Cmd c;
          c.kind = CmdPlaylist;
          c.id = playlists_[sel_].id;
          pushCmd(c);
          status_ = "加载歌单...";
          dirty_ = true;
        }
      } else if (listKind_ == ListKind::Artists) {
        if (sel_ < static_cast<int>(search_.artists.size())) {
          showMessage("歌手", {search_.artists[sel_].name, "歌手详情将在后续版本补全"},
                      "Tab 返回");
        }
      } else if (listKind_ == ListKind::Albums) {
        if (sel_ < static_cast<int>(search_.albums.size())) {
          showMessage("专辑",
                      {search_.albums[sel_].name, search_.albums[sel_].artist,
                       "专辑详情将在后续版本补全"},
                      "Tab 返回");
        }
      }
      break;
    }
    case Screen::Player:
      screen_ = Screen::Lyrics;
      dirty_ = true;
      break;
    case Screen::Lyrics:
      screen_ = Screen::Player;
      dirty_ = true;
      break;
    case Screen::Account: {
      if (loggedIn_) {
        Cmd c;
        c.kind = CmdLogout;
        pushCmd(c);
      } else {
        startLogin();
      }
      break;
    }
    case Screen::Message: {
      if (messageTitle_ == "网络") {
        Cmd c;
        c.kind = CmdConnectWifi;
        pushCmd(c);
        showMessage("网络", {"正在连接...", settings_.data().wifiSsid}, "请稍候");
      } else if (messageTitle_ == "登录") {
        startLogin();
      } else if (messageTitle_ == "播放失败") {
        playCurrent();
      } else {
        goBack();
      }
      break;
    }
    default:
      break;
  }
}

void App::adjust(int dir) {
  if (screen_ == Screen::Player || screen_ == Screen::Lyrics) {
    int v = settings_.data().volume + dir * 26;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    settings_.mutableData().volume = static_cast<uint8_t>(v);
    audio_.setVolume(static_cast<uint8_t>(v));
    settings_.saveAudio();
    dirty_ = true;
    return;
  }
  if (screen_ == Screen::SearchInput) {
    static const int types[] = {1, 100, 10, 1000};
    int idx = 0;
    for (int i = 0; i < 4; ++i)
      if (types[i] == searchType_) idx = i;
    idx = (idx + (dir > 0 ? 1 : 3)) % 4;
    searchType_ = types[idx];
    dirty_ = true;
    return;
  }
  if (screen_ != Screen::Settings) return;

  auto& d = settings_.mutableData();
  if (sel_ == 0) {
    d.sdCache = !d.sdCache;
    audio_.setSourceMode(d.sdCache ? AudioEngine::SourceMode::SdCache
                                   : AudioEngine::SourceMode::Stream);
  } else if (sel_ == 1) {
    int q = static_cast<int>(d.quality) + dir;
    if (q < 0) q = 0;
    if (q > 2) q = 2;
    d.quality = static_cast<AudioQuality>(q);
  } else if (sel_ == 2) {
    int v = d.volume + dir * 26;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    d.volume = static_cast<uint8_t>(v);
    audio_.setVolume(d.volume);
  } else if (sel_ == 3) {
    static const uint16_t sizes[] = {32, 48, 64, 96};
    int idx = 0;
    for (int i = 0; i < 4; ++i)
      if (sizes[i] == d.ringKb) idx = i;
    idx = (idx + (dir > 0 ? 1 : 3)) % 4;
    d.ringKb = sizes[idx];
    audio_.setRingBytes(static_cast<size_t>(d.ringKb) * 1024);
  } else if (sel_ == 4) {
    if (dir > 0) {
      player_.cycleMode();
    } else {
      const int m = (static_cast<int>(player_.mode()) + 3) % 4;
      player_.setMode(static_cast<PlayMode>(m));
    }
    d.playMode = player_.mode();
  }
  settings_.saveAudio();
  dirty_ = true;
}

void App::goBack() {
  switch (screen_) {
    case Screen::Player:
      showHome();
      break;
    case Screen::Lyrics:
      screen_ = Screen::Player;
      dirty_ = true;
      break;
    case Screen::List:
      if (listKind_ == ListKind::QueueList) {
        screen_ = Screen::Player;
        dirty_ = true;
      } else {
        showHome();
      }
      break;
    case Screen::SearchInput:
    case Screen::Settings:
    case Screen::Account:
    case Screen::Qr:
    case Screen::Message:
    case Screen::WifiSsid:
      qrActive_ = false;
      showHome();
      break;
    case Screen::WifiPass:
      screen_ = Screen::WifiSsid;
      inputBuf_ = wifiSsid_;
      dirty_ = true;
      break;
    default:
      break;
  }
}

void App::submitInput() {
  if (screen_ == Screen::SearchInput) {
    if (!inputBuf_.length()) return;
    startSearch();
  } else if (screen_ == Screen::WifiSsid) {
    if (!inputBuf_.length()) return;
    wifiSsid_ = inputBuf_;
    inputBuf_ = "";
    screen_ = Screen::WifiPass;
    title_ = "Wi-Fi 密码";
    dirty_ = true;
  } else if (screen_ == Screen::WifiPass) {
    wifiPass_ = inputBuf_;
    settings_.saveWifi(wifiSsid_, wifiPass_);
    Cmd c;
    c.kind = CmdConnectWifi;
    pushCmd(c);
    showMessage("网络", {"正在连接...", wifiSsid_}, "请稍候");
  }
}

void App::startSearch() {
  Cmd c;
  c.kind = CmdSearch;
  c.text = inputBuf_;
  c.type = searchType_;
  pushCmd(c);
  showMessage("搜索", {String("搜索: ") + inputBuf_, String("类型: ") + searchTypeName(searchType_)},
              "请稍候");
}

bool App::needLogin() {
  if (loggedIn_) return false;
  startLogin();
  return true;
}

void App::startLogin() {
  if (!wifi_.isConnected()) {
    showMessage("网络", {"Wi-Fi 未连接，请先联网"}, "Enter 重试  Tab 返回");
    return;
  }
  qrPayload_ = "";
  qrStatus_ = "获取二维码...";
  qrActive_ = true;
  screen_ = Screen::Qr;
  lastQrPollMs_ = millis();
  Cmd c;
  c.kind = CmdQrStart;
  pushCmd(c);
  dirty_ = true;
}

void App::playCurrent() {
  const Track* t = player_.current();
  if (!t) return;
  Cmd c;
  c.kind = CmdPlay;
  c.id = t->id;
  c.track = *t;
  pushCmd(c);
  audio_.setCookie(jar_.header());
  status_ = "获取播放地址...";
  dirty_ = true;
}

void App::playIndex(int idx) {
  if (idx < 0 || idx >= static_cast<int>(player_.size())) return;
  player_.setIndex(idx);
  screen_ = Screen::Player;
  lyrics_.clear();
  lyricTrackId_ = 0;
  lyricIndex_ = 0;
  playCurrent();
}

void App::nextTrack() {
  if (player_.empty()) return;
  const int ni = player_.nextIndex();
  if (ni >= 0) {
    player_.setIndex(ni);
    playCurrent();
  }
}

void App::prevTrack() {
  if (player_.empty()) return;
  const int pi = player_.prevIndex();
  if (pi >= 0) {
    player_.setIndex(pi);
    playCurrent();
  }
}

void App::toggleLike() {
  const Track* t = player_.current();
  if (!t) return;
  if (needLogin()) return;
  const bool liked = !player_.isLiked(t->id);
  player_.setLiked(t->id, liked);
  Cmd c;
  c.kind = CmdLike;
  c.id = t->id;
  c.flag = liked;
  c.track = *t;
  pushCmd(c);
  status_ = liked ? "正在添加喜欢..." : "正在取消喜欢...";
  dirty_ = true;
}

void App::openSettings() {
  screen_ = Screen::Settings;
  listKind_ = ListKind::Settings;
  title_ = "设置";
  sel_ = 0;
  top_ = 0;
  items_ = settingsItems();
  dirty_ = true;
}

std::vector<String> App::settingsItems() const {
  const auto& d = settings_.data();
  std::vector<String> out;
  out.push_back(String("播放源: ") + (d.sdCache ? "SD 缓存" : "流式"));
  out.push_back(String("音质: ") + qualityName(d.quality));
  out.push_back(String("音量: ") + String(d.volume * 100 / 255));
  out.push_back(String("缓冲: ") + String(d.ringKb) + " KB");
  out.push_back(String("播放模式: ") + modeName(player_.mode()));
  return out;
}

void App::showMessage(const String& title, const std::vector<String>& lines,
                      const String& hint) {
  messageTitle_ = title;
  messageLines_ = lines;
  messageHint_ = hint;
  screen_ = Screen::Message;
  dirty_ = true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void App::draw() {
  switch (screen_) {
    case Screen::Boot: {
      ui_.boot(status_.length() ? status_ : "正在启动...", wifi_.isConnected() ? wifi_.ip() : "");
      break;
    }
    case Screen::Home: {
      const String hint = status_.length() ? status_ : "Enter 选择  Tab 返回";
      ui_.list("网易云音乐", items_, sel_, top_, hint);
      break;
    }
    case Screen::List: {
      String hint = "Enter 选择  Tab 返回";
      if (listKind_ == ListKind::QueueList) hint = "Enter 播放  Q 返回";
      ui_.list(title_, items_, sel_, top_, hint);
      break;
    }
    case Screen::Settings: {
      ui_.list("设置", settingsItems(), sel_, top_, "←→ 调整  Tab 返回");
      break;
    }
    case Screen::Player: {
      const Track* t = player_.current();
      if (!t) {
        ui_.message("播放器", {"队列为空", "先搜索或打开歌单"});
        break;
      }
      ui_.player(*t, audio_.isPaused(), player_.isLiked(t->id), audio_.positionMs(),
                 t->durationMs, settings_.data().volume * 100 / 255, audio_.isDownloading(),
                 audio_.downloadPercent(), modeName(player_.mode()));
      break;
    }
    case Screen::Lyrics: {
      const Track* t = player_.current();
      if (!t) {
        ui_.message("歌词", {"队列为空"});
        break;
      }
      ui_.lyrics(t->name, t->artists, lyrics_, lyricIndex_, audio_.isPaused());
      break;
    }
    case Screen::Qr: {
      ui_.qr(qrPayload_, qrStatus_, "Tab 返回");
      break;
    }
    case Screen::Account: {
      std::vector<String> lines;
      const AccountInfo& a = auth_.account();
      lines.push_back(String("昵称: ") + (a.nickname.length() ? a.nickname : "-"));
      lines.push_back(String("UID: ") + String((long long)a.uid));
      lines.push_back(loggedIn_ ? "状态: 已登录" : "状态: 未登录");
      lines.push_back(String("播放源: ") + (settings_.data().sdCache ? "SD 缓存" : "流式"));
      ui_.message("账户", lines);
      break;
    }
    case Screen::WifiSsid: {
      ui_.input("Wi-Fi 名称", inputBuf_, "", "输入 2.4GHz Wi-Fi 名称");
      break;
    }
    case Screen::WifiPass: {
      String masked;
      for (size_t i = 0; i < inputBuf_.length(); ++i) masked += '*';
      ui_.input("Wi-Fi 密码", masked, wifiSsid_, "密码仅保存在设备 NVS");
      break;
    }
    case Screen::Message: {
      ui_.message(messageTitle_, messageLines_);
      break;
    }
    case Screen::SearchInput: {
      ui_.input("搜索", inputBuf_, searchTypeName(searchType_), "←→ 切换: 歌曲/歌手/专辑/歌单");
      break;
    }
    default:
      break;
  }
}
