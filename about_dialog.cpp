// main_dialog.cpp : implementation file
//
// clang-format off
#include "pch.h"
// clang-format on
#include "about_dialog.h"

#include "build_version.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

AboutDialog::AboutDialog() : DialogBase(AboutDialog::kDialogId) {
}

void AboutDialog::DoDataExchange(CDataExchange* exchange) {
  DialogBase::DoDataExchange(exchange);
  DDX_Control(exchange, IDC_STATIC_CORP_LINK, link_corp_);
  DDX_Control(exchange, IDC_STATIC_SSD_LINK, link_ssdr_);
  DDX_Control(exchange, IDOK, ok_);
}

BEGIN_MESSAGE_MAP(AboutDialog, DialogBase)
// No message handlers
END_MESSAGE_MAP()

BOOL AboutDialog::OnInitDialog() {
  DialogBase::OnInitDialog();

  CString text;
  text.Format(L"SsdReady Version %d.%d build %d", kVersion / 100, (kVersion - (kVersion / 100) * 100), kBuildNumber);
  SetDlgItemText(IDC_STATIC_BUILD, text);

  link_corp_.SetUrl(L"https://www.cezeo.com");
  link_ssdr_.SetUrl(L"https://www.ssdready.com");
  link_corp_.SetLinkCursor(AfxGetApp()->LoadCursor(IDC_CURSOR_HAND));
  link_ssdr_.SetLinkCursor(AfxGetApp()->LoadCursor(IDC_CURSOR_HAND));

  return TRUE;
}
