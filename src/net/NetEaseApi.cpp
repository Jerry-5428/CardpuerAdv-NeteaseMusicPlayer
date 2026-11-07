#include "NetEaseApi.h"

#include <ArduinoJson.h>
#include <esp_system.h>

#include <algorithm>

#include "AppConfig.h"
#include "Eapi.h"
#include "Log.h"

namespace {

constexpr const char* kTag = "API";
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Safari/537.36 Chrome/91.0.4472.164 NeteaseMusicDesktop/2.10.2.200154";

String urlEncode(const String& in) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(in.length() * 3);
  for (size_t i = 0; i < in.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(in[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else if (c == ' ') {
      out += '+';
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String joinArtists(JsonArray ar) {
  String out;
  for (JsonObject a : ar) {
    if (out.length()) out += '/';
    out += String(a["name"] | "");
  }
  return out;
}

Track parseSong(JsonObject s) {
  Track t;
  t.id = s["id"].as<int64_t>();
  t.name = String(s["name"] | "");
  JsonArray ar = s["ar"].as<JsonArray>();
  if (ar.isNull()) ar = s["artists"].as<JsonArray>();
  t.artists = joinArtists(ar);
  JsonObject al = s["al"].as<JsonObject>();
  if (al.isNull()) al = s["album"].as<JsonObject>();
  t.album = String(al["name"] | "");
  t.durationMs = s["dt"] | 0;
  if (t.durationMs == 0) t.durationMs = s["duration"] | 0;
  const int fee = s["fee"] | 0;
  JsonObject pr = s["privilege"].as<JsonObject>();
  if (fee == 4) t.playable = false;
  if (!pr.isNull()) {
    const int st = pr["st"] | 0;
    if (st < 0) t.playable = false;
  }
  return t;
}

void addSongFilter(JsonVariant song) {
  song["id"] = true;
  song["name"] = true;
  song["dt"] = true;
  song["duration"] = true;
  song["fee"] = true;
  song["ar"][0]["name"] = true;
  song["artists"][0]["name"] = true;
  song["al"]["name"] = true;
  song["album"]["name"] = true;
  song["privilege"]["st"] = true;
}

int jsonCode(JsonDocument& doc) { return doc["code"] | -1; }

}  // namespace

std::vector<HttpHeader> NetEaseApi::baseHeaders(bool withCookie) const {
  std::vector<HttpHeader> h;
  h.push_back({"User-Agent", kUserAgent});
  h.push_back({"Referer", "https://music.163.com/"});
  h.push_back({"Accept", "application/json, text/plain, */*"});
  if (withCookie) {
    String cookie = jar_.header();
    if (cookie.length()) cookie += "; ";
    cookie += "os=pc; appver=8.9.70; deviceId=cardputeradv";
    h.push_back({"Cookie", cookie});
  }
  return h;
}

String NetEaseApi::eapiUrl(const String& apiPath) const {
  // /api/xxx  ->  https://interface3.music.163.com/eapi/xxx
  String rest = apiPath.startsWith("/api") ? apiPath.substring(4) : apiPath;
  return String("https://") + cfg::EAPI_HOST + "/eapi" + rest;
}

String NetEaseApi::headerJson() const {
  StaticJsonDocument<512> h;
  h["os"] = "pc";
  h["appver"] = "8.9.70";
  h["osver"] = "";
  h["deviceId"] = "cardputeradv";
  h["requestId"] = String(random(20000000, 29999999));
  String out;
  serializeJson(h, out);
  return out;
}

bool NetEaseApi::eapi(const String& apiPath, const String& json, String& body, String& err,
                      bool collectCookies, std::vector<String>* setCookies) {
  HttpOptions opt;
  opt.method = "POST";
  opt.contentType = "application/x-www-form-urlencoded";
  opt.body = eapi::body(apiPath, json);
  opt.headers = baseHeaders(true);
  opt.timeoutMs = cfg::HTTP_TIMEOUT_MS;
  opt.maxBody = cfg::HTTP_MAX_RESPONSE;
  opt.collectCookies = collectCookies;

  const HttpResult r = http::request(eapiUrl(apiPath), opt);
  if (!r.ok) {
    err = r.error.length() ? r.error : "network error";
    LOGW(kTag, "eapi %s failed: %s", apiPath.c_str(), err.c_str());
    return false;
  }
  if (r.status != 200) {
    err = String("HTTP ") + r.status;
    return false;
  }
  body = r.body;
  if (collectCookies && setCookies) *setCookies = r.setCookies;
  return true;
}

bool NetEaseApi::plainForm(const String& apiPath, const String& form, String& body,
                           String& err) {
  HttpOptions opt;
  opt.method = "POST";
  opt.contentType = "application/x-www-form-urlencoded";
  opt.body = form;
  opt.headers = baseHeaders(true);
  opt.timeoutMs = cfg::HTTP_TIMEOUT_MS;
  opt.maxBody = cfg::HTTP_MAX_RESPONSE;

  const String url = String("https://") + cfg::API_HOST + apiPath;
  const HttpResult r = http::request(url, opt);
  if (!r.ok) {
    err = r.error.length() ? r.error : "network error";
    LOGW(kTag, "form %s failed: %s", apiPath.c_str(), err.c_str());
    return false;
  }
  if (r.status != 200) {
    err = String("HTTP ") + r.status;
    return false;
  }
  body = r.body;
  return true;
}

// ---------------------------------------------------------------------------
// Authentication
// ---------------------------------------------------------------------------

bool NetEaseApi::qrKey(String& unikey, String& err) {
  StaticJsonDocument<512> d;
  d["type"] = 1;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/login/qrcode/unikey", json, body, err)) return false;

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, body)) {
    err = "bad json";
    return false;
  }
  if (jsonCode(doc) != 200) {
    err = String("code ") + (doc["code"] | -1) + " " + String(doc["message"] | "");
    return false;
  }
  unikey = String(doc["unikey"] | "");
  if (!unikey.length()) {
    err = "empty unikey";
    return false;
  }
  return true;
}

int NetEaseApi::qrCheck(const String& unikey, std::vector<String>& setCookies, String& err) {
  StaticJsonDocument<512> d;
  d["key"] = unikey;
  d["type"] = 1;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/login/qrcode/client/login", json, body, err, true, &setCookies)) {
    return -1;
  }
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, body)) {
    err = "bad json";
    return -1;
  }
  return jsonCode(doc);
}

