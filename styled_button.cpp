// clang-format off
#include "pch.h"
// clang-format on
#include "styled_button.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

StyledButton::StyledButton() {
  mouse_on_button_ = FALSE;

  icon_ = nullptr;
  disabled_icon_ = nullptr;
  icon_size_.width = 0;
  icon_size_.height = 0;

  // By default the button is not the default button

  color_bg_ = GetSysColor(COLOR_BTNFACE);
  color_fg_ = GetSysColor(COLOR_BTNTEXT);

  color_bg_ = Core()->GetControlBk();
  color_fg_ = Core()->GetTextColor();

}  // End of StyledButton

StyledButton::~StyledButton() {
  DeleteIcon();
}

BEGIN_MESSAGE_MAP(StyledButton, CButton)
ON_WM_CAPTURECHANGED()
ON_WM_KILLFOCUS()
ON_WM_MOUSEMOVE()
ON_WM_MOUSELEAVE()
ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

void StyledButton::SetIcon(int resource_id) {
  if (resource_id_ == resource_id && icon_)
    return;
  DeleteIcon();
  icon_ = static_cast<HICON>(LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(resource_id), IMAGE_ICON, 0, 0, LR_SHARED));
  resource_id_ = resource_id;
  icon_size_ = GetIconSize(icon_);
  disabled_icon_ = CreateGrayscaleIcon(icon_);
  if (GetSafeHwnd())
    Invalidate(FALSE);
}  // End of SetIcon

void StyledButton::DeleteIcon() {
  // LR_SHARED resource icons belong to the module. The gray copy is owned.
  icon_ = nullptr;
  if (disabled_icon_)
    DestroyIcon(disabled_icon_);
  disabled_icon_ = nullptr;
  icon_size_ = {};
  resource_id_ = 0;
}

void StyledButton::OnMouseMove(UINT flags, CPoint point) {
  if (!mouse_on_button_) {
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, GetSafeHwnd(), 0};
    if (TrackMouseEvent(&tracking)) {
      mouse_on_button_ = TRUE;
      Invalidate(FALSE);
    }
  }
  CButton::OnMouseMove(flags, point);
}

void StyledButton::OnMouseLeave() {
  if (mouse_on_button_) {
    mouse_on_button_ = FALSE;
    Invalidate(FALSE);
  }
  CButton::OnMouseLeave();
}  // End of OnMouseMove

void StyledButton::OnKillFocus(CWnd* new_window) {
  CButton::OnKillFocus(new_window);

  if (mouse_on_button_ == TRUE) {
    mouse_on_button_ = FALSE;
    Invalidate();
  }
}  // End of OnKillFocus

void StyledButton::OnCaptureChanged(CWnd* window) {
  if (mouse_on_button_) {
    mouse_on_button_ = FALSE;
    Invalidate(FALSE);
  }
  CButton::OnCaptureChanged(window);
}  // End of OnCaptureChanged

void StyledButton::DrawItem(LPDRAWITEMSTRUCT draw_item) {
  CDC* target_dc = CDC::FromHandle(draw_item->hDC);
  BufferedDeviceContext buffered_dc(target_dc);
  CDC* dc = &buffered_dc;


  //	CPen *pOldPen;
  BOOL pressed = (draw_item->itemState & ODS_SELECTED);
  BOOL disabled = (draw_item->itemState & ODS_DISABLED);

  CRect item_rect = draw_item->rcItem;

  // Prepare draw... paint button's area with background color

  COLORREF current_background = color_bg_;

  // Draw pressed button
  if (pressed) {
    current_background = GetSysColor(COLOR_BTNSHADOW);
  } else {
    if (mouse_on_button_) {
      current_background = GetSysColor(COLOR_3DDKSHADOW);
    }
  }

  dc->FillSolidRect(&item_rect, current_background);

  // Read the button's title
  CString title;
  GetWindowText(title);

  CRect caption = draw_item->rcItem;

  // Draw the icon
  if (icon_ != nullptr) {
    DrawTheIcon(dc, &draw_item->rcItem, &caption, pressed, disabled, item_rect.Height());
  }

  if (pressed) {
    caption.OffsetRect(1, 1);
  }

  // Get dialog's font


  // Get dialog's font
  CFont* font = GetFont();
  CFont* previous_font = font ? dc->SelectObject(font) : nullptr;


  dc->SetTextColor(color_fg_);
  dc->SetBkColor(color_bg_);

  // Center text
  CRect centerRect = caption;

  if (icon_ != nullptr) {
    caption.top = (icon_size_.height - 10);
  }

  dc->DrawText(title, -1, caption, DT_SINGLELINE | DT_CALCRECT);
  caption.OffsetRect((centerRect.Width() - caption.Width()) / 2, (centerRect.Height() - caption.Height()) / 2);

  dc->SetBkMode(TRANSPARENT);
  dc->DrawState(caption.TopLeft(), caption.Size(), (LPCTSTR)title, (disabled ? DSS_DISABLED : DSS_NORMAL), TRUE, 0, (CBrush*)nullptr);


  if (previous_font)
    dc->SelectObject(previous_font);
}

