#pragma once
#include <afxwin.h>

class ColoredListBox : public CListBox {
 public:

  ColoredListBox();
  void DrawItem(LPDRAWITEMSTRUCT item) override;
  void MeasureItem(LPMEASUREITEMSTRUCT item) override;

 protected:

  HBRUSH CtlColor(CDC* dc, UINT type);
  CBrush background_;
  DECLARE_MESSAGE_MAP()
};