bool NetEaseApi::account(AccountInfo& out, String& err) {
  StaticJsonDocument<512> d;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/w/nuser/account/get", json, body, err)) return false;

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) {
    err = "bad json";
    return false;
  }
  if (jsonCode(doc) != 200) {
    err = String("code ") + (doc["code"] | -1);
    return false;
  }
  JsonObject acct = doc["account"];
  JsonObject prof = doc["profile"];
  if (acct.isNull() || (acct["id"].as<int64_t>()) == 0) {
    err = "未登录";
    return false;
  }
  out.uid = acct["id"].as<int64_t>();
  out.nickname = String(prof["nickname"] | "");
  out.valid = true;
  return true;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

bool NetEaseApi::search(const String& keyword, int type, int limit, int offset,
                        SearchResult& out, String& err) {
  out = SearchResult();
  out.keyword = keyword;

  StaticJsonDocument<768> d;
  d["s"] = keyword;
  d["type"] = type;
  d["limit"] = limit;
  d["offset"] = offset;
  d["total"] = true;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/cloudsearch/pc", json, body, err)) return false;
  if (body.length() > 200 * 1024) {
    err = "响应过大";
    return false;
  }

  DynamicJsonDocument filter(4096);
  filter["code"] = true;
  if (type == 1) {
    addSongFilter(filter["result"]["songs"][0]);
  } else if (type == 100) {
    filter["result"]["artists"][0]["id"] = true;
    filter["result"]["artists"][0]["name"] = true;
    filter["result"]["artists"][0]["albumSize"] = true;
    filter["result"]["artists"][0]["musicSize"] = true;
  } else if (type == 10) {
    filter["result"]["albums"][0]["id"] = true;
    filter["result"]["albums"][0]["name"] = true;
    filter["result"]["albums"][0]["artist"]["name"] = true;
    filter["result"]["albums"][0]["size"] = true;
  } else {
    filter["result"]["playlists"][0]["id"] = true;
    filter["result"]["playlists"][0]["name"] = true;
    filter["result"]["playlists"][0]["creator"]["nickname"] = true;
    filter["result"]["playlists"][0]["trackCount"] = true;
    filter["result"]["playlists"][0]["subscribed"] = true;
    filter["result"]["playlists"][0]["specialType"] = true;
  }

  DynamicJsonDocument doc(24576);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "解析失败";
    return false;
  }
  if (jsonCode(doc) != 200) {
    int c = jsonCode(doc);
    err = (c == -462) ? String("触发风控(-462)，请稍后重试") : String("code ") + c;
    return false;
  }

  JsonArray songs = doc["result"]["songs"].as<JsonArray>();
  for (JsonObject s : songs) {
    if (out.songs.size() >= cfg::MAX_SEARCH_RESULTS) break;
    out.songs.push_back(parseSong(s));
  }
  JsonArray artists = doc["result"]["artists"].as<JsonArray>();
  for (JsonObject a : artists) {
    if (out.artists.size() >= cfg::MAX_SEARCH_RESULTS) break;
    ArtistInfo info;
    info.id = a["id"].as<int64_t>();
    info.name = String(a["name"] | "");
    info.albumCount = a["albumSize"] | 0;
    info.songCount = a["musicSize"] | 0;
    out.artists.push_back(info);
  }
  JsonArray albums = doc["result"]["albums"].as<JsonArray>();
  for (JsonObject a : albums) {
    if (out.albums.size() >= cfg::MAX_SEARCH_RESULTS) break;
    AlbumInfo info;
    info.id = a["id"].as<int64_t>();
    info.name = String(a["name"] | "");
    info.artist = String(a["artist"]["name"] | "");
    info.songCount = a["size"] | 0;
    out.albums.push_back(info);
  }
  JsonArray playlists = doc["result"]["playlists"].as<JsonArray>();
  for (JsonObject p : playlists) {
    if (out.playlists.size() >= cfg::MAX_SEARCH_RESULTS) break;
    PlaylistInfo info;
    info.id = p["id"].as<int64_t>();
    info.name = String(p["name"] | "");
    info.creator = String(p["creator"]["nickname"] | "");
    info.trackCount = p["trackCount"] | 0;
    info.subscribed = p["subscribed"] | false;
    info.specialType = p["specialType"] | 0;
    out.playlists.push_back(info);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Playback URL
// ---------------------------------------------------------------------------

bool NetEaseApi::songUrl(int64_t id, AudioQuality quality, String& url, String& type,
                         int& br, int64_t& size, String& err) {
  url = "";
  type = "";
  br = 0;
  size = 0;

  StaticJsonDocument<512> d;
  d["ids"].add(id);
  d["level"] = qualityLevel(quality);
  d["encodeType"] = "mp3";
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/song/enhance/player/url/v1", json, body, err)) return false;

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) {
    err = "bad json";
    return false;
  }
  const int code = jsonCode(doc);
  if (code == -462) {
    err = "触发风控(-462)：请在手机 App 完成验证或稍后重试";
    return false;
  }
  if (code != 200) {
    err = String("code ") + code;
    return false;
  }
  JsonObject item = doc["data"][0];
  url = String(item["url"] | "");
  type = String(item["type"] | "");
  br = item["br"] | 0;
  size = item["size"] | 0;
  if (!url.length()) {
    err = "无可用播放地址（可能无版权或需要会员）";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Details / lyrics
// ---------------------------------------------------------------------------

bool NetEaseApi::lyric(int64_t id, String& raw, String& err) {
  raw = "";
  const String form = String("id=") + id + "&lv=-1&kv=-1&tv=-1";
  String body;
  if (!plainForm("/api/song/lyric", form, body, err)) return false;
  if (body.length() > 120 * 1024) {
    err = "歌词过大";
    return false;
  }

  DynamicJsonDocument filter(512);
  filter["lrc"]["lyric"] = true;
  filter["tlyric"]["lyric"] = true;

  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "解析失败";
    return false;
  }
  JsonObject lrc = doc["lrc"];
  raw = String(lrc["lyric"] | "");
  if (!raw.length()) {
    err = "无歌词";
    return false;
  }
  return true;
}

bool NetEaseApi::songDetail(const std::vector<int64_t>& ids, std::vector<Track>& out,
                            String& err) {
  if (ids.empty()) return true;

  String c = "[";
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i) c += ',';
    c += "{\"id\":";
    c += String(ids[i]);
    c += ",\"v\":0}";
  }
  c += "]";
  const String form = "c=" + urlEncode(c);

  String body;
  if (!plainForm("/api/v3/song/detail", form, body, err)) return false;
  if (body.length() > 220 * 1024) {
    err = "详情响应过大";
    return false;
  }

  DynamicJsonDocument filter(2048);
  addSongFilter(filter["songs"][0]);
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "解析失败";
    return false;
  }
  for (JsonObject s : doc["songs"].as<JsonArray>()) {
    out.push_back(parseSong(s));
  }
  return true;
}

