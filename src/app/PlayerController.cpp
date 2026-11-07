#include "PlayerController.h"

#include <esp_random.h>

#include <algorithm>

void PlayerController::setQueue(const std::vector<Track>& tracks, int startIndex) {
  queue_ = tracks;
  if (queue_.empty()) {
    index_ = -1;
    return;
  }
  index_ = (startIndex < 0) ? 0 : (startIndex % static_cast<int>(queue_.size()));
}

void PlayerController::clear() {
  queue_.clear();
  index_ = -1;
}

void PlayerController::append(const std::vector<Track>& tracks) {
  if (queue_.size() + tracks.size() > 256) return;
  queue_.insert(queue_.end(), tracks.begin(), tracks.end());
  if (index_ < 0) index_ = queue_.empty() ? -1 : 0;
}

const Track* PlayerController::current() const {
  if (index_ < 0 || index_ >= static_cast<int>(queue_.size())) return nullptr;
  return &queue_[index_];
}

Track* PlayerController::currentMutable() {
  if (index_ < 0 || index_ >= static_cast<int>(queue_.size())) return nullptr;
  return &queue_[index_];
}

int PlayerController::randomIndex(int avoid) const {
  const int n = static_cast<int>(queue_.size());
  if (n <= 1) return n ? 0 : -1;
  for (int i = 0; i < 8; ++i) {
    const int r = static_cast<int>(esp_random() % n);
    if (r != avoid) return r;
  }
  return (avoid + 1) % n;
}

int PlayerController::nextIndex() const {
  const int n = static_cast<int>(queue_.size());
  if (n == 0) return -1;
  if (index_ < 0) return 0;
  switch (mode_) {
    case PlayMode::RepeatOne:
      return index_;
    case PlayMode::Shuffle:
      return randomIndex(index_);
    case PlayMode::LoopAll:
      return (index_ + 1) % n;
    case PlayMode::Sequential:
    default:
      return (index_ + 1 < n) ? index_ + 1 : -1;
  }
}

int PlayerController::prevIndex() const {
  const int n = static_cast<int>(queue_.size());
  if (n == 0) return -1;
  if (index_ < 0) return 0;
  switch (mode_) {
    case PlayMode::RepeatOne:
      return index_;
    case PlayMode::Shuffle:
      return randomIndex(index_);
    case PlayMode::LoopAll:
      return (index_ + n - 1) % n;
    case PlayMode::Sequential:
    default:
      return (index_ > 0) ? index_ - 1 : -1;
  }
}

void PlayerController::setIndex(int i) {
  if (i >= 0 && i < static_cast<int>(queue_.size())) index_ = i;
}

void PlayerController::cycleMode() {
  switch (mode_) {
    case PlayMode::Sequential:
      mode_ = PlayMode::LoopAll;
      break;
    case PlayMode::LoopAll:
      mode_ = PlayMode::RepeatOne;
      break;
    case PlayMode::RepeatOne:
      mode_ = PlayMode::Shuffle;
      break;
    default:
      mode_ = PlayMode::Sequential;
      break;
  }
}

bool PlayerController::isLiked(int64_t id) const {
  return std::find(likedIds_.begin(), likedIds_.end(), id) != likedIds_.end();
}

void PlayerController::setLiked(int64_t id, bool liked) {
  auto it = std::find(likedIds_.begin(), likedIds_.end(), id);
  if (liked) {
    if (it == likedIds_.end()) likedIds_.push_back(id);
  } else if (it != likedIds_.end()) {
    likedIds_.erase(it);
  }
  for (Track& t : queue_) {
    if (t.id == id) t.liked = liked;
  }
}

void PlayerController::markLikedSet(const std::vector<Track>& liked) {
  likedIds_.clear();
  likedIds_.reserve(liked.size());
  for (const Track& t : liked) likedIds_.push_back(t.id);
  for (Track& t : queue_) t.liked = isLiked(t.id);
}
