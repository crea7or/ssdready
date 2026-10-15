#pragma once
#include <afxwin.h>

#include "resource.h"

class SsdReadyApp : public CWinApp {
 public:

  SsdReadyApp();
  BOOL InitInstance() override;
  DECLARE_MESSAGE_MAP()
};
