#pragma once
#include <afxcmn.h>

class SortableListCtrl : public CListCtrl {
 public:

  SortableListCtrl() = default;
  int FindByData(DWORD_PTR data);
  void ApplySettings();
  virtual void SortAllItems() = 0;

  void AllowAutoResize(BOOL enabled) {
    allow_auto_resize_ = enabled;
  }

  void CheckColumnsWidth();

  void SetColumnsCallback(void (*callback)(SortableListCtrl*)) {
    set_columns_ = callback;
  }

 protected:

  void (*set_columns_)(SortableListCtrl*) = nullptr;
  BOOL allow_auto_resize_ = FALSE;
  int GetColumnsCount();
  int GetColumnsSize();
  afx_msg void OnHeaderItemClick(NMHDR* header, LRESULT* result);
  afx_msg void OnWindowPosChanged(WINDOWPOS* position);
  DECLARE_MESSAGE_MAP()
};
