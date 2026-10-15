// ssd_ready_app.cpp : Defines the class behaviors for the application.
//

// clang-format off
#include "pch.h"
// clang-format on
#include "ssd_ready_app.h"

#include "main_dialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

BEGIN_MESSAGE_MAP(SsdReadyApp, CWinApp)
//    DO NOT EDIT what you see in these blocks of generated code!
ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

SsdReadyApp::SsdReadyApp() {
}

SsdReadyApp g_ssd_ready_app;

BOOL SsdReadyApp::InitInstance() {
  const wchar_t* application_name = L"CEZEO software SsdReady App";
  const UINT activate_message = RegisterWindowMessage(application_name);
  HANDLE instance = CreateMutex(nullptr, FALSE, application_name);
  const DWORD error = GetLastError();
  if (!instance) {
    SystemError failure(L"Create instance mutex", error);
    return FALSE;
  }

  struct InstanceHandle {
    HANDLE value;

    ~InstanceHandle() {
      CloseHandle(value);
    }
  } handle{instance};

  if (error == ERROR_ALREADY_EXISTS) {
    if (activate_message)
      ::PostMessage(HWND_BROADCAST, activate_message, 0, 0);
    return FALSE;
  }
  CWinApp::InitInstance();
  NewCore();

  struct CoreSession {
    ~CoreSession() {
      DeleteCore();
    }
  } core_session;

  Core()->SetGlobalMessage(activate_message);
  {
    MainDialog dialog;
    m_pMainWnd = &dialog;
    dialog.DoModal();
    m_pMainWnd = nullptr;
  }
  return FALSE;
}
