// clang-format off
#include "pch.h"
// clang-format on
#include "folder_tree_item.h"

#include "monitoring_tree_ctrl.h"

namespace {
constexpr std::size_t kIndexedChildCount = 16;
}

std::wstring FolderTreeItem::NameKey(LPCTSTR name) {
  std::wstring key(name);
  _wcslwr_s(key.data(), key.size() + 1);
  return key;
}

TreeItem* FolderTreeItem::FindChild(LPCTSTR name) {
  if (!children_by_name_ && children_.size() < kIndexedChildCount) {
    for (const auto& child : children_)
      if (child->CompareAlias(name))
        return child.get();
    return nullptr;
  }
  if (!children_by_name_) {
    auto index = std::make_unique<ChildIndex>();
    index->reserve(children_.size());
    for (const auto& child : children_)
      index->emplace(NameKey(child->GetAlias()), child.get());
    children_by_name_ = std::move(index);
  }
  const auto found = children_by_name_->find(NameKey(name));
  return found == children_by_name_->end() ? nullptr : found->second;
}

TreeItem* FolderTreeItem::GetItemByPath(CStringArray* parts, int index, BOOL is_file) {
  if (index >= parts->GetSize())
    return this;
  auto* child = FindChild(parts->GetAt(index));
  if (!child)
    child = AddNewItem(parts->GetAt(index), is_file && index + 1 == parts->GetSize());
  return child->GetItemByPath(parts, index + 1, is_file);
}

TreeItem* FolderTreeItem::AddNewItem(LPCTSTR name, BOOL is_file) {
  std::unique_ptr<TreeItem> child;
  if (is_file)
    child = std::make_unique<FileTreeItem>();
  else
    child = std::make_unique<FolderTreeItem>();
  child->SetAlias(name);
  child->SetParent(this);
  auto* result = child.get();
  children_.push_back(std::move(child));
  // Roll back ownership if updating the existing index cannot allocate.
  if (children_by_name_) {
    try {
      children_by_name_->emplace(NameKey(name), result);
    } catch (...) {
      children_.pop_back();
      throw;
    }
  }
  Core()->GetManager()->AddFileCount();
  if (auto* control = Core()->GetTreeControl()) {
    MarkTreeChildrenChanged();
    control->NotifyItemsChanged();
  }
  return result;
}

void FolderTreeItem::Clear() {
  children_by_name_.reset();
  children_.clear();
  TreeItem::Clear();
}

void FolderTreeItem::TakeContents(FolderTreeItem&& source) {
  const auto handle = GetTreeItem();
  *this = std::move(source);
  SetTreeItem(handle);
  for (const auto& child : children_)
    child->SetParent(this);
}

int FolderTreeItem::GetImage() const {
  return parent_ && parent_->GetTreeItem() == TVI_ROOT ? 0 : 1;
}
