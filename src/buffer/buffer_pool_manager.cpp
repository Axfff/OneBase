#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/common/logger.h"

namespace onebase {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<frame_id_t>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  std::lock_guard<std::mutex> guard(latch_);

  // 1. Pick a victim frame from free list or replacer.
  frame_id_t frame_id;
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    }
  }

  Page *page = &pages_[frame_id];

  // 2. If victim is dirty, write it back to disk and remove its old mapping.
  if (page->page_id_ != INVALID_PAGE_ID) {
    if (page->is_dirty_) {
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
    }
    page_table_.erase(page->GetPageId());
  }

  // 3. Allocate a new page_id via disk_manager_.
  *page_id = disk_manager_->AllocatePage();

  // 4. Update page_table_ and page metadata.
  page->ResetMemory();
  page->page_id_ = *page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;
  page_table_[*page_id] = frame_id;

  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);
  return page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id) -> Page * {
  std::lock_guard<std::mutex> guard(latch_);

  // 1. Search page_table_ for existing mapping.
  auto page_iter = page_table_.find(page_id);
  if (page_iter != page_table_.end()) {
    Page *page = &pages_[page_iter->second];
    page->pin_count_++;
    replacer_->RecordAccess(page_iter->second);
    replacer_->SetEvictable(page_iter->second, false);
    return page;
  }

  // 2. If not found, pick a victim frame.
  frame_id_t frame_id;
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    }
  }

  Page *page = &pages_[frame_id];

  // Write back and remove the previous page if this frame is being reused.
  if (page->page_id_ != INVALID_PAGE_ID) {
    if (page->is_dirty_) {
      disk_manager_->WritePage(page->GetPageId(), page->GetData());
    }
    page_table_.erase(page->GetPageId());
  }

  page->ResetMemory();

  // 3. Read page from disk into the frame.
  disk_manager_->ReadPage(page_id, page->GetData());
  page->page_id_ = page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;
  page_table_[page_id] = frame_id;

  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);
  return page;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  // Unpin a page, decrementing pin count.
  auto page_iter = page_table_.find(page_id);
  if (page_iter == page_table_.end()) {
    return false;
  }

  Page *page = &pages_[page_iter->second];
  if (page->pin_count_ <= 0) {
    return false;
  }

  page->is_dirty_ = page->is_dirty_ || is_dirty;
  page->pin_count_--;

  // If pin_count reaches 0, set evictable in replacer.
  if (page->pin_count_ == 0) {
    replacer_->SetEvictable(page_iter->second, true);
  }
  return true;
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  // Delete a page from the buffer pool.
  auto page_iter = page_table_.find(page_id);
  if (page_iter == page_table_.end()) {
    disk_manager_->DeallocatePage(page_id);
    return true;
  }

  const frame_id_t frame_id = page_iter->second;
  Page *page = &pages_[frame_id];

  // Page must have pin_count == 0.
  if (page->pin_count_ > 0) {
    return false;
  }

  // Remove from page_table_, reset memory, add frame to free_list_.
  replacer_->Remove(frame_id);
  page_table_.erase(page_iter);
  page->ResetMemory();
  page->page_id_ = INVALID_PAGE_ID;
  page->pin_count_ = 0;
  page->is_dirty_ = false;
  free_list_.push_back(frame_id);
  disk_manager_->DeallocatePage(page_id);
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  // Force flush a page to disk regardless of dirty flag.
  auto page_iter = page_table_.find(page_id);
  if (page_iter == page_table_.end()) {
    return false;
  }

  Page *page = &pages_[page_iter->second];
  disk_manager_->WritePage(page_id, page->GetData());
  page->is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard<std::mutex> guard(latch_);

  // Flush all pages in the buffer pool to disk.
  for (auto &[page_id, frame_id] : page_table_) {
    Page *page = &pages_[frame_id];
    disk_manager_->WritePage(page->GetPageId(), page->GetData());
    page->is_dirty_ = false;
  }
}

}  // namespace onebase
