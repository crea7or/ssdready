#pragma once

#include <atomic>

#include "autostart_task.h"
#include "settings.h"

class InterfaceSettings : public Settings {
 public:

  using AutostartHandler = BOOL (*)(bool, LPCWSTR);

  explicit InterfaceSettings(AutostartHandler handler = ConfigureAutostartTask)
      : Settings(L"Interface Settings"), autostart_handler_(handler) {
  }

  BOOL SaveSettings() override;
  BOOL LoadSettings() override;

  // Changed by the UI and read independently by each worker at batch boundaries.
  std::atomic<bool> collect_process_information_{true};
  std::atomic<bool> do_not_log_files_{false};
  bool autostart_session_ = false;
  bool tray_icon_ = false;
  bool start_minimized_ = false;
  bool auto_expand_folders_ = false;
  bool sort_tree_by_write_size_ = true;
  int selected_disk_size_ = -1;
  CString selected_drives_;
  // Legacy empty means all drives. A non-letter sentinel means explicitly none.
  static constexpr LPCWSTR kNoSelectedDrives = L"-";

  bool IsDriveSelected(LPCWSTR letter) const {
    return selected_drives_.IsEmpty() || selected_drives_.Find(letter) != -1;
  }

 private:

  AutostartHandler autostart_handler_;
};