bool NetEaseApi::playlistDetail(int64_t id, PlaylistInfo& info, std::vector<Track>& tracks,
                                String& err) {
  tracks.clear();
  info = PlaylistInfo();

  const String form = String("id=") + id + "&n=0&s=0";
  String body;
  if (!plainForm("/api/v6/playlist/detail", form, body, err)) return false;
  if (body.length() > 160 * 1024) {
    err = "歌单响应过大";
    return false;
  }

  DynamicJsonDocument filter(1024);
  filter["code"] = true;
  JsonObject p = filter["playlist"].to<JsonObject>();
  p["id"] = true;
  p["name"] = true;
  p["trackCount"] = true;
  p["specialType"] = true;
  p["subscribed"] = true;
  p["creator"]["nickname"] = true;
  p["trackIds"][0]["id"] = true;

  DynamicJsonDocument doc(24576);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "解析失败";
    return false;
  }
  if (jsonCode(doc) != 200) {
    err = String("code ") + jsonCode(doc);
    return false;
  }
  JsonObject pl = doc["playlist"];
  info.id = pl["id"] | id;
  info.name = String(pl["name"] | "");
  info.creator = String(pl["creator"]["nickname"] | "");
  info.trackCount = pl["trackCount"] | 0;
  info.subscribed = pl["subscribed"] | false;
  info.specialType = pl["specialType"] | 0;

  std::vector<int64_t> ids;
  ids.reserve(100);
  for (JsonObject t : pl["trackIds"].as<JsonArray>()) {
    ids.push_back(t["id"].as<int64_t>());
    if (ids.size() >= 100) break;
  }
  if (ids.empty()) {
    err = "歌单为空";
    return false;
  }

  // Fetch details in small batches to keep peak heap usage bounded.
  constexpr size_t kBatch = 25;
  for (size_t i = 0; i < ids.size(); i += kBatch) {
    std::vector<int64_t> batch(ids.begin() + i,
                               ids.begin() + std::min(ids.size(), i + kBatch));
    String e;
    if (!songDetail(batch, tracks, e)) {
      if (tracks.empty()) {
        err = e;
        return false;
      }
      LOGW(kTag, "song detail batch failed: %s", e.c_str());
    }
    delay(10);
  }
  return !tracks.empty();
}

