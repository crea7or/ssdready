// monitoring_tree_ctrl.cpp : implementation file
// clang-format off
#include "pch.h"
// clang-format on
#include "monitoring_tree_ctrl.h"

#include <utility>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

MonitoringTreeCtrl::MonitoringTreeCtrl() : update_posted_(false), update_depth_(0), redraw_suspended_(false) {
}

MonitoringTreeCtrl::~MonitoringTreeCtrl() {
}

BEGIN_MESSAGE_MAP(MonitoringTreeCtrl, CTreeCtrl)
ON_NOTIFY_REFLECT(TVN_GETDISPINFO, OnGetDisplayInformation)

ON_NOTIFY_REFLECT(TVN_ITEMEXPANDING, OnItemExpanding)
ON_NOTIFY_REFLECT(TVN_DELETEITEM, OnItemDeleted)
ON_MESSAGE(kItemsChangedMessage, &MonitoringTreeCtrl::OnItemsChanged)
ON_WM_CREATE()
ON_WM_RBUTTONDOWN()

END_MESSAGE_MAP()

void MonitoringTreeCtrl::EnableBufferedDrawing() {
  SetExtendedStyle(TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
}

void MonitoringTreeCtrl::PreSubclassWindow() {
  CTreeCtrl::PreSubclassWindow();
  EnableBufferedDrawing();
}

int MonitoringTreeCtrl::OnCreate(LPCREATESTRUCT create) {
  if (CTreeCtrl::OnCreate(create) == -1)
    return -1;
  EnableBufferedDrawing();
  return 0;
}

void MonitoringTreeCtrl::NoteTreeChange() {
  // WM_SETREDRAW(TRUE) can show a hidden window, so preserve its visibility.
  if (update_depth_ != 0 && !redraw_suspended_ && (GetStyle() & WS_VISIBLE)) {
    redraw_suspended_ = true;
    SetRedraw(FALSE);
  }
}

void MonitoringTreeCtrl::EndUpdate() {
  ASSERT(update_depth_ != 0);
  if (--update_depth_ == 0 && redraw_suspended_) {
    redraw_suspended_ = false;
    SetRedraw(TRUE);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_FRAME | RDW_ALLCHILDREN);
  }
}

void MonitoringTreeCtrl::SortChildrenIfNeeded(HTREEITEM parent) {
  HTREEITEM previous = GetChildItem(parent);
  if (previous == nullptr)
    return;
  for (HTREEITEM current = GetNextSiblingItem(previous); current != nullptr; previous = current, current = GetNextSiblingItem(current)) {
    if (CompareProc(GetItemData(previous), GetItemData(current), 0) > 0) {
      TVSORTCB sort = {parent, &MonitoringTreeCtrl::CompareProc, 0};
      NoteTreeChange();
      SortChildrenCB(&sort);
      return;
    }
  }
}

void MonitoringTreeCtrl::OnGetDisplayInformation(NMHDR* header, LRESULT* result) {
  auto lock = Core()->GetManager()->LockData();
  TV_DISPINFO* tv_disp_info = (TV_DISPINFO*)header;
  TV_ITEM* item = &tv_disp_info->item;
  TreeItem* contact_list_item = reinterpret_cast<TreeItem*>(item->lParam);
  if (contact_list_item)
    contact_list_item->GetDisplayInformation(item);
  *result = 0;
}

BOOL MonitoringTreeCtrl::AddModelItem(TreeItem* item) {
  if (!item)
    return FALSE;
  if (item->GetTreeItem() != nullptr)
    return TRUE;
  BOOL result = FALSE;
  TVINSERTSTRUCT tv_ins;
  HTREEITEM i_tree = nullptr, i_parent = TVI_ROOT;
  if (item->GetParent() != nullptr) {
    i_parent = item->GetParent()->GetTreeItem();
    // An unmaterialized model parent must never insert its child at the root.
    if (i_parent == nullptr)
      return FALSE;
  }
  tv_ins.hParent = i_parent;
  if (i_parent == TVI_ROOT) {
    tv_ins.hInsertAfter = TVI_SORT;
  } else {
    tv_ins.hInsertAfter = TVI_FIRST;
  }

  tv_ins.item.mask = TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_TEXT;
  tv_ins.item.pszText = LPSTR_TEXTCALLBACK;
  tv_ins.item.iImage = I_IMAGECALLBACK;
  tv_ins.item.iSelectedImage = I_IMAGECALLBACK;
  tv_ins.item.cChildren = item->GetChildrenCount() != 0;
  tv_ins.item.lParam = reinterpret_cast<LPARAM>(item);
  NoteTreeChange();
  i_tree = InsertItem(&tv_ins);

  if (i_tree) {
    item->SetTreeItem(i_tree);
    result = TRUE;
  }

  return result;
}

void MonitoringTreeCtrl::NotifyItemsChanged() {
  if (!update_posted_.exchange(true)) {
    if (!::PostMessage(GetSafeHwnd(), kItemsChangedMessage, 0, 0))
      update_posted_.store(false);
  }
}

