#pragma once

#include "settings.h"

class RuntimeSettings : public Settings {
 public:

  RuntimeSettings() : Settings(L"RunTime Settings") {
  }

  BOOL SaveSettings() override;
  BOOL LoadSettings() override;
  static CString GetExecutablePath();

  CString user_data_folder_;
  HWND system_tray_window_ = nullptr;
};
