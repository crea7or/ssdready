#pragma once
#include <afxcmn.h>

#include <atomic>

#include "tree_item.h"

class MonitoringTreeCtrl : public CTreeCtrl {
 public:

  MonitoringTreeCtrl();
  ~MonitoringTreeCtrl() override;

  class UpdateBatch {
   public:

    explicit UpdateBatch(MonitoringTreeCtrl& tree) : tree_(tree) {
      ++tree_.update_depth_;
    }

    ~UpdateBatch() {
      tree_.EndUpdate();
    }

    UpdateBatch(const UpdateBatch&) = delete;
    UpdateBatch& operator=(const UpdateBatch&) = delete;

   private:

    MonitoringTreeCtrl& tree_;
  };

  BOOL AddModelItem(TreeItem* item);
  void NotifyItemsChanged();
  void RefreshSortOrder();
  TreeItem* GetSelectedModelItem();
  // Only labels and item icons activate a model; expansion buttons do not.
  TreeItem* GetModelItemAt(CPoint point);
  static int CALLBACK CompareProc(LPARAM left, LPARAM right, LPARAM context);
  void PreSubclassWindow() override;

 protected:

  std::atomic<bool> update_posted_{false};
  unsigned update_depth_ = 0;
  bool redraw_suspended_ = false;
  bool resort_all_ = false;
  static constexpr UINT kItemsChangedMessage = WM_APP + 21;
  void EnableBufferedDrawing();
  void NoteTreeChange();
  void EndUpdate();
  void SortChildrenIfNeeded(HTREEITEM parent);
  afx_msg void OnGetDisplayInformation(NMHDR* header, LRESULT* result);
  afx_msg void OnItemExpanding(NMHDR* header, LRESULT* result);
  afx_msg void OnItemDeleted(NMHDR* header, LRESULT* result);
  afx_msg LRESULT OnItemsChanged(WPARAM, LPARAM);
  afx_msg int OnCreate(LPCREATESTRUCT create);
  afx_msg void OnRButtonDown(UINT flags, CPoint point);
  DECLARE_MESSAGE_MAP()
};
