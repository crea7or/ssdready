// sortable_list_ctrl.cpp : implementation file
// clang-format off
#include "pch.h"
// clang-format on
#include "sortable_list_ctrl.h"

#include "resource.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


BEGIN_MESSAGE_MAP(SortableListCtrl, CListCtrl)


ON_WM_WINDOWPOSCHANGED()
ON_NOTIFY(HDN_ITEMCLICKA, 0, OnHeaderItemClick)
ON_NOTIFY(HDN_ITEMCLICKW, 0, OnHeaderItemClick)
END_MESSAGE_MAP()

int SortableListCtrl::FindByData(DWORD_PTR data) {
  const int item_count = GetItemCount();
  for (int index = 0; index < item_count; ++index) {
    if (GetItemData(index) == data)
      return index;
  }
  return -1;
}

void SortableListCtrl::ApplySettings() {
  DWORD flags = LVS_EX_FULLROWSELECT;
  SetExtendedStyle(flags);
}

int SortableListCtrl::GetColumnsSize() {
  int size = 0;
  int count = GetColumnsCount();
  if (count > 0) {
    for (int cnt = 0; cnt < count; cnt++) {
      size += GetColumnWidth(cnt);
    }
  }
  return size;
}

int SortableListCtrl::GetColumnsCount() {
  CHeaderCtrl* header = GetHeaderCtrl();
  return header ? (std::max)(0, header->GetItemCount()) : 0;
}

void SortableListCtrl::OnWindowPosChanged(WINDOWPOS* position) {
  CListCtrl::OnWindowPosChanged(position);
  if (allow_auto_resize_ && !(position->flags & SWP_NOSIZE))
    CheckColumnsWidth();
}

void SortableListCtrl::CheckColumnsWidth() {
  if (allow_auto_resize_) {
    int sizes_difference;
    CRect new_client_rect;
    GetClientRect(new_client_rect);
    sizes_difference = new_client_rect.Width() - GetColumnsSize();
    if (sizes_difference != 0) {
      if (set_columns_ != nullptr) {
        (*set_columns_)(this);
      }
    }
  }
}

void SortableListCtrl::OnHeaderItemClick(NMHDR* header, LRESULT* result) {
  const auto* notification = reinterpret_cast<NMHEADER*>(header);
  if (notification->iItem >= 0)
    SortAllItems();
  *result = 0;
}
