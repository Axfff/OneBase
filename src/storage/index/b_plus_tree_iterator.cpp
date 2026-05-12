#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/storage/page/b_plus_tree_leaf_page.h"

namespace onebase {

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_ITERATOR_TYPE::BPlusTreeIterator(page_id_t page_id, int index, BufferPoolManager *bpm)
    : page_id_(page_id), index_(index), bpm_(bpm) {}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::IsEnd() const -> bool {
  return page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator*() -> const std::pair<KeyType, ValueType> & {
  // TODO(student): Dereference the iterator
  Page *page = bpm_->FetchPage(page_id_);
  auto *leaf_page = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(page->GetData());
  current_ = {leaf_page->KeyAt(index_), leaf_page->ValueAt(index_)};
  bpm_->UnpinPage(page_id_, false);
  return current_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator++() -> BPlusTreeIterator & {
  // TODO(student): Advance the iterator to the next key-value pair
  if (IsEnd()) {
    return *this;
  }

  Page *page = bpm_->FetchPage(page_id_);
  auto *leaf_page = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(page->GetData());
  if (index_ + 1 < leaf_page->GetSize()) {
    ++index_;
    bpm_->UnpinPage(page_id_, false);
    return *this;
  }

  page_id_ = leaf_page->GetNextPageId();
  index_ = 0;
  bpm_->UnpinPage(page->GetPageId(), false);
  return *this;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator==(const BPlusTreeIterator &other) const -> bool {
  return page_id_ == other.page_id_ && index_ == other.index_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator!=(const BPlusTreeIterator &other) const -> bool {
  return !(*this == other);
}

template class BPlusTreeIterator<int, RID, std::less<int>>;

}  // namespace onebase
