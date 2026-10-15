#pragma once
#include <vector>

#include "dialog_base.h"

class ResizableDialog : public DialogBase {
 public:

  explicit ResizableDialog(UINT dialog_id, CWnd* parent = nullptr);

 protected:

  virtual void RecalculateLayout() = 0;
  void GetNewOffset(int& dx, int& dy);
  void SetWindowRectangle();
  void MoveSizeControl(UINT id, int dx, int dy, int width_delta, int height_delta);
  void MoveSizeWidth(UINT id, int width_delta);
  void MoveSizeTopWidth(UINT id, int dy, int width_delta);
  void MoveSizePos(UINT id, int dx, int dy);
  void MoveSizeSize(UINT id, int width_delta, int height_delta);
  void PositionControl(CWnd& control, int x, int y, int width, int height);

  struct ControlPosition {
    HWND window;
    int x, y, width, height;
  };

  std::vector<ControlPosition> positions_;
  bool collect_positions_ = false;
  void BeginLayout();
  void EndLayout();
  BOOL OnInitDialog() override;
  afx_msg void OnSize(UINT type, int width, int height);

  DECLARE_MESSAGE_MAP()
};
