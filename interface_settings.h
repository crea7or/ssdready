#pragma once

#include <atomic>

#include "settings.h"

class InterfaceSettings : public Settings {
 public:

  InterfaceSettings() : Settings(L"Interface Settings") {
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
};
