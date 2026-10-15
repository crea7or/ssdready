#pragma once
#include <afxwin.h>

#include "resource.h"

class DialogBase : public CDialog {
 public:

  explicit DialogBase(UINT dialog_id, CWnd* parent = nullptr);
  INT_PTR DoModal() override;
  void ShowAboutBox();
  void SetAlwaysOnTop(BOOL enabled, DWORD flags = SWP_SHOWWINDOW);
  void MinimizeWindow();
  void RestoreWindow();
  void Tray(DWORD message, HICON icon, LPCTSTR tooltip);
  virtual void OnTrayRightClick();
  void ToggleWindowVisibility();
  BOOL IsVisibleOnScreen();

 protected:

  CBrush background_back_;
  BOOL hidden_window_ = FALSE;
  BOOL system_tray_ = FALSE;
  BOOL allow_resize_ = FALSE;
  BOOL main_window_ = FALSE;
  CRect main_window_rect_;
  int min_width_ = 0;
  int min_height_ = 0;
  BOOL OnInitDialog() override;
  afx_msg LRESULT TrayIconNotify(WPARAM, LPARAM message);
  afx_msg void OnSysCommand(UINT id, LPARAM parameter);
  afx_msg HCURSOR OnQueryDragIcon();
  afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
  afx_msg void OnInitMenuPopup(CMenu* menu, UINT index, BOOL system_menu);
  afx_msg BOOL OnEraseBkgnd(CDC* dc);
  afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT type);
  DECLARE_MESSAGE_MAP()
};
