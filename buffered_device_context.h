#pragma once
// Author: Keith Rule, keithr@europa.com
// Copyright 1996-1997, Keith Rule
// You may freely use or modify this code provided this Copyright is included
// in all derived versions.
#include <afxwin.h>

class BufferedDeviceContext : public CDC {
 public:

  explicit BufferedDeviceContext(CDC* target) : target_dc_(target) {
    if (!target)
      return;
    if (!target->IsPrinting() && target->GetClipBox(&rect_) != ERROR && rect_.Width() > 0 && rect_.Height() > 0 &&
        CreateCompatibleDC(target) && bitmap_.CreateCompatibleBitmap(target, rect_.Width(), rect_.Height())) {
      previous_bitmap_ = SelectObject(&bitmap_);
      if (previous_bitmap_) {
        buffered_ = true;
        SetWindowOrg(rect_.left, rect_.top);
        return;
      }
    }
    // Draw directly if buffering is unavailable. Borrow the caller's DC.
    DeleteDC();
    m_bPrinting = target->m_bPrinting;
    m_hDC = target->m_hDC;
    m_hAttribDC = target->m_hAttribDC;
  }

  ~BufferedDeviceContext() override {
    if (buffered_) {
      target_dc_->BitBlt(rect_.left, rect_.top, rect_.Width(), rect_.Height(), this, rect_.left, rect_.top, SRCCOPY);
      SelectObject(previous_bitmap_);
    } else {
      m_hDC = m_hAttribDC = nullptr;
    }
  }

  BufferedDeviceContext(const BufferedDeviceContext&) = delete;
  BufferedDeviceContext& operator=(const BufferedDeviceContext&) = delete;

 private:

  CBitmap bitmap_;
  CBitmap* previous_bitmap_ = nullptr;
  CDC* target_dc_ = nullptr;
  CRect rect_;
  bool buffered_ = false;
};
