// about_dialog.h : header file

#pragma once

#include "dialog_base.h"
#include "hyperlink_ctrl.h"
#include "resource.h"
#include "styled_button.h"

// AboutDialog dialog used for App About
class AboutDialog : public DialogBase {
 public:

  AboutDialog();

  enum { kDialogId = IDD_ABOUTBOX };

  HyperlinkCtrl link_corp_;
  HyperlinkCtrl link_ssdr_;

 protected:

  void DoDataExchange(CDataExchange* exchange) override;  // DDX/DDV support

 protected:

  BOOL OnInitDialog() override;
  DECLARE_MESSAGE_MAP()

 public:

  StyledButton ok_;
};
