#pragma once

#include "drive_size_option.h"

class DriveSizeComboBox : public CComboBox {
 public:

  DriveSizeComboBox();
  ~DriveSizeComboBox() override;

  BOOL AddOption(DriveSizeOption* item);

 protected:

  HBRUSH CtlColor(CDC* dc, UINT color_type);
  CBrush background_;

  void DrawItem(LPDRAWITEMSTRUCT draw_item) override;
  void MeasureItem(LPMEASUREITEMSTRUCT measure_item) override;

  DECLARE_MESSAGE_MAP()
};
