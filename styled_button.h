#pragma once
#include <afxwin.h>

#include "buffered_device_context.h"

struct IconSize {
  int width = 0;
  int height = 0;
  int bits_per_pixel = 0;
};

class StyledButton : public CButton {
 public:

  StyledButton();
  ~StyledButton() override;
  void SetIcon(int resource_id);
  void DrawItem(LPDRAWITEMSTRUCT item) override;


 protected:

  void PreSubclassWindow() override;
  LRESULT DefWindowProc(UINT message, WPARAM wparam, LPARAM lparam) override;
  afx_msg void OnCaptureChanged(CWnd* window);
  afx_msg void OnKillFocus(CWnd* window);
  afx_msg void OnMouseMove(UINT flags, CPoint point);
  afx_msg void OnMouseLeave();
  afx_msg BOOL OnEraseBkgnd(CDC* dc);
  DECLARE_MESSAGE_MAP()

 private:

  void DeleteIcon();
  void DrawTheIcon(CDC* dc, RECT* item_rect, CRect* caption, BOOL pressed, BOOL disabled, int height);
  HICON CreateGrayscaleIcon(HICON icon);
  IconSize GetIconSize(HICON icon);
  BOOL mouse_on_button_ = FALSE;
  IconSize icon_size_;
  HICON icon_ = nullptr;
  HICON disabled_icon_ = nullptr;
  int resource_id_ = 0;
  COLORREF color_bg_ = 0;
  COLORREF color_fg_ = 0;
};