void StyledButton::DrawTheIcon(CDC* dc, RECT* item_rect, CRect* caption, BOOL pressed, BOOL disabled, int height) {
  CRect icon_rect = item_rect;

  // Center the icon horizontally

  icon_rect.left += ((icon_rect.Width() - icon_size_.width) / 2);

  // else

  // L'icona deve vedersi subito dentro il focus rect

  int topOffset = height - ((caption->Height() + icon_rect.Height()) / 2) + 6;

  // Center the icon vertically

  icon_rect.top = topOffset;

  // If button is pressed then press the icon also
  if (pressed)
    icon_rect.OffsetRect(1, 1);

  dc->DrawState(icon_rect.TopLeft(), icon_rect.Size(), (disabled && disabled_icon_ ? disabled_icon_ : icon_), DSS_NORMAL, (CBrush*)nullptr);

}  // End of DrawTheIcon

void StyledButton::PreSubclassWindow() {
  UINT button_style;
  button_style = GetButtonStyle();
  // Check if this is the default button

  // Add BS_OWNERDRAW style
  SetButtonStyle(button_style | BS_OWNERDRAW);
  CButton::PreSubclassWindow();
}  // End of PreSubclassWindow

LRESULT StyledButton::DefWindowProc(UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_LBUTTONDBLCLK) {
    message = WM_LBUTTONDOWN;
  }
  return CButton::DefWindowProc(message, wparam, lparam);
}  // End of DefWindowProc

// This function creates a grayscale icon starting from a given icon.
// The resulting icon will have the same size of the original one.
//
// Parameters:
//      [IN]    icon
//              Handle to the original icon.
//
// Return value:
//      If the function succeeds, the return value is the handle to
//      the newly created grayscale icon.
//      If the function fails, the return value is nullptr.
//
HICON StyledButton::CreateGrayscaleIcon(HICON icon) {
  if (!icon)
    return nullptr;

  struct IconBitmaps {
    ICONINFO info{};

    ~IconBitmaps() {
      if (info.hbmColor)
        ::DeleteObject(info.hbmColor);
      if (info.hbmMask)
        ::DeleteObject(info.hbmMask);
    }
  } original;

  if (!::GetIconInfo(icon, &original.info))
    return nullptr;
  if (!original.info.hbmColor)
    return CopyIcon(icon);
  BITMAP source{};
  if (!::GetObject(original.info.hbmColor, sizeof(source), &source) || source.bmWidth <= 0 || source.bmHeight <= 0)
    return nullptr;
  CWindowDC screen(nullptr);
  if (!screen.GetSafeHdc())
    return nullptr;
  BITMAPINFO format{};
  format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  format.bmiHeader.biWidth = source.bmWidth;
  format.bmiHeader.biHeight = -source.bmHeight;
  format.bmiHeader.biPlanes = 1;
  format.bmiHeader.biBitCount = 32;
  format.bmiHeader.biCompression = BI_RGB;
  void* pixels = nullptr;
  CBitmap grayscale;
  grayscale.Attach(CreateDIBSection(screen.GetSafeHdc(), &format, DIB_RGB_COLORS, &pixels, nullptr, 0));
  if (!grayscale.GetSafeHandle() || !pixels ||
      GetDIBits(screen.GetSafeHdc(), original.info.hbmColor, 0, source.bmHeight, pixels, &format, DIB_RGB_COLORS) != source.bmHeight)
    return nullptr;
  auto* colors = static_cast<RGBQUAD*>(pixels);
  const auto count = static_cast<std::size_t>(source.bmWidth) * source.bmHeight;
  for (std::size_t index = 0; index < count; ++index) {
    auto& color = colors[index];
    const BYTE luminance = static_cast<BYTE>((299u * color.rgbRed + 587u * color.rgbGreen + 114u * color.rgbBlue + 500u) / 1000u);
    color.rgbRed = color.rgbGreen = color.rgbBlue = luminance;
  }
  ICONINFO output = original.info;
  output.fIcon = TRUE;
  output.hbmColor = static_cast<HBITMAP>(grayscale.GetSafeHandle());
  return CreateIconIndirect(&output);
}  // End of CreateGrayscaleIcon

IconSize StyledButton::GetIconSize(HICON icon) {
  IconSize size;
  ZeroMemory(&size, sizeof(size));

  ICONINFO info;
  ZeroMemory(&info, sizeof(info));

  BOOL bRes = FALSE;

  bRes = GetIconInfo(icon, &info);
  if (!bRes)
    return size;

  BITMAP bmp;
  ZeroMemory(&bmp, sizeof(bmp));

  if (info.hbmColor) {
    const int nWrittenBytes = GetObject(info.hbmColor, sizeof(bmp), &bmp);
    if (nWrittenBytes > 0) {
      size.width = bmp.bmWidth;
      size.height = bmp.bmHeight;
      size.bits_per_pixel = bmp.bmBitsPixel;
    }
  } else if (info.hbmMask) {
    // Icon has no color plane, image data stored in mask
    const int nWrittenBytes = GetObject(info.hbmMask, sizeof(bmp), &bmp);
    if (nWrittenBytes > 0) {
      size.width = bmp.bmWidth;
      size.height = bmp.bmHeight / 2;
      size.bits_per_pixel = 1;
    }
  }

  if (info.hbmColor)
    DeleteObject(info.hbmColor);
  if (info.hbmMask)
    DeleteObject(info.hbmMask);

  return size;
}

BOOL StyledButton::OnEraseBkgnd(CDC*) {
  // TODO: Add your message handler code here and/or call default

  return FALSE;
}
