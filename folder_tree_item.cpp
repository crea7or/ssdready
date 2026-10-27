// clang-format off
#include "pch.h"
// clang-format on
#include "folder_tree_item.h"

#include "file_tree_item.h"
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
  if (!contents_)
    return nullptr;
  if (!contents_->index && contents_->children.size() < kIndexedChildCount) {
    for (const auto& child : contents_->children)
      if (child->CompareAlias(name))
        return child.get();
    return nullptr;
  }
  if (!contents_->index) {
    auto index = std::make_unique<ChildIndex>();
    index->reserve(contents_->children.size());
    for (const auto& child : contents_->children)
      index->emplace(NameKey(child->GetAlias()), child.get());
    contents_->index = std::move(index);
  }
  const auto found = contents_->index->find(NameKey(name));
  return found == contents_->index->end() ? nullptr : found->second;
}

TreeItem* FolderTreeItem::GetItemByPath(CStringArray* parts, int index, BOOL is_file) {
  if (index >= parts->GetSize())
    return this;
  const bool was_file = GetObjectType() == TreeItemType::kFile;
  auto* child = FindChild(parts->GetAt(index));
  if (!child)
    child = AddNewItem(parts->GetAt(index), is_file && index + 1 == parts->GetSize());
  if (was_file && GetObjectType() == TreeItemType::kFolder)
    Core()->GetManager()->RemoveFileCount();
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
  if (!contents_)
    contents_ = std::make_unique<Contents>();
  contents_->children.push_back(std::move(child));
  // Roll back ownership if updating the existing index cannot allocate.
  if (contents_->index) {
    try {
      contents_->index->emplace(NameKey(name), result);
    } catch (...) {
      contents_->children.pop_back();
      throw;
    }
  }
  if (is_file)
    Core()->GetManager()->AddFileCount();
  if (auto* control = Core()->GetTreeControl()) {
    MarkTreeChildrenChanged();
    control->NotifyItemsChanged();
  }
  return result;
}

void FolderTreeItem::Clear() {
  contents_.reset();
  TreeItem::Clear();
}

void FolderTreeItem::TakeContents(FolderTreeItem&& source) {
  const auto handle = GetTreeItem();
  *this = std::move(source);
  SetTreeItem(handle);
  for (const auto& child : GetChildren())
    child->SetParent(this);
}

int FolderTreeItem::GetImage() const {
  return parent_ && parent_->GetTreeItem() == TVI_ROOT ? 0 : 1;
}

const TreeItem::Children& FolderTreeItem::GetChildren() const {
  static const Children empty;
  return contents_ ? contents_->children : empty;
}

void FolderTreeItem::ReplaceChildren(Children children) {
  if (children.empty()) {
    contents_.reset();
  } else {
    auto contents = std::make_unique<Contents>();
    contents->children = std::move(children);
    contents_ = std::move(contents);
  }
}
