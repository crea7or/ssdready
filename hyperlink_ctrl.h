#pragma once
#include <afxwin.h>

class HyperlinkCtrl : public CStatic {
 public:

  HyperlinkCtrl();
  void SetUrl(const CString& url);

  void SetLinkCursor(HCURSOR cursor) {
    link_cursor_ = cursor;
  }

 protected:

  void PreSubclassWindow() override;
  void PositionWindow();
  COLORREF link_colour_ = RGB(0, 0, 238);
  COLORREF visited_colour_ = RGB(85, 26, 139);
  BOOL visited_ = FALSE;
  CString url_;
  CFont underline_font_;
  HCURSOR link_cursor_ = nullptr;
  afx_msg HBRUSH CtlColor(CDC* dc, UINT type);
  afx_msg BOOL OnSetCursor(CWnd* window, UINT hit_test, UINT message);
  afx_msg void OnClicked();
  DECLARE_MESSAGE_MAP()
};
