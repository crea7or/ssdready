#pragma once

#include "app_log.h"
#include "interface_settings.h"
#include "mini_spy_manager.h"
#include "runtime_settings.h"

class MonitoringTreeCtrl;

class ApplicationCore {
 public:

  enum class StartupMode { kApplication, kHeadless };
  explicit ApplicationCore(StartupMode mode = StartupMode::kApplication);
  ~ApplicationCore();
  ApplicationCore(const ApplicationCore&) = delete;
  ApplicationCore& operator=(const ApplicationCore&) = delete;

  AppLog* GetLog() const {
    return &AppLog::Get();
  }

  InterfaceSettings* GetInterfaceSettings() {
    return &interface_settings_;
  }

  RuntimeSettings* GetRuntimeSettings() {
    return &runtime_settings_;
  }

  MiniSpyManager* GetManager() {
    return &manager_;
  }

  HICON GetMainIcon();

  HWND GetMainWindow() const {
    return main_window_;
  }

  void SetMainWindow(HWND window) {
    main_window_ = window;
  }

  // Workers read this reference under the manager's model lock.
  MonitoringTreeCtrl* GetTreeControl() const {
    return tree_control_;
  }

  void SetTreeControl(MonitoringTreeCtrl* control);

  COLORREF GetControlBk() const {
    return RGB(0x3d, 0x3d, 0x40);
  }

  COLORREF GetTextColor() const {
    return RGB(0xff, 0xff, 0xf0);
  }

  COLORREF GetWindowBk() const {
    return RGB(0x2d, 0x2d, 0x30);
  }

  BOOL OpenSsdReadyDriver();
  BOOL CloseSsdReadyDriver();

  bool IsStartingUp() const {
    return starting_up_;
  }

  void SetStartingUp(bool value) {
    starting_up_ = value;
  }

  UINT GetGlobalMessage() const {
    return global_message_;
  }

  void SetGlobalMessage(UINT message) {
    global_message_ = message;
  }

 private:

  UINT global_message_ = 0;
  CImageList tree_images_;
  RuntimeSettings runtime_settings_;
  InterfaceSettings interface_settings_;
  // Destroy workers before the settings/resources they can access.
  MiniSpyManager manager_;
  HICON main_icon_ = nullptr;  // Shared application resource; never DestroyIcon.
  HWND main_window_ = nullptr;
  MonitoringTreeCtrl* tree_control_ = nullptr;
  bool starting_up_ = true;
  bool settings_loaded_ = false;
  StartupMode startup_mode_;
};
