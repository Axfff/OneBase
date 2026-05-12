#include "onebase/buffer/lru_k_replacer.h"
#include "onebase/common/exception.h"

namespace onebase {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k)
    : max_frames_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  // Find the frame with the largest backward k-distance.
  // Among frames with fewer than k accesses, evict the one with earliest first access.
  // Only consider evictable frames.
  bool found = false;
  frame_id_t victim = INVALID_FRAME_ID;
  bool victim_has_inf_distance = false;
  size_t victim_time = 0;

  for (const auto &[candidate_id, entry] : entries_) {
    if (!entry.is_evictable_) {
      continue;
    }

    const bool has_inf_distance = entry.history_.size() < k_;
    const size_t candidate_time = entry.history_.front();

    if (!found) {
      found = true;
      victim = candidate_id;
      victim_has_inf_distance = has_inf_distance;
      victim_time = candidate_time;
      continue;
    }

    if (has_inf_distance != victim_has_inf_distance) {
      if (has_inf_distance) {
        victim = candidate_id;
        victim_has_inf_distance = true;
        victim_time = candidate_time;
      }
      continue;
    }

    if (candidate_time < victim_time ||
        (candidate_time == victim_time && candidate_id < victim)) {
      victim = candidate_id;
      victim_time = candidate_time;
    }
  }

  if (!found) {
    return false;
  }

  *frame_id = victim;
  entries_.erase(victim);
  curr_size_--;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::lock_guard<std::mutex> guard(latch_);

  // If frame_id is new, create an entry.
  // Add current_timestamp_ to the frame's history.
  // Increment current_timestamp_.
  auto &entry = entries_[frame_id];
  entry.history_.push_back(current_timestamp_++);
  while (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> guard(latch_);

  // Set whether a frame is evictable and update curr_size_ accordingly.
  if (entries_.find(frame_id) == entries_.end()) {
    return;
  }
  if (entries_[frame_id].is_evictable_ == set_evictable) {
    return;
  }
  entries_[frame_id].is_evictable_ = set_evictable;
  if (set_evictable) {
    curr_size_++;
  } else {
    curr_size_--;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> guard(latch_);

  // Remove a frame from the replacer.
  // The frame must be evictable; throw if not.
  if (entries_.find(frame_id) == entries_.end()) {
    return;
  }
  if (!entries_[frame_id].is_evictable_) {
    throw OneBaseException("Frame is not evictable");
  }
  entries_.erase(frame_id);
  curr_size_--;
}

auto LRUKReplacer::Size() const -> size_t {
  std::lock_guard<std::mutex> guard(latch_);
  // Return the number of evictable frames.
  return curr_size_;
}

}  // namespace onebase
