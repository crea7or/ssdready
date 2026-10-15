// resizable_dialog.cpp : implementation file
// clang-format off
#include "pch.h"
// clang-format on
#include "resizable_dialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

ResizableDialog::ResizableDialog(UINT id, CWnd* parent) : DialogBase(id, parent) {
  positions_.reserve(48);
}

BEGIN_MESSAGE_MAP(ResizableDialog, DialogBase)
ON_WM_SIZE()

END_MESSAGE_MAP()

BOOL ResizableDialog::OnInitDialog() {
  DialogBase::OnInitDialog();

  allow_resize_ = TRUE;

  GetWindowRect(main_window_rect_);

  min_height_ = main_window_rect_.Height();
  min_width_ = main_window_rect_.Width();

  RecalculateLayout();

  return TRUE;
}

void ResizableDialog::OnSize(UINT type, int width, int height) {
  DialogBase::OnSize(type, width, height);
  if (!allow_resize_ || type == SIZE_MINIMIZED || width == 0 || height == 0)
    return;
  BeginLayout();
  RecalculateLayout();
  EndLayout();
}

void ResizableDialog::GetNewOffset(int& dx, int& dy) {
  CRect rect_ctrl;
  rect_ctrl = main_window_rect_;
  SetWindowRectangle();

  dx = main_window_rect_.Width() - rect_ctrl.Width();
  dy = main_window_rect_.Height() - rect_ctrl.Height();
}

void ResizableDialog::SetWindowRectangle() {
  GetWindowRect(main_window_rect_);
}

void ResizableDialog::MoveSizeControl(UINT id, int dx, int dy, int width_delta, int height_delta) {
  CWnd* control = GetDlgItem(id);
  if (!control)
    return;
  CRect rect;
  control->GetWindowRect(rect);
  ScreenToClient(rect);
  PositionControl(
      *control, rect.left + dx, rect.top + dy, (std::max)(0, rect.Width() + width_delta), (std::max)(0, rect.Height() + height_delta));
}

void ResizableDialog::PositionControl(CWnd& control, int x, int y, int width, int height) {
  CRect current;
  control.GetWindowRect(current);
  ScreenToClient(current);
  if (current.left == x && current.top == y && current.Width() == width && current.Height() == height)
    return;
  if (collect_positions_)
    positions_.push_back({control.GetSafeHwnd(), x, y, width, height});
  else
    control.SetWindowPos(nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ResizableDialog::BeginLayout() {
  positions_.clear();
  collect_positions_ = true;
}

void ResizableDialog::EndLayout() {
  collect_positions_ = false;
  if (positions_.empty())
    return;
  HDWP batch = BeginDeferWindowPos(static_cast<int>(positions_.size()));
  for (const auto& position : positions_) {
    if (!batch)
      break;
    batch = DeferWindowPos(
        batch, position.window, nullptr, position.x, position.y, position.width, position.height, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (!batch || !EndDeferWindowPos(batch)) {
    for (const auto& position : positions_)
      ::SetWindowPos(position.window, nullptr, position.x, position.y, position.width, position.height, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  positions_.clear();
}

void ResizableDialog::MoveSizePos(UINT id, int dx, int dy) {
  MoveSizeControl(id, dx, dy, 0, 0);
}

void ResizableDialog::MoveSizeWidth(UINT id, int dx_w) {
  MoveSizeControl(id, 0, 0, dx_w, 0);
}

void ResizableDialog::MoveSizeSize(UINT id, int dx_w, int dy_h) {
  MoveSizeControl(id, 0, 0, dx_w, dy_h);
}

void ResizableDialog::MoveSizeTopWidth(UINT id, int dy, int dx_w) {
  MoveSizeControl(id, 0, dy, dx_w, 0);
}
