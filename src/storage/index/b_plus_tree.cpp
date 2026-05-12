#include "onebase/storage/index/b_plus_tree.h"
#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *bpm, const KeyComparator &comparator,
                           int leaf_max_size, int internal_max_size)
    : Index(std::move(name)), bpm_(bpm), comparator_(comparator),
      leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {
  if (leaf_max_size_ == 0) {
    leaf_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage) - sizeof(page_id_t)) /
        (sizeof(KeyType) + sizeof(ValueType)));
  }
  if (internal_max_size_ == 0) {
    internal_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage)) /
        (sizeof(KeyType) + sizeof(page_id_t)));
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  return root_page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // TODO(student): Insert a key-value pair into the B+ tree
  // 1. If tree is empty, create a new leaf root
  // 2. Find the leaf page for key
  // 3. Insert into leaf; if overflow, split and propagate up
  auto find_leaf_page_id = [&](const KeyType &search_key, std::vector<page_id_t> *ancestor_page_ids) -> page_id_t {
    page_id_t page_id = root_page_id_;
    while (page_id != INVALID_PAGE_ID) {
      Page *page = bpm_->FetchPage(page_id);
      auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
      if (tree_page->IsLeafPage()) {
        bpm_->UnpinPage(page_id, false);
        return page_id;
      }

      auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
      if (ancestor_page_ids != nullptr) {
        ancestor_page_ids->push_back(page_id);
      }
      page_id_t child_page_id = internal_page->Lookup(search_key, comparator_);
      bpm_->UnpinPage(page_id, false);
      page_id = child_page_id;
    }
    return INVALID_PAGE_ID;
  };

  auto set_page_parent = [&](page_id_t page_id, page_id_t parent_page_id) {
    Page *page = bpm_->FetchPage(page_id);
    if (page == nullptr) {
      return;
    }
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    tree_page->SetParentPageId(parent_page_id);
    bpm_->UnpinPage(page_id, true);
  };

  auto update_internal_children_parent = [&](page_id_t internal_page_id, InternalPage *internal_page) {
    for (int i = 0; i < internal_page->GetSize(); ++i) {
      set_page_parent(internal_page->ValueAt(i), internal_page_id);
    }
  };

  if (IsEmpty()) {
    page_id_t root_page_id;
    Page *root_page = bpm_->NewPage(&root_page_id);
    if (root_page == nullptr) {
      return false;
    }
    auto *root_leaf = reinterpret_cast<LeafPage *>(root_page->GetData());
    root_leaf->Init(leaf_max_size_);
    root_leaf->SetParentPageId(INVALID_PAGE_ID);
    root_leaf->Insert(key, value, comparator_);
    root_page_id_ = root_page_id;
    bpm_->UnpinPage(root_page_id, true);
    return true;
  }

  std::vector<page_id_t> ancestor_page_ids;
  page_id_t leaf_page_id = find_leaf_page_id(key, &ancestor_page_ids);
  Page *leaf_page_raw = bpm_->FetchPage(leaf_page_id);
  if (leaf_page_raw == nullptr) {
    return false;
  }
  auto *leaf_page = reinterpret_cast<LeafPage *>(leaf_page_raw->GetData());
  ValueType existing_value;
  if (leaf_page->Lookup(key, &existing_value, comparator_)) {
    bpm_->UnpinPage(leaf_page_id, false);
    return false;
  }

  leaf_page->Insert(key, value, comparator_);
  if (leaf_page->GetSize() <= leaf_page->GetMaxSize()) {
    bpm_->UnpinPage(leaf_page_id, true);
    return true;
  }

  page_id_t new_leaf_page_id;
  Page *new_leaf_page_raw = bpm_->NewPage(&new_leaf_page_id);
  if (new_leaf_page_raw == nullptr) {
    leaf_page->RemoveAndDeleteRecord(key, comparator_);
    bpm_->UnpinPage(leaf_page_id, true);
    return false;
  }
  auto *new_leaf_page = reinterpret_cast<LeafPage *>(new_leaf_page_raw->GetData());
  new_leaf_page->Init(leaf_max_size_);
  new_leaf_page->SetParentPageId(leaf_page->GetParentPageId());
  new_leaf_page->SetNextPageId(leaf_page->GetNextPageId());
  leaf_page->SetNextPageId(new_leaf_page_id);
  leaf_page->MoveHalfTo(new_leaf_page);

  KeyType middle_key = new_leaf_page->KeyAt(0);
  page_id_t left_page_id = leaf_page_id;
  page_id_t right_page_id = new_leaf_page_id;
  bpm_->UnpinPage(leaf_page_id, true);
  bpm_->UnpinPage(new_leaf_page_id, true);

  while (!ancestor_page_ids.empty()) {
    page_id_t parent_page_id = ancestor_page_ids.back();
    ancestor_page_ids.pop_back();

    Page *parent_page_raw = bpm_->FetchPage(parent_page_id);
    if (parent_page_raw == nullptr) {
      return false;
    }
    auto *parent_page = reinterpret_cast<InternalPage *>(parent_page_raw->GetData());
    const bool needs_split = parent_page->GetSize() + 1 > parent_page->GetMaxSize();
    page_id_t new_internal_page_id = INVALID_PAGE_ID;
    Page *new_internal_page_raw = nullptr;
    if (needs_split) {
      new_internal_page_raw = bpm_->NewPage(&new_internal_page_id);
      if (new_internal_page_raw == nullptr) {
        bpm_->UnpinPage(parent_page_id, false);
        return false;
      }
    }

    parent_page->InsertNodeAfter(left_page_id, middle_key, right_page_id);
    set_page_parent(right_page_id, parent_page_id);

    if (parent_page->GetSize() <= parent_page->GetMaxSize()) {
      bpm_->UnpinPage(parent_page_id, true);
      return true;
    }

    auto *new_internal_page = reinterpret_cast<InternalPage *>(new_internal_page_raw->GetData());
    new_internal_page->Init(internal_max_size_);
    new_internal_page->SetParentPageId(parent_page->GetParentPageId());

    int middle_index = parent_page->GetSize() / 2;
    middle_key = parent_page->KeyAt(middle_index);
    parent_page->MoveHalfTo(new_internal_page, middle_key);
    update_internal_children_parent(new_internal_page_id, new_internal_page);

    left_page_id = parent_page_id;
    right_page_id = new_internal_page_id;
    bpm_->UnpinPage(parent_page_id, true);
    bpm_->UnpinPage(new_internal_page_id, true);
  }

  page_id_t new_root_page_id;
  Page *new_root_page_raw = bpm_->NewPage(&new_root_page_id);
  if (new_root_page_raw == nullptr) {
    return false;
  }
  auto *new_root_page = reinterpret_cast<InternalPage *>(new_root_page_raw->GetData());
  new_root_page->Init(internal_max_size_);
  new_root_page->SetParentPageId(INVALID_PAGE_ID);
  new_root_page->PopulateNewRoot(left_page_id, middle_key, right_page_id);
  root_page_id_ = new_root_page_id;
  set_page_parent(left_page_id, new_root_page_id);
  set_page_parent(right_page_id, new_root_page_id);
  bpm_->UnpinPage(new_root_page_id, true);
  return true;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // TODO(student): Remove a key from the B+ tree
  // 1. Find the leaf page containing key
  // 2. Remove from leaf; if underflow, merge or redistribute
  auto find_leaf_page_id = [&](const KeyType &search_key, std::vector<page_id_t> *ancestor_page_ids) -> page_id_t {
    page_id_t page_id = root_page_id_;
    while (page_id != INVALID_PAGE_ID) {
      Page *page = bpm_->FetchPage(page_id);
      auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
      if (tree_page->IsLeafPage()) {
        bpm_->UnpinPage(page_id, false);
        return page_id;
      }

      auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
      if (ancestor_page_ids != nullptr) {
        ancestor_page_ids->push_back(page_id);
      }
      page_id_t child_page_id = internal_page->Lookup(search_key, comparator_);
      bpm_->UnpinPage(page_id, false);
      page_id = child_page_id;
    }
    return INVALID_PAGE_ID;
  };

  auto set_page_parent = [&](page_id_t page_id, page_id_t parent_page_id) {
    Page *page = bpm_->FetchPage(page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    tree_page->SetParentPageId(parent_page_id);
    bpm_->UnpinPage(page_id, true);
  };

  auto update_internal_children_parent = [&](page_id_t internal_page_id, InternalPage *internal_page) {
    for (int i = 0; i < internal_page->GetSize(); ++i) {
      set_page_parent(internal_page->ValueAt(i), internal_page_id);
    }
  };

  std::function<KeyType(page_id_t)> get_min_key = [&](page_id_t page_id) -> KeyType {
    Page *page = bpm_->FetchPage(page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
      KeyType min_key = leaf_page->KeyAt(0);
      bpm_->UnpinPage(page_id, false);
      return min_key;
    }

    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->ValueAt(0);
    bpm_->UnpinPage(page_id, false);
    return get_min_key(child_page_id);
  };

  auto refresh_page_keys = [&](page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) {
      return;
    }

    Page *page = bpm_->FetchPage(page_id);
    if (page == nullptr) {
      return;
    }
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      bpm_->UnpinPage(page_id, false);
      return;
    }

    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    for (int i = 1; i < internal_page->GetSize(); ++i) {
      internal_page->SetKeyAt(i, get_min_key(internal_page->ValueAt(i)));
    }
    bpm_->UnpinPage(page_id, true);
  };

  auto refresh_ancestors = [&](const std::vector<page_id_t> &page_ids) {
    for (auto it = page_ids.rbegin(); it != page_ids.rend(); ++it) {
      refresh_page_keys(*it);
    }
  };

  if (IsEmpty()) {
    return;
  }

  std::vector<page_id_t> ancestor_page_ids;
  page_id_t leaf_page_id = find_leaf_page_id(key, &ancestor_page_ids);
  if (leaf_page_id == INVALID_PAGE_ID) {
    return;
  }

  Page *leaf_page_raw = bpm_->FetchPage(leaf_page_id);
  auto *leaf_page = reinterpret_cast<LeafPage *>(leaf_page_raw->GetData());
  int old_size = leaf_page->GetSize();
  leaf_page->RemoveAndDeleteRecord(key, comparator_);
  if (leaf_page->GetSize() == old_size) {
    bpm_->UnpinPage(leaf_page_id, false);
    return;
  }

  if (leaf_page_id == root_page_id_) {
    if (leaf_page->GetSize() == 0) {
      root_page_id_ = INVALID_PAGE_ID;
      bpm_->UnpinPage(leaf_page_id, true);
      bpm_->DeletePage(leaf_page_id);
      return;
    }
    bpm_->UnpinPage(leaf_page_id, true);
    return;
  }

  if (leaf_page->GetSize() >= leaf_page->GetMinSize()) {
    bpm_->UnpinPage(leaf_page_id, true);
    refresh_ancestors(ancestor_page_ids);
    return;
  }
  bpm_->UnpinPage(leaf_page_id, true);

  page_id_t current_page_id = leaf_page_id;
  while (current_page_id != INVALID_PAGE_ID) {
    Page *current_page_raw = bpm_->FetchPage(current_page_id);
    auto *current_tree_page = reinterpret_cast<BPlusTreePage *>(current_page_raw->GetData());

    if (current_page_id == root_page_id_) {
      if (current_tree_page->IsLeafPage()) {
        auto *root_leaf = reinterpret_cast<LeafPage *>(current_page_raw->GetData());
        if (root_leaf->GetSize() == 0) {
          root_page_id_ = INVALID_PAGE_ID;
          bpm_->UnpinPage(current_page_id, true);
          bpm_->DeletePage(current_page_id);
          return;
        }
        bpm_->UnpinPage(current_page_id, true);
        refresh_page_keys(root_page_id_);
        return;
      }

      auto *root_internal = reinterpret_cast<InternalPage *>(current_page_raw->GetData());
      if (root_internal->GetSize() == 1) {
        page_id_t only_child = root_internal->RemoveAndReturnOnlyChild();
        root_page_id_ = only_child;
        set_page_parent(only_child, INVALID_PAGE_ID);
        bpm_->UnpinPage(current_page_id, true);
        bpm_->DeletePage(current_page_id);
        refresh_page_keys(root_page_id_);
        return;
      }
      bpm_->UnpinPage(current_page_id, true);
      refresh_page_keys(root_page_id_);
      return;
    }

    if (current_tree_page->GetSize() >= current_tree_page->GetMinSize()) {
      bpm_->UnpinPage(current_page_id, true);
      auto affected_page_ids = ancestor_page_ids;
      affected_page_ids.push_back(current_page_id);
      refresh_ancestors(affected_page_ids);
      return;
    }

    page_id_t parent_page_id = ancestor_page_ids.back();
    Page *parent_page_raw = bpm_->FetchPage(parent_page_id);
    auto *parent_page = reinterpret_cast<InternalPage *>(parent_page_raw->GetData());
    int current_index = parent_page->ValueIndex(current_page_id);
    bool use_left_sibling = current_index > 0;
    int sibling_index = use_left_sibling ? current_index - 1 : current_index + 1;
    page_id_t sibling_page_id = parent_page->ValueAt(sibling_index);
    Page *sibling_page_raw = bpm_->FetchPage(sibling_page_id);
    auto *sibling_tree_page = reinterpret_cast<BPlusTreePage *>(sibling_page_raw->GetData());

    if (sibling_tree_page->GetSize() > sibling_tree_page->GetMinSize()) {
      if (current_tree_page->IsLeafPage()) {
        auto *current_leaf = reinterpret_cast<LeafPage *>(current_page_raw->GetData());
        auto *sibling_leaf = reinterpret_cast<LeafPage *>(sibling_page_raw->GetData());
        if (use_left_sibling) {
          sibling_leaf->MoveLastToFrontOf(current_leaf);
          parent_page->SetKeyAt(current_index, current_leaf->KeyAt(0));
        } else {
          sibling_leaf->MoveFirstToEndOf(current_leaf);
          parent_page->SetKeyAt(sibling_index, sibling_leaf->KeyAt(0));
        }
      } else {
        auto *current_internal = reinterpret_cast<InternalPage *>(current_page_raw->GetData());
        auto *sibling_internal = reinterpret_cast<InternalPage *>(sibling_page_raw->GetData());
        if (use_left_sibling) {
          KeyType replacement_key = sibling_internal->KeyAt(sibling_internal->GetSize() - 1);
          sibling_internal->MoveLastToFrontOf(current_internal, parent_page->KeyAt(current_index));
          set_page_parent(current_internal->ValueAt(0), current_page_id);
          parent_page->SetKeyAt(current_index, replacement_key);
        } else {
          KeyType replacement_key = sibling_internal->KeyAt(1);
          sibling_internal->MoveFirstToEndOf(current_internal, parent_page->KeyAt(sibling_index));
          set_page_parent(current_internal->ValueAt(current_internal->GetSize() - 1), current_page_id);
          parent_page->SetKeyAt(sibling_index, replacement_key);
        }
      }

      bpm_->UnpinPage(sibling_page_id, true);
      bpm_->UnpinPage(parent_page_id, true);
      bpm_->UnpinPage(current_page_id, true);
      refresh_ancestors(ancestor_page_ids);
      return;
    }

    page_id_t page_to_delete;
    if (current_tree_page->IsLeafPage()) {
      auto *current_leaf = reinterpret_cast<LeafPage *>(current_page_raw->GetData());
      auto *sibling_leaf = reinterpret_cast<LeafPage *>(sibling_page_raw->GetData());
      if (use_left_sibling) {
        current_leaf->MoveAllTo(sibling_leaf);
        parent_page->Remove(current_index);
        page_to_delete = current_page_id;
      } else {
        sibling_leaf->MoveAllTo(current_leaf);
        parent_page->Remove(sibling_index);
        page_to_delete = sibling_page_id;
      }
    } else {
      auto *current_internal = reinterpret_cast<InternalPage *>(current_page_raw->GetData());
      auto *sibling_internal = reinterpret_cast<InternalPage *>(sibling_page_raw->GetData());
      if (use_left_sibling) {
        current_internal->MoveAllTo(sibling_internal, parent_page->KeyAt(current_index));
        update_internal_children_parent(sibling_page_id, sibling_internal);
        parent_page->Remove(current_index);
        page_to_delete = current_page_id;
      } else {
        sibling_internal->MoveAllTo(current_internal, parent_page->KeyAt(sibling_index));
        update_internal_children_parent(current_page_id, current_internal);
        parent_page->Remove(sibling_index);
        page_to_delete = sibling_page_id;
      }
    }

    bpm_->UnpinPage(sibling_page_id, true);
    bpm_->UnpinPage(parent_page_id, true);
    bpm_->UnpinPage(current_page_id, true);
    bpm_->DeletePage(page_to_delete);

    current_page_id = parent_page_id;
    ancestor_page_ids.pop_back();
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  // TODO(student): Search for key and add matching values to result
  page_id_t leaf_page_id = root_page_id_;
  while (leaf_page_id != INVALID_PAGE_ID) {
    Page *page = bpm_->FetchPage(leaf_page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      bpm_->UnpinPage(leaf_page_id, false);
      break;
    }

    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->Lookup(key, comparator_);
    bpm_->UnpinPage(leaf_page_id, false);
    leaf_page_id = child_page_id;
  }
  if (leaf_page_id == INVALID_PAGE_ID) {
    return false;
  }

  Page *page = bpm_->FetchPage(leaf_page_id);
  auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  ValueType value;
  bool found = leaf_page->Lookup(key, &value, comparator_);
  if (found) {
    result->push_back(value);
  }
  bpm_->UnpinPage(leaf_page_id, false);
  return found;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin() -> Iterator {
  // TODO(student): Return an iterator pointing to the first key
  if (IsEmpty()) {
    return End();
  }

  page_id_t page_id = root_page_id_;
  while (page_id != INVALID_PAGE_ID) {
    Page *page = bpm_->FetchPage(page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
      bool is_empty = leaf_page->GetSize() == 0;
      bpm_->UnpinPage(page_id, false);
      return is_empty ? End() : Iterator(page_id, 0, bpm_);
    }

    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->ValueAt(0);
    bpm_->UnpinPage(page_id, false);
    page_id = child_page_id;
  }
  return End();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> Iterator {
  // TODO(student): Return an iterator pointing to the given key
  page_id_t leaf_page_id = root_page_id_;
  while (leaf_page_id != INVALID_PAGE_ID) {
    Page *page = bpm_->FetchPage(leaf_page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      bpm_->UnpinPage(leaf_page_id, false);
      break;
    }

    auto *internal_page = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child_page_id = internal_page->Lookup(key, comparator_);
    bpm_->UnpinPage(leaf_page_id, false);
    leaf_page_id = child_page_id;
  }
  if (leaf_page_id == INVALID_PAGE_ID) {
    return End();
  }

  Page *page = bpm_->FetchPage(leaf_page_id);
  auto *leaf_page = reinterpret_cast<LeafPage *>(page->GetData());
  int index = leaf_page->KeyIndex(key, comparator_);
  if (index < leaf_page->GetSize()) {
    bpm_->UnpinPage(leaf_page_id, false);
    return Iterator(leaf_page_id, index, bpm_);
  }

  page_id_t next_page_id = leaf_page->GetNextPageId();
  bpm_->UnpinPage(leaf_page_id, false);
  return next_page_id == INVALID_PAGE_ID ? End() : Iterator(next_page_id, 0, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::End() -> Iterator {
  return Iterator(INVALID_PAGE_ID, 0);
}

template class BPlusTree<int, RID, std::less<int>>;

}  // namespace onebase
