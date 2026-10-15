#pragma once

#include "tree_item.h"

class FileTreeItem : public TreeItem {
 public:

  TreeItemType GetObjectType() const override {
    return TreeItemType::kFile;
  }

  int GetImage() const override {
    return 2;
  }

  TreeItem* GetItemByPath(CStringArray*, int, BOOL) override {
    return this;
  }
};
