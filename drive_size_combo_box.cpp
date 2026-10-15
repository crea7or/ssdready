// clang-format off
#include "pch.h"
// clang-format on
#include "drive_size_combo_box.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

DriveSizeComboBox::DriveSizeComboBox() {
  background_.CreateSolidBrush(Core()->GetControlBk());
}

DriveSizeComboBox::~DriveSizeComboBox() {
}

BEGIN_MESSAGE_MAP(DriveSizeComboBox, CComboBox)
ON_WM_CTLCOLOR_REFLECT()
END_MESSAGE_MAP()

HBRUSH DriveSizeComboBox::CtlColor(CDC* dc, UINT) {
  dc->SetBkColor(Core()->GetControlBk());

  return background_;
}

BOOL DriveSizeComboBox::AddOption(DriveSizeOption* item) {
  if (item == nullptr)
    return FALSE;
  // Without CBS_HASSTRINGS, AddString stores its argument as item data.
  // Supply the object immediately so measurement callbacks see valid data.
  const int index = AddString((GetStyle() & CBS_HASSTRINGS) ? item->GetDisplaySize() : reinterpret_cast<LPCTSTR>(item));
  if (index == CB_ERR || index == CB_ERRSPACE)
    return FALSE;
  if (SetItemDataPtr(index, item) == CB_ERR) {
    DeleteString(index);
    return FALSE;
  }
  return TRUE;
}

void DriveSizeComboBox::DrawItem(LPDRAWITEMSTRUCT draw_item) {
  DriveSizeOption* drive_item;
  if (draw_item->itemID != (UINT)-1) {
    drive_item = reinterpret_cast<DriveSizeOption*>(draw_item->itemData);
    if (drive_item == nullptr || draw_item->itemData == static_cast<ULONG_PTR>(CB_ERR))
      return;
    const wchar_t* text = drive_item->GetDisplaySize();

    CDC dc;
    dc.Attach(draw_item->hDC);

    // Save these value to restore them when done drawing.
    COLORREF previous_text_color = dc.GetTextColor();
    COLORREF previous_background_color = dc.GetBkColor();

    dc.SetBkColor(Core()->GetControlBk());
    dc.SetTextColor(Core()->GetTextColor());

    dc.FillRect(&draw_item->rcItem, &background_);
    // Draw the text.
    dc.DrawText(text, static_cast<int>(wcslen(text)), &draw_item->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // Reset the background color and the text color back to their
    // original values.
    dc.SetTextColor(previous_text_color);
    dc.SetBkColor(previous_background_color);

    dc.Detach();
  }
}

void DriveSizeComboBox::MeasureItem(LPMEASUREITEMSTRUCT measure_item) {
  ASSERT(measure_item->CtlType == ODT_COMBOBOX);

  if (measure_item->itemID != (UINT)-1) {
    LPCTSTR lpszText = L" ";
    if (!(GetStyle() & CBS_HASSTRINGS) && measure_item->itemData != 0 && measure_item->itemData != static_cast<ULONG_PTR>(CB_ERR)) {
      const auto* item = reinterpret_cast<DriveSizeOption*>(measure_item->itemData);
      lpszText = item->GetDisplaySize();
    }
    CSize sz;
    CDC* dc = GetDC();
    if (dc == nullptr)
      return;
    sz = dc->GetTextExtent(lpszText);
    ReleaseDC(dc);
    measure_item->itemHeight = 2 * sz.cy;
  }
}
