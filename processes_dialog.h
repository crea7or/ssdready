#pragma once

#include "dialog_base.h"
#include "process_list_ctrl.h"
#include "styled_button.h"

// ProcessesDialog dialog

class ProcessesDialog : public DialogBase {
 public:

  ProcessesDialog(TreeItem* tree_item, BOOL all, CWnd* parent = nullptr);  // standard constructor
  ~ProcessesDialog() override = default;

  enum { kDialogId = IDD_DIALOG_PROCESSES };

 protected:

  void DoDataExchange(CDataExchange* exchange) override;  // DDX/DDV support
  static void SetColumns(SortableListCtrl* list_ctrl);

  TreeItem* tree_item_;
  BOOL all_;

  StyledButton ok_;

  DECLARE_MESSAGE_MAP()

 protected:

  BOOL OnInitDialog() override;

  ProcessListCtrl list_;
};
