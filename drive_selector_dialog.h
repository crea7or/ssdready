#pragma once

#include "afxcmn.h"
#include "dialog_base.h"
#include "drive_list_ctrl.h"
#include "resource.h"
#include "styled_button.h"

// DriveSelectorDialog dialog

class DriveSelectorDialog : public DialogBase {
 public:

  DriveSelectorDialog(CWnd* parent = nullptr);  // standard constructor
  ~DriveSelectorDialog() override = default;

  enum { kDialogId = IDD_DIALOG_DRIVES };

  static void SetColumns(SortableListCtrl* list_ctrl);

 protected:

  // Own the snapshot for as long as the list stores pointers to its entries.
  DriveInfoListPtr volumes_;
  StyledButton ok_;

  void DoDataExchange(CDataExchange* exchange) override;  // DDX/DDV support

  DECLARE_MESSAGE_MAP()

 public:

  DriveListCtrl list_drives_;
  afx_msg void OnBnClickedOk();
  BOOL OnInitDialog() override;
};
