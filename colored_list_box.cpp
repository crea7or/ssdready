// clang-format off
#include "pch.h"
// clang-format on
#include "colored_list_box.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

ColoredListBox::ColoredListBox() {
  background_.CreateSolidBrush(Core()->GetControlBk());
}

BEGIN_MESSAGE_MAP(ColoredListBox, CListBox)
ON_WM_CTLCOLOR_REFLECT()
END_MESSAGE_MAP()

HBRUSH ColoredListBox::CtlColor(CDC* dc, UINT) {
  dc->SetBkColor(Core()->GetControlBk());
  dc->SetBkMode(TRANSPARENT);
  return background_;
}

void ColoredListBox::DrawItem(LPDRAWITEMSTRUCT draw_item) {
  if ((int)draw_item->itemID < 0)
    return;

  CDC* dc = CDC::FromHandle(draw_item->hDC);

  COLORREF previous_text_color;
  CString text;

  const COLORREF previous_background = dc->SetBkColor(Core()->GetControlBk());

  dc->FillRect(&draw_item->rcItem, &background_);

  // Set the background mode to TRANSPARENT to draw the text.
  int previous_background_mode = dc->SetBkMode(TRANSPARENT);

  previous_text_color = dc->SetTextColor(Core()->GetTextColor());

  // Get and display item text.
  GetText(draw_item->itemID, text);
  CRect rect = draw_item->rcItem;

  // Setup the text format.
  UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER;
  if (GetStyle() & LBS_USETABSTOPS)
    format |= DT_EXPANDTABS;

  // Calculate the rectangle size before drawing the text.

  dc->DrawText(text, -1, &rect, format);

  dc->SetTextColor(previous_text_color);
  dc->SetBkMode(previous_background_mode);
  dc->SetBkColor(previous_background);
}  // DrawItem

void ColoredListBox::MeasureItem(LPMEASUREITEMSTRUCT measure_item) {
  measure_item->itemHeight = ::GetSystemMetrics(SM_CYMENUCHECK);
}
