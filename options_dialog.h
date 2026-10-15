#pragma once

#include "afxwin.h"
#include "dialog_base.h"
#include "styled_button.h"

// OptionsDialog dialog
class OptionsDialog : public DialogBase {
 public:

  OptionsDialog(CWnd* parent = nullptr);  // standard constructor
  ~OptionsDialog() override = default;

  enum { kDialogId = IDD_DIALOG_OPTIONS };

 protected:

  void DoDataExchange(CDataExchange* exchange) override;  // DDX/DDV support
  DECLARE_MESSAGE_MAP()

 public:

  BOOL OnInitDialog() override;
  afx_msg void OnBnClickedOk();

 protected:

  StyledButton ok_;
  StyledButton cancel_;
  StyledButton help_;

  CButton check_collect_processes_;
  CButton check_do_not_log_;
  CButton check_autostart_;
  CButton check_show_tray_icon_;
  CButton check_start_minimized_;
  CButton check_auto_expand_;
  CButton check_sort_files_;

  BOOL collect_processes_information_;
  BOOL autostart_session_;
  BOOL do_not_log_files_;
  BOOL start_minimized_;
  BOOL tray_icon_;
  BOOL auto_expand_folders_;
  BOOL sort_tree_by_write_size_;

 public:

  afx_msg void OnBnClickedButtonHelp();
};
