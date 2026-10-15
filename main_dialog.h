// main_dialog.h : header file

#pragma once

#include <array>

#include "afxcmn.h"
#include "afxwin.h"
#include "drive_size_combo_box.h"
#include "drive_size_option.h"
#include "monitoring_tree_ctrl.h"
#include "resizable_dialog.h"
#include "styled_button.h"

class MainDialog : public ResizableDialog {
 public:

  MainDialog(CWnd* parent = nullptr);  // standard constructor

  enum { kDialogId = IDD_SSDREADY_DIALOG };

  MonitoringTreeCtrl tree_ctrl_;
  DriveSizeComboBox combo_size_;
  StyledButton menu_;
  StyledButton start_stop_;
  StyledButton estimate_;
  StyledButton buy_;
  StyledButton options_;
  StyledButton processes_;

 public:

  BOOL PreTranslateMessage(MSG* message) override;

 protected:

  void DoDataExchange(CDataExchange* exchange) override;  // DDX/DDV support

 protected:

  INT_PTR OnToolHitTest(CPoint point, TOOLINFO* tool_info) const override;

  void MoveControlLeftTo(UINT id, int dx, int width);
  void RecalculateToolbar();
  void CloseApp();

  void RecalculateLayout() override;
  void OnTrayRightClick() override;

  // The combo box borrows these addresses; fixed storage keeps them stable.
  std::array<DriveSizeOption, 14> drive_sizes_{{
      {L"32Gb", 32},
      {L"40Gb", 40},
      {L"64Gb", 64},
      {L"80Gb", 80},
      {L"120Gb", 120},
      {L"160Gb", 160},
      {L"180Gb", 180},
      {L"240Gb", 240},
      {L"256Gb", 256},
      {L"320Gb", 320},
      {L"512Gb", 512},
      {L"1Tb", 1024},
      {L"2Tb", 2048},
      {L"4Tb", 4096},
  }};

  UINT_PTR timer_ = 0;
  DWORD tree_update_counter_;
  DWORD64 estimated_endurance_bytes_ = 0;

  BOOL OnInitDialog() override;
  void OnOK() override;
  void OnCancel() override;
  afx_msg void OnDestroy();
  afx_msg void OnButtonStartStop();
  afx_msg void OnTimer(UINT_PTR timer_id);
  afx_msg void OnBnClickedButtonClearData();
  afx_msg void OnBnClickedButtonHelp();
  afx_msg void OnBnClickedButtonBuy();
  afx_msg void OnBnClickedButtonEstimate();
  afx_msg void OnBnClickedButtonOptions();
  DECLARE_MESSAGE_MAP()

 public:

  afx_msg void OnTreeProcessesCheck(NMHDR* header, LRESULT* result);
  afx_msg void OnBnClickedButtonProcesses();
  afx_msg BOOL OnQueryEndSession();
  afx_msg void OnTreeRightClick(NMHDR* header, LRESULT* result);
  afx_msg void OnBnClickedButtonMenu();
  afx_msg void OnCbnSelchangeComboSizes();
};
