// clang-format off
#include "pch.h"
// clang-format on
#include "drive_list_ctrl.h"

#include "resource.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

DriveListCtrl::DriveListCtrl() {
  CBitmap image;
  if (image.LoadBitmap(IDB_BITMAP_DRIVES) && drives_image_list_.Create(64, 44, ILC_COLOR24 | ILC_MASK, 2, 1))
    drives_image_list_.Add(&image, RGB(255, 0, 255));
}

DriveListCtrl::~DriveListCtrl() {
  drives_image_list_.DeleteImageList();
}

BEGIN_MESSAGE_MAP(DriveListCtrl, SortableListCtrl)
ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnGetdispinfo)
ON_NOTIFY_REFLECT(NM_CLICK, OnClick)
ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomdrawList)
END_MESSAGE_MAP()

void DriveListCtrl::OnClick(NMHDR* header, LRESULT* result) {
  NMITEMACTIVATE* nm = (NMITEMACTIVATE*)header;
  if (nm->iItem != -1) {
    SetItemState(nm->iItem, LVIS_SELECTED, LVIS_SELECTED);
    SetSelectionMark(nm->iItem);
  }
  *result = 0;
}

void DriveListCtrl::AttachList() {
  SetImageList(&drives_image_list_, LVSIL_SMALL);
}

void DriveListCtrl::SortAllItems() {
  SortItems(
      [](LPARAM left, LPARAM right, LPARAM) -> int {
        const auto& first = *reinterpret_cast<const DriveInfo*>(left);
        const auto& second = *reinterpret_cast<const DriveInfo*>(right);
        const int letters = wcscmp(first.drive_letter.data(), second.drive_letter.data());
        return letters != 0 ? letters : first.volume_name.compare(second.volume_name);
      },
      0);
}

BOOL DriveListCtrl::AddDrive(const DriveInfo& drive, BOOL selected) {
  BOOL result = FALSE;
  LVITEM list_item;
  memset(&list_item, 0, sizeof(LVITEM));

  list_item.mask = LVIF_PARAM | LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
  list_item.iImage = I_IMAGECALLBACK;
  list_item.pszText = LPSTR_TEXTCALLBACK;
  list_item.lParam = reinterpret_cast<LPARAM>(&drive);
  if (selected) {
    list_item.state = LVIS_SELECTED;
  }
  list_item.stateMask = LVIS_SELECTED;
  list_item.iItem = GetItemCount();

  int item = InsertItem(&list_item);
  if (item != -1) {
    result = TRUE;
  }
  return result;
}

void DriveListCtrl::OnGetdispinfo(NMHDR* header, LRESULT* result) {
  auto& item = reinterpret_cast<NMLVDISPINFO*>(header)->item;
  const auto* drive = reinterpret_cast<const DriveInfo*>(item.lParam);
  if ((item.mask & LVIF_TEXT) && item.pszText && item.cchTextMax > 0) {
    wcsncpy_s(item.pszText, static_cast<std::size_t>(item.cchTextMax), drive ? drive->drive_letter.data() : L"", _TRUNCATE);
  }
  if (item.mask & LVIF_IMAGE)
    item.iImage = 0;
  *result = 0;
}

void DriveListCtrl::OnCustomdrawList(NMHDR* header, LRESULT* result) {
  NMLVCUSTOMDRAW* pLVCD = reinterpret_cast<NMLVCUSTOMDRAW*>(header);

  *result = 0;

  // Request item-specific notifications if this is the beginning of the paint cycle.
  if (CDDS_PREPAINT == pLVCD->nmcd.dwDrawStage) {
    *result = CDRF_NOTIFYITEMDRAW;
  } else if (CDDS_ITEMPREPAINT == pLVCD->nmcd.dwDrawStage) {
    // This is the beginning of an item's paint cycle.
    LVITEM item_state;
    int item_id = static_cast<int>(pLVCD->nmcd.dwItemSpec);
    CDC* dc = CDC::FromHandle(pLVCD->nmcd.hdc);
    const int saved_dc = dc->SaveDC();
    COLORREF background;
    // BOOL bListHasFocus;
    CRect item_rect;
    CRect icon_rect;
    CRect bounds;
    CRect text_rect;

    // Get the image index and selected/focused state of the item being drawn.
    ZeroMemory(&item_state, sizeof(LVITEM));
    item_state.mask = LVIF_IMAGE | LVIF_STATE | LVIF_PARAM;
    item_state.iItem = item_id;
    item_state.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    if (!GetItem(&item_state)) {
      if (saved_dc)
        dc->RestoreDC(saved_dc);
      return;
    }

    const auto* drive = reinterpret_cast<const DriveInfo*>(item_state.lParam);
    if (!drive) {
      if (saved_dc)
        dc->RestoreDC(saved_dc);
      return;
    }

    // Get the rect that holds the item's icon.
    GetItemRect(item_id, &icon_rect, LVIR_ICON);
    // Get the rect that bounds the text label.
    GetItemRect(item_id, &item_rect, LVIR_LABEL);
    if (!GetItemRect(item_id, &bounds, LVIR_BOUNDS)) {
      if (saved_dc)
        dc->RestoreDC(saved_dc);
      return;
    }

    // Draw the background of the list item.  Colors are selected according to the item's state.
    int image_index = 0;  // Off
    if (item_state.state & LVIS_SELECTED) {
      image_index = 1;  // On
      background = Core()->GetTextColor();
      dc->SetTextColor(Core()->GetControlBk());
    } else {
      background = Core()->GetWindowBk();
      dc->SetTextColor(Core()->GetTextColor());
    }

    // Draw the background & prep the DC for the text drawing.  Note
    // that the entire item RECT is filled in, so this emulates the full-
    // row selection style of normal lists.
    dc->FillSolidRect(bounds, background);

    dc->SetBkMode(TRANSPARENT);

    // Draw the icon.
    drives_image_list_.Draw(dc, image_index, icon_rect.TopLeft(), ILD_TRANSPARENT);

    // Tweak the rect a bit for nicer-looking text alignment.
    text_rect = item_rect;
    text_rect.left += 3;
    text_rect.top += 10;

    // Draw the text.
    CString text(drive->drive_letter.data());
    text += L"\n";
    text += drive->volume_name.c_str();

    dc->DrawText(text, text_rect, DT_VCENTER);

    if (saved_dc)
      dc->RestoreDC(saved_dc);
    *result = CDRF_SKIPDEFAULT;  // We've painted everything.
  }
}
