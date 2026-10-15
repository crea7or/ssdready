// clang-format off
#include "pch.h"
// clang-format on
#include "interface_settings.h"

#include "runtime_settings.h"
#include "settings_registry.h"

BOOL InterfaceSettings::SaveSettings() {
  const auto section = Registry()->OpenSection(section_, true);
  if (!section)
    return FALSE;
  // Retain the original value names so installed settings remain compatible.
  bool saved = true;
  saved = section.WriteInt(L"m_intSelectedDiskSize", static_cast<DWORD>(selected_disk_size_)) && saved;
  saved = section.WriteInt(L"m_boolCollectProcessesInformation", collect_process_information_.load(std::memory_order_relaxed)) && saved;
  saved = section.WriteInt(L"m_boolAutostartSession", autostart_session_) && saved;
  saved = section.WriteInt(L"m_boolDoNotLogFiles", do_not_log_files_.load(std::memory_order_relaxed)) && saved;
  saved = section.WriteInt(L"m_boolTrayIcon", tray_icon_) && saved;
  saved = section.WriteInt(L"m_boolStartMinimized", start_minimized_) && saved;
  saved = section.WriteInt(L"m_boolAutoExpandFolders", auto_expand_folders_) && saved;
  saved = section.WriteInt(L"m_boolSortTreeByWriteSize", sort_tree_by_write_size_) && saved;
  saved = section.WriteString(L"m_csSelectedDrives", selected_drives_) && saved;
  if (!saved)
    return FALSE;

  const auto run = RegistrySection::Open(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", true);
  if (!run)
    return FALSE;
  if (!autostart_session_)
    return run.DeleteValue(L"SsdReady");
  const CString executable = RuntimeSettings::GetExecutablePath();
  if (executable.IsEmpty())
    return FALSE;
  const CString command = L"\"" + executable + L"\"";
  return run.WriteString(L"SsdReady", command);
}

BOOL InterfaceSettings::LoadSettings() {
  const auto section = Registry()->OpenSection(section_);
  if (!section && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
    return FALSE;
  selected_disk_size_ = static_cast<int>(section.GetInt(L"m_intSelectedDiskSize", static_cast<DWORD>(-1)));
  collect_process_information_.store(section.GetInt(L"m_boolCollectProcessesInformation", TRUE) != 0, std::memory_order_relaxed);
  autostart_session_ = section.GetInt(L"m_boolAutostartSession", FALSE) != 0;
  do_not_log_files_.store(section.GetInt(L"m_boolDoNotLogFiles", FALSE) != 0, std::memory_order_relaxed);
  tray_icon_ = section.GetInt(L"m_boolTrayIcon", FALSE) != 0;
  start_minimized_ = section.GetInt(L"m_boolStartMinimized", FALSE) != 0;
  auto_expand_folders_ = section.GetInt(L"m_boolAutoExpandFolders", FALSE) != 0;
  sort_tree_by_write_size_ = section.GetInt(L"m_boolSortTreeByWriteSize", TRUE) != 0;
  selected_drives_ = section.GetString(L"m_csSelectedDrives");
  return TRUE;
}
