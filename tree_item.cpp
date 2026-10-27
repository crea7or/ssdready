// clang-format off
#include "pch.h"
// clang-format on
#include "tree_item.h"

#include <algorithm>

#include "file_tree_item.h"
#include "folder_tree_item.h"
#include "monitoring_tree_ctrl.h"

const TreeItem::Children& TreeItem::GetChildren() const {
  static const Children empty;
  return empty;
}

std::vector<ProcessWriteStatistics> TreeItem::GetProcessWrites(bool include_children) const {
  if (!include_children && process_links_.size() <= 1)
    return process_links_;
  std::vector<ProcessWriteStatistics> result;
  std::unordered_map<std::size_t, std::size_t> positions;
  CollectProcessWrites(result, positions, include_children);
  return result;
}

void TreeItem::CollectProcessWrites(std::vector<ProcessWriteStatistics>& result,
                                    std::unordered_map<std::size_t, std::size_t>& positions,
                                    bool include_children) const {
  for (const auto& link : process_links_) {
    const auto inserted = positions.emplace(link.process_index, result.size());
    if (inserted.second)
      result.push_back(link);
    else
      result[inserted.first->second].written_bytes += link.written_bytes;
  }
  if (include_children)
    for (const auto& child : GetChildren())
      child->CollectProcessWrites(result, positions, true);
}

void TreeItem::SetProcessWrite(std::size_t process_index, DWORD64 bytes) {
  const auto found = std::find_if(process_links_.begin(), process_links_.end(), [process_index](const ProcessWriteStatistics& link) {
    return link.process_index == process_index;
  });
  if (found != process_links_.end())
    found->written_bytes += bytes;
  else
    process_links_.push_back({bytes, process_index});
}

void TreeItem::AddWrite(DWORD64 bytes, std::optional<std::size_t> process_index) {
  values_.written_bytes += bytes;
  values_.rounded_bytes += ((bytes + 4095) & ~DWORD64(4095));
  if (process_index)
    SetProcessWrite(*process_index, bytes);
  for (auto* node = this; node; node = node->parent_) {
    node->values_.total_with_children += bytes;
    node->description_up_to_date_ = false;
    node->children_changed_ = true;
  }
}

void TreeItem::Clear() {
  process_links_.clear();
  values_ = {};
  description_.Empty();
  description_up_to_date_ = false;
  children_changed_ = false;
}

void TreeItem::GetFullPath(CString* path) const {
  if (parent_)
    parent_->GetFullPath(path);
  *path += alias_;
  if (GetObjectType() == TreeItemType::kFolder)
    string_utils::EnsureTrailingSlash(*path);
}

BOOL TreeItem::GetDisplayInformation(TVITEM* item) {
  if (item->mask & TVIF_IMAGE)
    item->iImage = GetImage();
  if (item->mask & TVIF_SELECTEDIMAGE)
    item->iSelectedImage = GetImage();
  if (item->mask & TVIF_CHILDREN)
    item->cChildren = !GetChildren().empty();
  if (item->mask & TVIF_TEXT) {
    if (!description_up_to_date_)
      UpdateDescription();
    item->pszText = const_cast<LPTSTR>(GetDescription());
  }
  return TRUE;
}

void TreeItem::UpdateDescription() {
  CString size;
  size = string_utils::FormatBytes(values_.total_with_children);
  description_.Format(L"%s ( %s )", GetAlias(), size.GetString());
  description_up_to_date_ = true;
}

BOOL TreeItem::Save(File* file) const {
  auto values = values_;
  if (!NamedObject::Save(file) || !file->Write(&values, sizeof(values)) || !file->WriteSize32(process_links_.size()))
    return FALSE;
  for (const auto& link : process_links_)
    if (!link.Save(file))
      return FALSE;
  if (!file->WriteSize32(GetChildren().size()))
    return FALSE;
  for (const auto& child : GetChildren()) {
    DWORD type = static_cast<DWORD>(child->GetObjectType());
    if (!file->Write(&type, sizeof(type)) || !child->Save(file))
      return FALSE;
  }
  return TRUE;
}

void TreeItem::AddToTreeCtrl() {
  auto* control = Core()->GetTreeControl();
  if (!control)
    return;
  MonitoringTreeCtrl::UpdateBatch batch(*control);
  // Populate only this level. Expansion materializes the next level once.
  for (const auto& child : GetChildren())
    if (!child->GetTreeItem())
      control->AddModelItem(child.get());
}

void TreeItem::MarkTreeChildrenChanged() {
  for (auto* node = this; node; node = node->parent_)
    node->children_changed_ = true;
}

BOOL TreeItem::TakeTreeChildrenChanged() {
  const bool changed = children_changed_;
  children_changed_ = false;
  return changed;
}

std::size_t TreeItem::CountFiles() const {
  std::size_t count = GetObjectType() == TreeItemType::kFile ? 1 : 0;
  for (const auto& child : GetChildren())
    count += child->CountFiles();
  return count;
}

BOOL TreeItem::Load(File* file) {
  NamedObject object;
  ItemValues values;
  std::size_t count;
  if (!object.Load(file) || !file->Read(&values, sizeof(values)) || !file->ReadSize32(count))
    return FALSE;
  std::vector<ProcessWriteStatistics> links;
  links.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    ProcessWriteStatistics link;
    if (!link.Load(file))
      return FALSE;
    links.push_back(link);
  }
  if (!file->ReadSize32(count))
    return FALSE;
  if (GetObjectType() == TreeItemType::kFile && count != 0) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
  Children children;
  children.reserve(count);
  // Rebuild the cached total, including files saved with stale root counters.
  values.total_with_children = values.written_bytes;
  for (std::size_t index = 0; index < count; ++index) {
    DWORD type;
    if (!file->Read(&type, sizeof(type)))
      return FALSE;
    std::unique_ptr<TreeItem> child;
    if (static_cast<TreeItemType>(type) == TreeItemType::kFile)
      child = std::make_unique<FileTreeItem>();
    else if (static_cast<TreeItemType>(type) == TreeItemType::kFolder)
      child = std::make_unique<FolderTreeItem>();
    else {
      SetLastError(ERROR_INVALID_DATA);
      return FALSE;
    }
    if (!child->Load(file))
      return FALSE;
    child->SetParent(this);
    values.total_with_children += child->GetWriteSizeTotal();
    children.push_back(std::move(child));
  }
  // Commit only a complete load; failed reads leave the existing node intact.
  // Allocate folder storage before changing the existing model. Base Clear
  // resets counters/links only; it must not discard the newly installed children.
  ReplaceChildren(std::move(children));
  TreeItem::Clear();
  NamedObject::operator=(object);
  values_ = values;
  process_links_ = std::move(links);
  return TRUE;
}
