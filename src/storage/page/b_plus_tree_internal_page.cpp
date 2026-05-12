#include "onebase/storage/page/b_plus_tree_internal_page.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  array_[index].first = key;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) {
  array_[index].second = value;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  // TODO(student): Find the index of the given value in the internal page
  for (int i = 0; i < GetSize(); ++i) {
    if (array_[i].second == value) {
      return i;
    }
  }
  return -1;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  // TODO(student): Find the child page that should contain the given key
  int left = 1;
  int right = GetSize();
  while (left < right) {
    int mid = left + (right - left) / 2;
    if (comparator(key, array_[mid].first)) {
      right = mid;
    } else {
      left = mid + 1;
    }
  }
  return array_[left - 1].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::PopulateNewRoot(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) {
  // TODO(student): Create a new root with one key and two children
  array_[0].second = old_value;
  array_[1] = {key, new_value};
  SetSize(2);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) -> int {
  // TODO(student): Insert a new key-value pair after old_value
  int index = ValueIndex(old_value);
  if (index == -1) {
    return GetSize();
  }
  int insert_index = index + 1;
  for (int i = GetSize(); i > insert_index; --i) {
    array_[i] = array_[i - 1];
  }
  array_[insert_index] = {key, new_value};
  IncreaseSize(1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
  // TODO(student): Remove the key-value pair at the given index
  for (int i = index; i + 1 < GetSize(); ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAndReturnOnlyChild() -> ValueType {
  // TODO(student): Remove all entries and return the only remaining child
  ValueType only_child = array_[0].second;
  SetSize(0);
  return only_child;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // TODO(student): Move all entries to recipient during merge
  int recipient_size = recipient->GetSize();
  recipient->array_[recipient_size] = {middle_key, array_[0].second};
  for (int i = 1; i < GetSize(); ++i) {
    recipient->array_[recipient_size + i] = array_[i];
  }
  recipient->IncreaseSize(GetSize());
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // TODO(student): Move the second half of entries to recipient during split
  (void)middle_key;
  int middle_index = GetSize() / 2;
  int recipient_size = GetSize() - middle_index;
  recipient->array_[0].second = array_[middle_index].second;
  for (int i = 1; i < recipient_size; ++i) {
    recipient->array_[i] = array_[middle_index + i];
  }
  recipient->SetSize(recipient_size);
  SetSize(middle_index);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // TODO(student): Move first entry to end of recipient (redistribute)
  recipient->array_[recipient->GetSize()] = {middle_key, array_[0].second};
  recipient->IncreaseSize(1);
  for (int i = 0; i + 1 < GetSize(); ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // TODO(student): Move last entry to front of recipient (redistribute)
  for (int i = recipient->GetSize(); i > 0; --i) {
    recipient->array_[i] = recipient->array_[i - 1];
  }
  recipient->array_[0].second = array_[GetSize() - 1].second;
  recipient->array_[1].first = middle_key;
  recipient->IncreaseSize(1);
  IncreaseSize(-1);
}

template class BPlusTreeInternalPage<int, page_id_t, std::less<int>>;

}  // namespace onebase
