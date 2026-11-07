#pragma once

#include <Arduino.h>

#include <deque>
#include <vector>

#include "app/PlayerController.h"
#include "audio/AudioEngine.h"
#include "model/Types.h"
#include "net/AuthService.h"
#include "net/CookieJar.h"
#include "net/WifiManager.h"
#include "storage/CookieStore.h"
#include "storage/Settings.h"
#include "ui/Ui.h"

// Application orchestration.
//
//  * UI task (Arduino loop): keyboard, drawing, state machine.  Never blocks
//    on network/SD/decoding.
//  * Worker task (core 0): Wi-Fi, login, NetEase API, URL resolution.
//  * Audio task (owned by AudioEngine, core 1): download to SD (optional) and
//    MP3 decode -> M5.Speaker.
class App {
 public:
  App();
  void begin();
  void loop();

 private:
  enum class Screen : uint8_t {
    Boot,
    Home,
    SearchInput,
    List,
    Player,
    Lyrics,
    Qr,
    Account,
    Settings,
    WifiSsid,
    WifiPass,
    Message
  };

  enum class ListKind : uint8_t {
    Menu,
    Songs,
    Artists,
    Albums,
    Playlists,
    QueueList,
    Settings
  };

  enum CmdKind : uint8_t {
    CmdConnectWifi,
    CmdRestore,
    CmdQrStart,
    CmdQrPoll,
    CmdLogout,
    CmdAccount,
    CmdSearch,
    CmdPlaylists,
    CmdPlaylist,
    CmdLiked,
    CmdRecommend,
    CmdRecent,
    CmdFm,
    CmdPlay,
    CmdLike,
    CmdLyric
  };

  enum EventKind : uint8_t {
    EvWifiOk,
    EvWifiFail,
    EvRestored,
    EvNoSession,
    EvSessionExpired,
    EvOffline,
    EvQrReady,
    EvQrStatus,
    EvLoginOk,
    EvLoginFail,
    EvSearch,
    EvPlaylists,
    EvPlaylist,
    EvLiked,
    EvRecommend,
    EvRecent,
    EvFm,
    EvTrackUrl,
    EvTrackFail,
    EvLike,
    EvLyric,
    EvAccount,
    EvLoggedOut
  };

  struct Cmd {
    uint8_t kind = 0;
    int64_t id = 0;
    int type = 0;
    bool flag = false;
    String text;
    Track track;
  };

  struct Event {
    uint8_t kind = 0;
    bool ok = false;
    int code = 0;
    String msg;
    String aux;
    SearchResult search;
    std::vector<Track> tracks;
    std::vector<PlaylistInfo> playlists;
    PlaylistInfo playlist;
    Track track;
    AccountInfo account;
  };

  static void workerEntry(void* arg);
  void workerLoop();
  void runCmd(const Cmd& cmd);
  void pushCmd(const Cmd& cmd);
  void pushEvent(Event&& ev);
  bool popEvent(Event& ev);

  void handleKeys();
  void handleEvent(const Event& ev);
  void tick();
  void draw();

  void showHome();
  void buildMenu();
  void openList(ListKind kind, const String& title);
  void confirm();
  void adjust(int dir);
  void moveSel(int dir);
  void goBack();
  void submitInput();
  void startSearch();
  void startLogin();
  void playIndex(int idx);
  void playCurrent();
  void nextTrack();
  void prevTrack();
  void toggleLike();
  void openSettings();
  std::vector<String> settingsItems() const;
  void showMessage(const String& title, const std::vector<String>& lines,
                   const String& hint = "Enter 重试  Tab 返回");
  bool needLogin();
  int currentLyricIndex() const;
  void setStatus(const String& s) {
    status_ = s;
    dirty_ = true;
  }

  WifiManager wifi_;
  CookieJar jar_;
  CookieStore cookieStore_;
  AuthService auth_;
  Settings settings_;
  AudioEngine audio_;
  PlayerController player_;
  Ui ui_;

  TaskHandle_t worker_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;
  std::deque<Cmd> cmds_;
  std::deque<Event> events_;

  Screen screen_ = Screen::Boot;
  ListKind listKind_ = ListKind::Menu;
  String title_;
  std::vector<String> items_;
  std::vector<Track> songs_;
  std::vector<PlaylistInfo> playlists_;
  SearchResult search_;
  int sel_ = 0;
  int top_ = 0;
  int searchType_ = 1;  // 1 songs, 100 artists, 10 albums, 1000 playlists
  String inputBuf_;
  String wifiSsid_;
  String wifiPass_;

  bool loggedIn_ = false;
  bool qrActive_ = false;
  bool fmMode_ = false;
  bool fmAppendPending_ = false;
  bool pendingPlayLiked_ = false;
  bool dirty_ = true;
  String qrPayload_;
  String qrStatus_;
  String status_;
  String messageTitle_;
  std::vector<String> messageLines_;
  String messageHint_;

  std::vector<LyricLine> lyrics_;
  int64_t lyricTrackId_ = 0;
  int lyricIndex_ = 0;

  uint32_t lastQrPollMs_ = 0;
  uint32_t qrRestartMs_ = 0;
  uint32_t lastDrawMs_ = 0;
  uint32_t lastBootMs_ = 0;
  uint32_t lastConnectAttemptMs_ = 0;
};
