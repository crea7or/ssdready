#pragma once

#include "folder_tree_item.h"

class FileTreeItem : public FolderTreeItem {
 public:

  TreeItemType GetObjectType() const override {
    return GetChildrenCount() ? TreeItemType::kFolder : TreeItemType::kFile;
  }

  int GetImage() const override {
    return GetObjectType() == TreeItemType::kFile ? 2 : FolderTreeItem::GetImage();
  }
};
