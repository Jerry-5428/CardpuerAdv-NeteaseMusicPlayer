#pragma once

#include <Arduino.h>

#include <vector>

#include "model/Types.h"
#include "net/CookieJar.h"
#include "net/HttpClient.h"

// Thin, allocation-conscious wrapper around the NetEase web/EAPI endpoints.
//
// Protocol notes (verified against the reference project Netease_url and live
// requests):
//  * search / lyric / playlist detail / song detail are plain form POSTs on
//    music.163.com.
//  * login + song-url + account + personal endpoints use EAPI on
//    interface3.music.163.com (AES-128-ECB, see Eapi.h).
//  * every call has a timeout and a response size cap.
class NetEaseApi {
 public:
  explicit NetEaseApi(const CookieJar& jar) : jar_(jar) {}

  // ---- Authentication ----
  bool qrKey(String& unikey, String& err);
  // Returns the raw status code (801 waiting, 802 scanned, 803 success).
  int qrCheck(const String& unikey, std::vector<String>& setCookies, String& err);
  bool account(AccountInfo& out, String& err);

  // ---- Discovery / library ----
  bool search(const String& keyword, int type, int limit, int offset, SearchResult& out,
              String& err);
  bool songUrl(int64_t id, AudioQuality quality, String& url, String& type, int& br,
               int64_t& size, String& err);
  bool lyric(int64_t id, String& raw, String& err);
  bool songDetail(const std::vector<int64_t>& ids, std::vector<Track>& out, String& err);
  bool playlistDetail(int64_t id, PlaylistInfo& info, std::vector<Track>& tracks,
                      String& err);
  bool userPlaylists(int64_t uid, std::vector<PlaylistInfo>& out, String& err);
  bool likedSongs(int64_t uid, std::vector<Track>& out, String& err);
  bool likeSong(int64_t id, int64_t uid, bool like, String& err);
  bool recommendSongs(std::vector<Track>& out, String& err);
  bool personalFm(std::vector<Track>& out, String& err);
  bool recentSongs(int64_t uid, std::vector<Track>& out, String& err);

 private:
  std::vector<HttpHeader> baseHeaders(bool withCookie) const;
  String eapiUrl(const String& apiPath) const;
  bool eapi(const String& apiPath, const String& json, String& body, String& err,
            bool collectCookies = false, std::vector<String>* setCookies = nullptr);
  bool plainForm(const String& apiPath, const String& form, String& body, String& err);
  String headerJson() const;

  const CookieJar& jar_;
};
