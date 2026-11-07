#pragma once

#include <Arduino.h>

#include <vector>

#include "model/Types.h"

// Playback queue + play mode + "liked" bookkeeping.
//
// Pure data/state: it never performs I/O, so it is safe to use from the UI
// task.  Network playback is requested through App -> worker.
class PlayerController {
 public:
  void setQueue(const std::vector<Track>& tracks, int startIndex = 0);
  void clear();
  void append(const std::vector<Track>& tracks);

  bool empty() const { return queue_.empty(); }
  size_t size() const { return queue_.size(); }
  int index() const { return index_; }
  const std::vector<Track>& queue() const { return queue_; }
  const Track* current() const;
  Track* currentMutable();

  // Returns -1 when the queue should stop (sequential mode at the end).
  int nextIndex() const;
  int prevIndex() const;
  void setIndex(int i);

  PlayMode mode() const { return mode_; }
  void setMode(PlayMode m) { mode_ = m; }
  void cycleMode();

  bool isLiked(int64_t id) const;
  void setLiked(int64_t id, bool liked);
  void markLikedSet(const std::vector<Track>& liked);

 private:
  int randomIndex(int avoid) const;

  std::vector<Track> queue_;
  int index_ = -1;
  PlayMode mode_ = PlayMode::LoopAll;
  std::vector<int64_t> likedIds_;
};
