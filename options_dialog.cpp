// options_dialog.cpp : implementation file
//
// clang-format off
#include "pch.h"
// clang-format on
#include "options_dialog.h"

#include "monitoring_tree_ctrl.h"
#include "ssd_ready_app.h"

// OptionsDialog dialog

OptionsDialog::OptionsDialog(CWnd* parent /*=nullptr*/) : DialogBase(OptionsDialog::kDialogId, parent) {
  collect_processes_information_ = Core()->GetInterfaceSettings()->collect_process_information_;
  autostart_session_ = Core()->GetInterfaceSettings()->autostart_session_;
  do_not_log_files_ = Core()->GetInterfaceSettings()->do_not_log_files_;
  tray_icon_ = Core()->GetInterfaceSettings()->tray_icon_;
  start_minimized_ = Core()->GetInterfaceSettings()->start_minimized_;
  auto_expand_folders_ = Core()->GetInterfaceSettings()->auto_expand_folders_;
  sort_tree_by_write_size_ = Core()->GetInterfaceSettings()->sort_tree_by_write_size_;
}

void OptionsDialog::DoDataExchange(CDataExchange* exchange) {
  DialogBase::DoDataExchange(exchange);
  DDX_Check(exchange, IDC_CHECK_PROCESS, collect_processes_information_);
  DDX_Check(exchange, IDC_CHECK_AUTOSTARTLOGGIN, autostart_session_);
  DDX_Check(exchange, IDC_CHECK_DONOTLOG_FILES, do_not_log_files_);
  DDX_Check(exchange, IDC_CHECK_TRAYICON, tray_icon_);
  DDX_Check(exchange, IDC_CHECK_STARTMINIMIZED, start_minimized_);
  DDX_Check(exchange, IDC_CHECK_AUTOEXPAND, auto_expand_folders_);
  DDX_Check(exchange, IDC_CHECK_SORTBYSIZE, sort_tree_by_write_size_);
  DDX_Control(exchange, IDOK, ok_);
  DDX_Control(exchange, IDCANCEL, cancel_);
  DDX_Control(exchange, IDC_BUTTON_HELP, help_);
  DDX_Control(exchange, IDC_CHECK_PROCESS, check_collect_processes_);
  DDX_Control(exchange, IDC_CHECK_DONOTLOG_FILES, check_do_not_log_);
  DDX_Control(exchange, IDC_CHECK_AUTOSTARTLOGGIN, check_autostart_);
  DDX_Control(exchange, IDC_CHECK_TRAYICON, check_show_tray_icon_);
  DDX_Control(exchange, IDC_CHECK_STARTMINIMIZED, check_start_minimized_);
  DDX_Control(exchange, IDC_CHECK_AUTOEXPAND, check_auto_expand_);
  DDX_Control(exchange, IDC_CHECK_SORTBYSIZE, check_sort_files_);
}

BEGIN_MESSAGE_MAP(OptionsDialog, DialogBase)
ON_BN_CLICKED(IDOK, &OptionsDialog::OnBnClickedOk)
ON_BN_CLICKED(IDC_BUTTON_HELP, &OptionsDialog::OnBnClickedButtonHelp)
END_MESSAGE_MAP()

BOOL OptionsDialog::OnInitDialog() {
  DialogBase::OnInitDialog();

  SetWindowTheme(GetDlgItem(IDC_CHECK_PROCESS)->m_hWnd, L"wstr", L"wstr");
  SetWindowTheme(GetDlgItem(IDC_CHECK_DONOTLOG_FILES)->m_hWnd, L"wstr", L"wstr");
  SetWindowTheme(GetDlgItem(IDC_CHECK_TRAYICON)->m_hWnd, L"wstr", L"wstr");
  SetWindowTheme(GetDlgItem(IDC_CHECK_AUTOSTARTLOGGIN)->m_hWnd, L"wstr", L"wstr");
  SetWindowTheme(GetDlgItem(IDC_CHECK_STARTMINIMIZED)->m_hWnd, L"wstr", L"wstr");
  SetWindowTheme(GetDlgItem(IDC_CHECK_AUTOEXPAND)->m_hWnd, L"wstr", L"wstr");
  SetWindowTheme(GetDlgItem(IDC_CHECK_SORTBYSIZE)->m_hWnd, L"wstr", L"wstr");

  return TRUE;
}

void OptionsDialog::OnBnClickedOk() {
  if (!UpdateData(TRUE))
    return;

  Core()->GetInterfaceSettings()->collect_process_information_ = collect_processes_information_;
  Core()->GetInterfaceSettings()->autostart_session_ = autostart_session_;
  Core()->GetInterfaceSettings()->do_not_log_files_ = do_not_log_files_;
  Core()->GetInterfaceSettings()->tray_icon_ = tray_icon_;
  Core()->GetInterfaceSettings()->start_minimized_ = start_minimized_;
  Core()->GetInterfaceSettings()->auto_expand_folders_ = auto_expand_folders_;
  Core()->GetInterfaceSettings()->sort_tree_by_write_size_ = sort_tree_by_write_size_;

  if (!Core()->GetInterfaceSettings()->SaveSettings()) {
    AfxMessageBox(L"Could not save all settings. Check registry permissions and try again.");
    return;
  }

  if (auto* tree = Core()->GetTreeControl())
    tree->RefreshSortOrder();
  OnOK();
}

void OptionsDialog::OnBnClickedButtonHelp() {
  ShellExecute(nullptr, L"open", L"https://www.ssdready.com/help/?inref=ssdready", nullptr, nullptr, SW_SHOWNORMAL);
}