bool NetEaseApi::userPlaylists(int64_t uid, std::vector<PlaylistInfo>& out, String& err) {
  out.clear();
  StaticJsonDocument<512> d;
  d["uid"] = uid;
  d["limit"] = 50;
  d["offset"] = 0;
  d["includeVideo"] = false;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/user/playlist", json, body, err)) return false;
  if (body.length() > 220 * 1024) {
    err = "歌单列表过大";
    return false;
  }

  DynamicJsonDocument filter(1024);
  filter["code"] = true;
  JsonObject p = filter["playlist"][0].to<JsonObject>();
  p["id"] = true;
  p["name"] = true;
  p["trackCount"] = true;
  p["subscribed"] = true;
  p["specialType"] = true;
  p["creator"]["nickname"] = true;

  DynamicJsonDocument doc(24576);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "解析失败";
    return false;
  }
  if (jsonCode(doc) != 200) {
    err = String("code ") + jsonCode(doc);
    return false;
  }
  for (JsonObject pl : doc["playlist"].as<JsonArray>()) {
    PlaylistInfo info;
    info.id = pl["id"].as<int64_t>();
    info.name = String(pl["name"] | "");
    info.creator = String(pl["creator"]["nickname"] | "");
    info.trackCount = pl["trackCount"] | 0;
    info.subscribed = pl["subscribed"] | false;
    info.specialType = pl["specialType"] | 0;
    if (info.id) out.push_back(info);
  }
  return true;
}

