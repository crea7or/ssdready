#pragma once

#include <string>
#include <unordered_map>

#include "tree_item.h"

class FolderTreeItem : public TreeItem {
 public:

  TreeItemType GetObjectType() const override {
    return TreeItemType::kFolder;
  }

  int GetImage() const override;
  TreeItem* GetItemByPath(CStringArray* parts, int index, BOOL is_file) override;
  void Clear() override;
  // Commit a completely loaded tree; retain the root's UI handle and repair
  // immediate parent pointers after transferring the owned children.
  void TakeContents(FolderTreeItem&& source);

 private:

  const Children& GetChildren() const override;
  void ReplaceChildren(Children children) override;
  using ChildIndex = std::unordered_map<std::wstring, TreeItem*>;

  struct Contents {
    Children children;
    std::unique_ptr<ChildIndex> index;
  };

  // Leaves pay for one pointer, not an empty vector and hash table.
  std::unique_ptr<Contents> contents_;
  static std::wstring NameKey(LPCTSTR name);
  TreeItem* FindChild(LPCTSTR name);
  TreeItem* AddNewItem(LPCTSTR name, BOOL is_file);
  // Small folders use contiguous scans; large folders build their index lazily.
};
