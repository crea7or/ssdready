// clang-format off
#include "pch.h"
// clang-format on
#include "hyperlink_ctrl.h"

#include "resource.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

HyperlinkCtrl::HyperlinkCtrl() = default;


BEGIN_MESSAGE_MAP(HyperlinkCtrl, CStatic)
ON_CONTROL_REFLECT(STN_CLICKED, OnClicked)
ON_WM_CTLCOLOR_REFLECT()
ON_WM_SETCURSOR()
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// HyperlinkCtrl message handlers

HBRUSH HyperlinkCtrl::CtlColor(CDC* dc, UINT) {
  dc->SetTextColor(visited_ ? visited_colour_ : link_colour_);
  if (!underline_font_.GetSafeHandle()) {
    if (CFont* font = GetFont()) {
      LOGFONT description{};
      if (font->GetLogFont(&description)) {
        description.lfUnderline = TRUE;
        underline_font_.CreateFontIndirect(&description);
      }
    }
  }
  if (underline_font_.GetSafeHandle())
    dc->SelectObject(&underline_font_);
  dc->SetBkMode(TRANSPARENT);
  return static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
}

BOOL HyperlinkCtrl::OnSetCursor(CWnd*, UINT, UINT) {
  ::SetCursor(link_cursor_ ? link_cursor_ : ::LoadCursor(nullptr, IDC_HAND));
  return TRUE;
}

void HyperlinkCtrl::PreSubclassWindow() {
  ModifyStyle(0, SS_NOTIFY);
  if (url_.IsEmpty())
    GetWindowText(url_);
  PositionWindow();
  if (!link_cursor_)
    link_cursor_ = ::LoadCursor(nullptr, IDC_HAND);
  CStatic::PreSubclassWindow();
}

/////////////////////////////////////////////////////////////////////////////
// HyperlinkCtrl operations

void HyperlinkCtrl::SetUrl(const CString& url) {
  url_ = url;
  if (GetSafeHwnd())
    PositionWindow();
}

// Move and resize the window so that the window is the same size
// as the hyperlink text. This stops the hyperlink cursor being active
// when it is not directly over the text. If the text is left justified
// then the window is merely shrunk, but if it is centred or right
// justified then the window will have to be moved as well.
//
// Suggested by PÃ¥l K. TÃ¸nder

void HyperlinkCtrl::PositionWindow() {
  // Get the current window position
  CRect rect;
  GetWindowRect(rect);

  CWnd* parent = GetParent();
  if (parent)
    parent->ScreenToClient(rect);

  // Get the size of the window text
  CString window_text;
  GetWindowText(window_text);

  CClientDC dc(this);
  CFont* previous = GetFont() ? dc.SelectObject(GetFont()) : nullptr;
  CSize extent = dc.GetTextExtent(window_text);
  if (previous)
    dc.SelectObject(previous);

  // Get the text justification via the window style
  DWORD style = GetStyle();

  // Recalc the window size and position based on the text justification
  if (style & SS_CENTERIMAGE) {
    rect.DeflateRect(0, (rect.Height() - extent.cy) / 2);
  } else {
    rect.bottom = rect.top + extent.cy;
  }

  if ((style & SS_TYPEMASK) == SS_CENTER) {
    rect.DeflateRect((rect.Width() - extent.cx) / 2, 0);
  } else {
    if ((style & SS_TYPEMASK) == SS_RIGHT) {
      rect.left = rect.right - extent.cx;
    } else  // SS_LEFT = 0, so we can't test for it explicitly
    {
      rect.right = rect.left + extent.cx;
    }
  }
  // Move the window
  SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOZORDER);
}

void HyperlinkCtrl::OnClicked() {
  const INT_PTR result = reinterpret_cast<INT_PTR>(ShellExecute(GetSafeHwnd(), L"open", url_, nullptr, nullptr, SW_SHOW));
  visited_ = (result > HINSTANCE_ERROR);
  if (!visited_) {
    MessageBeep(MB_ICONEXCLAMATION);  // Unable to follow link
  } else {
    visited_ = TRUE;
    Invalidate();
  }
}