bool NetEaseApi::likedSongs(int64_t uid, std::vector<Track>& out, String& err) {
  out.clear();
  std::vector<PlaylistInfo> lists;
  if (!userPlaylists(uid, lists, err)) return false;
  int64_t likedId = 0;
  for (const auto& p : lists) {
    if (p.specialType == 5) {
      likedId = p.id;
      break;
    }
  }
  if (!likedId) {
    err = "未找到“我喜欢的音乐”";
    return false;
  }
  PlaylistInfo info;
  return playlistDetail(likedId, info, out, err);
}

bool NetEaseApi::likeSong(int64_t id, int64_t uid, bool like, String& err) {
  StaticJsonDocument<512> d;
  d["trackId"] = id;
  d["userid"] = uid;
  d["like"] = like;
  const String csrf = jar_.get("__csrf");
  if (csrf.length()) d["csrf_token"] = csrf;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/song/like", json, body, err)) return false;

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, body)) {
    err = "bad json";
    return false;
  }
  const int code = jsonCode(doc);
  if (code != 200) {
    err = String("code ") + code;
    return false;
  }
  return true;
}

bool NetEaseApi::recommendSongs(std::vector<Track>& out, String& err) {
  out.clear();
  StaticJsonDocument<512> d;
  d["limit"] = 30;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/v3/discovery/recommend/songs", json, body, err)) return false;

  DynamicJsonDocument filter(2048);
  filter["code"] = true;
  addSongFilter(filter["data"]["dailySongs"][0]);
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "解析失败";
    return false;
  }
  if (jsonCode(doc) != 200) {
    err = String("code ") + jsonCode(doc);
    return false;
  }
  for (JsonObject s : doc["data"]["dailySongs"].as<JsonArray>()) {
    out.push_back(parseSong(s));
  }
  return !out.empty();
}

bool NetEaseApi::personalFm(std::vector<Track>& out, String& err) {
  out.clear();
  StaticJsonDocument<512> d;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/v1/radio/get", json, body, err)) return false;

  DynamicJsonDocument filter(2048);
  filter["code"] = true;
  addSongFilter(filter["data"][0]);
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "解析失败";
    return false;
  }
  if (jsonCode(doc) != 200) {
    err = String("code ") + jsonCode(doc);
    return false;
  }
  for (JsonObject s : doc["data"].as<JsonArray>()) {
    out.push_back(parseSong(s));
  }
  return !out.empty();
}

bool NetEaseApi::recentSongs(int64_t uid, std::vector<Track>& out, String& err) {
  out.clear();
  StaticJsonDocument<512> d;
  d["uid"] = uid;
  d["type"] = 1;
  d["header"] = headerJson();
  String json;
  serializeJson(d, json);

  String body;
  if (!eapi("/api/v1/play/record", json, body, err)) return false;

  DynamicJsonDocument filter(2048);
  filter["code"] = true;
  addSongFilter(filter["allData"][0]["song"]);
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    err = "解析失败";
    return false;
  }
  if (jsonCode(doc) != 200) {
    err = String("code ") + jsonCode(doc);
    return false;
  }
  for (JsonObject item : doc["allData"].as<JsonArray>()) {
    JsonObject s = item["song"];
    if (!s.isNull()) out.push_back(parseSong(s));
  }
  return true;
}
