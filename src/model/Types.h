#pragma once

#include <Arduino.h>

#include <vector>

struct Track {
  int64_t id = 0;
  String name;
  String artists;
  String album;
  uint32_t durationMs = 0;
  bool liked = false;
  bool playable = true;

  bool valid() const { return id > 0; }
};

struct ArtistInfo {
  int64_t id = 0;
  String name;
  int albumCount = 0;
  int songCount = 0;
};

struct AlbumInfo {
  int64_t id = 0;
  String name;
  String artist;
  int songCount = 0;
};

struct PlaylistInfo {
  int64_t id = 0;
  String name;
  String creator;
  int trackCount = 0;
  bool subscribed = false;
  int specialType = 0;  // 5 = "我喜欢的音乐"
};

struct SearchResult {
  String keyword;
  std::vector<Track> songs;
  std::vector<ArtistInfo> artists;
  std::vector<AlbumInfo> albums;
  std::vector<PlaylistInfo> playlists;
  bool empty() const {
    return songs.empty() && artists.empty() && albums.empty() && playlists.empty();
  }
};

struct LyricLine {
  uint32_t timeMs = 0;
  String text;
};

struct AccountInfo {
  bool valid = false;
  int64_t uid = 0;
  String nickname;
};

enum class PlayMode : uint8_t { Sequential = 0, LoopAll = 1, RepeatOne = 2, Shuffle = 3 };

enum class AudioQuality : uint8_t { Standard = 0, Higher = 1, ExHigh = 2 };

inline const char* qualityLevel(AudioQuality q) {
  switch (q) {
    case AudioQuality::Higher:
      return "higher";
    case AudioQuality::ExHigh:
      return "exhigh";
    default:
      return "standard";
  }
}
