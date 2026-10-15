#pragma once

#include "afxwin.h"
#include "colored_list_box.h"
#include "resizable_dialog.h"
#include "styled_button.h"

// LogViewerDialog dialog
class LogViewerDialog : public ResizableDialog {
 public:

  LogViewerDialog(CWnd* parent = nullptr);  // standard constructor
  ~LogViewerDialog() override = default;

  enum { kDialogId = IDD_DIALOG_LOG };

 protected:

  void DoDataExchange(CDataExchange* exchange) override;  // DDX/DDV support
  BOOL OnInitDialog() override;
  void RecalculateLayout() override;

  StyledButton ok_;
  ColoredListBox list_box_;
  INT_PTR displayed_count_ = 0;
  CSize client_size_{};
  void RefreshLog();

  DECLARE_MESSAGE_MAP()

 public:

  afx_msg void OnDestroy();
  afx_msg LRESULT OnLogUpdated(WPARAM, LPARAM);
};
