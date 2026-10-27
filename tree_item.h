#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "named_object.h"
#include "process_write_statistics.h"

enum class TreeItemType : DWORD {
  kFile = 0,
  kFolder = 1,
};

// Access is serialized by the manager's processed-data mutex. Children own their
// nodes; tree-control handles and folder lookup indexes are non-owning references.
class TreeItem : public NamedObject {
 public:

  void GetFullPath(CString* path) const;
  virtual TreeItem* GetItemByPath(CStringArray* parts, int index, BOOL is_file) = 0;
  virtual TreeItemType GetObjectType() const = 0;
  virtual int GetImage() const = 0;
  BOOL GetDisplayInformation(TVITEM* item);
  virtual void Clear();

  HTREEITEM GetTreeItem() const {
    return tree_item_;
  }

  void SetTreeItem(HTREEITEM item) {
    tree_item_ = item;
  }

  TreeItem* GetParent() const {
    return parent_;
  }

  void SetParent(TreeItem* parent) {
    parent_ = parent;
  }

  void AddWrite(DWORD64 bytes, std::optional<std::size_t> process_index = std::nullopt);

  DWORD64 GetWriteSize() const {
    return values_.written_bytes;
  }

  DWORD64 GetWriteSizeTotal() const {
    return values_.total_with_children;
  }

  BOOL Save(File* file) const override;
  BOOL Load(File* file) override;

  void AddToTreeCtrl();
  BOOL TakeTreeChildrenChanged();

  std::size_t GetChildrenCount() const {
    return GetChildren().size();
  }

  std::size_t CountFiles() const;

  std::vector<ProcessWriteStatistics> GetProcessWrites(bool include_children) const;

 protected:

  void MarkTreeChildrenChanged();
  using Children = std::vector<std::unique_ptr<TreeItem>>;
  TreeItem* parent_ = nullptr;

 private:

  friend class HistoryCodec;

  virtual const Children& GetChildren() const;

  virtual void ReplaceChildren(Children) {
  }

  void UpdateDescription();
  void CollectProcessWrites(std::vector<ProcessWriteStatistics>& result,
                            std::unordered_map<std::size_t, std::size_t>& positions,
                            bool include_children) const;
  void SetProcessWrite(std::size_t process_index, DWORD64 bytes);
  bool description_up_to_date_ = false;
  bool children_changed_ = false;
  HTREEITEM tree_item_ = nullptr;
  std::vector<ProcessWriteStatistics> process_links_;

  // Version 1 stores these three uint64 fields in this order.
  struct ItemValues {
    DWORD64 total_with_children = 0;
    DWORD64 written_bytes = 0;
    DWORD64 rounded_bytes = 0;
  } values_;

  static_assert(sizeof(ItemValues) == 24, "Preserve version 1 tree counters");
};