void MonitoringTreeCtrl::RefreshSortOrder() {
  resort_all_ = true;
  NotifyItemsChanged();
}

void MonitoringTreeCtrl::OnItemExpanding(NMHDR* header, LRESULT* result) {
  LPNMTREEVIEW notification = reinterpret_cast<LPNMTREEVIEW>(header);
  if (notification->action == TVE_EXPAND) {
    auto lock = Core()->GetManager()->LockData();
    UpdateBatch batch(*this);
    TreeItem* item = reinterpret_cast<TreeItem*>(notification->itemNew.lParam);
    if (item != nullptr) {
      item->AddToTreeCtrl();
      // Sort before revealing children, and leave an unchanged order alone.
      SortChildrenIfNeeded(notification->itemNew.hItem);
    }
  }
  *result = 0;
}

void MonitoringTreeCtrl::OnItemDeleted(NMHDR* header, LRESULT* result) {
  auto lock = Core()->GetManager()->LockData();
  const auto* notification = reinterpret_cast<NMTREEVIEW*>(header);
  auto* item = reinterpret_cast<TreeItem*>(notification->itemOld.lParam);
  if (item)
    item->SetTreeItem(nullptr);
  *result = 0;
}

LRESULT MonitoringTreeCtrl::OnItemsChanged(WPARAM, LPARAM) {
  UpdateBatch batch(*this);
  std::vector<HTREEITEM> expand;
  {
    auto lock = Core()->GetManager()->LockData();
    update_posted_.store(false);
    auto* root = Core()->GetManager()->GetRootFolder();
    const bool force = std::exchange(resort_all_, false);
    if (!root->TakeTreeChildrenChanged() && !force)
      return 0;
    root->AddToTreeCtrl();
    BOOL autoExpand = Core()->GetInterfaceSettings()->auto_expand_folders_ && !Core()->IsStartingUp();
    // Walk only nodes already represented in the control, not the entire model.
    for (HTREEITEM handle = GetRootItem(); handle != nullptr;) {
      TreeItem* item = reinterpret_cast<TreeItem*>(GetItemData(handle));
      BOOL changed = item->TakeTreeChildrenChanged();
      BOOL expanded = (GetItemState(handle, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
      if (changed || force) {
        TVITEM state = {};
        state.mask = TVIF_CHILDREN;
        state.hItem = handle;
        int children = item->GetChildrenCount() != 0;
        if (GetItem(&state) && state.cChildren != children) {
          state.cChildren = children;
          NoteTreeChange();
          SetItem(&state);
        }
        if (expanded || autoExpand) {
          item->AddToTreeCtrl();
          SortChildrenIfNeeded(handle);
        }
        if (autoExpand && !expanded && children)
          expand.push_back(handle);
      }
      HTREEITEM next = (changed || force) ? GetChildItem(handle) : nullptr;
      while (next == nullptr && handle != nullptr) {
        next = GetNextSiblingItem(handle);
        handle = GetParentItem(handle);
      }
      handle = next;
    }
  }
  // Expanding sends TVN_ITEMEXPANDING, which acquires the model mutex itself.
  for (HTREEITEM handle : expand)
    Expand(handle, TVE_EXPAND);
  return 0;
}

TreeItem* MonitoringTreeCtrl::GetSelectedModelItem() {
  const HTREEITEM selected = CTreeCtrl::GetSelectedItem();
  return selected ? reinterpret_cast<TreeItem*>(GetItemData(selected)) : nullptr;
}

TreeItem* MonitoringTreeCtrl::GetModelItemAt(CPoint point) {
  UINT flags = 0;
  const HTREEITEM item = HitTest(point, &flags);
  if (!item || !(flags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON)))
    return nullptr;
  return reinterpret_cast<TreeItem*>(GetItemData(item));
}

void MonitoringTreeCtrl::OnRButtonDown(UINT flags, CPoint point) {
  UINT hit_flags = 0;
  HTREEITEM item = HitTest(point, &hit_flags);
  SelectItem(item && (hit_flags & TVHT_ONITEM) ? item : nullptr);
  CTreeCtrl::OnRButtonDown(flags, point);
}

// Sort the item in reverse alphabetical order.
int CALLBACK MonitoringTreeCtrl::CompareProc(LPARAM left, LPARAM right, LPARAM) {
  const auto* first = reinterpret_cast<TreeItem*>(left);
  const auto* second = reinterpret_cast<TreeItem*>(right);
  if (!first || !second)
    return (first != nullptr) - (second != nullptr);
  if (Core()->GetInterfaceSettings()->sort_tree_by_write_size_) {
    const bool first_folder = first->GetObjectType() == TreeItemType::kFolder;
    const bool second_folder = second->GetObjectType() == TreeItemType::kFolder;
    if (first_folder != second_folder)
      return first_folder ? -1 : 1;
    if (first->GetWriteSizeTotal() != second->GetWriteSizeTotal())
      return first->GetWriteSizeTotal() > second->GetWriteSizeTotal() ? -1 : 1;
  }
  return CompareStringOrdinal(first->GetAlias(), -1, second->GetAlias(), -1, TRUE) - CSTR_EQUAL;
}
