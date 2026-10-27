// clang-format off
#include "pch.h"
// clang-format on

#include "settings_registry.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include "interface_settings.h"
#include "runtime_settings.h"

namespace {
void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

// Redirect both predefined hives only inside this process. Production settings
// and the real Windows Run key are never touched by these tests.
class IsolatedRegistry {
 public:

  IsolatedRegistry() {
    path_.Format(L"Software\\SsdReadySettingsTest-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    Check(RegCreateKeyExW(HKEY_CURRENT_USER, path_, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &key_, nullptr) ==
              ERROR_SUCCESS,
          "Create isolated registry");
    if (RegCreateKeyExW(key_, L"UserHive", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &user_key_, nullptr) !=
            ERROR_SUCCESS ||
        RegCreateKeyExW(key_, L"MachineHive", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &machine_key_, nullptr) !=
            ERROR_SUCCESS) {
      Cleanup();
      throw std::runtime_error("Create isolated hive keys");
    }
    if (RegOverridePredefKey(HKEY_CURRENT_USER, user_key_) != ERROR_SUCCESS) {
      Cleanup();
      throw std::runtime_error("Redirect current-user hive");
    }
    current_user_redirected_ = true;
    if (RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine_key_) != ERROR_SUCCESS) {
      Cleanup();
      throw std::runtime_error("Redirect machine hive");
    }
    machine_redirected_ = true;
  }

  ~IsolatedRegistry() {
    Cleanup();
  }

  HKEY key() const {
    return user_key_;
  }

  HKEY machine_key() const {
    return machine_key_;
  }

 private:

  void Cleanup() {
    if (machine_redirected_)
      RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
    if (current_user_redirected_)
      RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    if (machine_key_)
      RegCloseKey(machine_key_);
    if (user_key_)
      RegCloseKey(user_key_);
    if (key_) {
      RegCloseKey(key_);
      key_ = nullptr;
      RegDeleteTreeW(HKEY_CURRENT_USER, path_);
    }
  }

  CString path_;
  HKEY key_ = nullptr;
  HKEY user_key_ = nullptr;
  HKEY machine_key_ = nullptr;
  bool current_user_redirected_ = false;
  bool machine_redirected_ = false;
};

class RegistryBinding {
 public:

  explicit RegistryBinding(SettingsRegistry& registry) : previous_(std::exchange(g_settings_registry, &registry)) {
  }

  ~RegistryBinding() {
    g_settings_registry = previous_;
  }

 private:

  SettingsRegistry* previous_;
};

void TestRegistryReadsAndOwnership(HKEY root) {
  static_assert(!std::is_copy_constructible_v<RegistrySection>);
  static_assert(std::is_nothrow_move_constructible_v<RegistrySection>);
  SettingsRegistry registry(root, L"RegistryChecks");
  {
    const auto missing = registry.OpenSection(L"Missing");
    Check(!missing && missing.GetInt(L"count", 42) == 42 && missing.GetString(L"name", L"default") == L"default",
          "Missing settings use defaults");
    Check(!missing.WriteInt(L"count", 1), "Invalid section reports failed write");
  }
  HKEY unexpected = nullptr;
  const LSTATUS status = RegOpenKeyExW(root, L"RegistryChecks", 0, KEY_READ, &unexpected);
  if (unexpected)
    RegCloseKey(unexpected);
  Check(status == ERROR_FILE_NOT_FOUND, "Reads must not create settings keys");

  auto section = registry.OpenSection(L"Values", true);
  Check(section && section.WriteInt(L"count", 0xffffffff) && section.WriteString(L"name", L"Unicode \u03b1\u0436"),
        "Write existing DWORD/string format");
  CString long_value(L'x', 5000);
  Check(section.WriteString(L"long", long_value), "Write long registry string");
  Check(section.GetInt(L"count", 0) == 0xffffffff && section.GetString(L"name") == L"Unicode \u03b1\u0436" &&
            section.GetString(L"long") == long_value,
        "Read signed sentinel, Unicode and strings beyond the stack buffer");
  Check(section.WriteString(L"empty", L"") && section.GetString(L"empty", L"default").IsEmpty(), "Empty string is valid");

  HKEY raw = nullptr;
  Check(RegOpenKeyExW(root, L"RegistryChecks\\Values", 0, KEY_SET_VALUE, &raw) == ERROR_SUCCESS, "Open fixture values");
  const DWORD binary = 7;
  const std::array<BYTE, 2> short_dword{1, 2};
  const std::array<wchar_t, 4> unterminated{L't', L'e', L's', L't'};
  const std::array<BYTE, 3> odd_string{L'a', 0, L'b'};
  const bool fixtures_written =
      RegSetValueExW(raw, L"binary", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&binary), sizeof(binary)) == ERROR_SUCCESS &&
      RegSetValueExW(raw, L"short", 0, REG_DWORD, short_dword.data(), sizeof(short_dword)) == ERROR_SUCCESS &&
      RegSetValueExW(raw, L"unterminated", 0, REG_SZ, reinterpret_cast<const BYTE*>(unterminated.data()), sizeof(unterminated)) ==
          ERROR_SUCCESS &&
      RegSetValueExW(raw, L"odd", 0, REG_SZ, odd_string.data(), sizeof(odd_string)) == ERROR_SUCCESS;
  RegCloseKey(raw);
  Check(fixtures_written, "Write malformed registry fixtures");
  Check(section.GetInt(L"binary", 42) == 42 && section.GetInt(L"short", 42) == 42 && section.GetString(L"count", L"default") == L"default",
        "Wrong types and DWORD sizes use defaults in release builds");
  Check(section.GetString(L"unterminated", L"default") == L"test", "Safely terminate stored strings");
  Check(section.GetString(L"odd", L"default") == L"default", "Reject odd UTF-16 byte count");
  Check(section.GetInt(nullptr, 42) == 42 && section.GetString(nullptr, nullptr).IsEmpty(), "Handle null read arguments");
  Check(!section.WriteString(L"null", nullptr) && !section.WriteInt(nullptr, 1), "Handle null write arguments");

  auto moved = std::move(section);
  Check(!section && moved.GetInt(L"count", 0) == 0xffffffff, "Move transfers key ownership");
  RegistrySection assigned;
  assigned = std::move(moved);
  Check(!moved && assigned.DeleteValue(L"name") && assigned.DeleteValue(L"name"), "Move assignment and idempotent delete");
  const auto read_only = registry.OpenSection(L"Values");
  Check(read_only && !read_only.WriteInt(L"count", 1) && read_only.GetInt(L"count", 0) == 0xffffffff,
        "Read-only handles reject writes without changing values");
}

bool task_enabled = false;
bool task_failure = false;
CString task_executable;

BOOL ConfigureTestTask(bool enabled, LPCWSTR executable) {
  task_enabled = enabled;
  task_executable = executable;
  if (task_failure)
    SetLastError(ERROR_ACCESS_DENIED);
  return !task_failure;
}

void TestSettingsAndAutostart(HKEY root, HKEY machine_root) {
  SettingsRegistry registry(root, L"Software\\CEZEO software\\SsdReady");
  RegistryBinding binding(registry);
  InterfaceSettings settings(ConfigureTestTask);
  Check(settings.collect_process_information_ && !settings.do_not_log_files_ && settings.sort_tree_by_write_size_ &&
            settings.selected_disk_size_ == -1 && !settings.tray_icon_,
        "Defaults exist before loading settings");
  Check(settings.LoadSettings() && settings.selected_disk_size_ == -1, "Load defaults without stored settings");
  const auto legacy = registry.OpenSection(L"Interface Settings", true);
  Check(legacy.WriteInt(L"m_intSelectedDiskSize", 3) && legacy.WriteInt(L"m_boolCollectProcessesInformation", 0) &&
            legacy.WriteInt(L"m_boolDoNotLogFiles", 2) && legacy.WriteInt(L"m_boolTrayIcon", 1) &&
            legacy.WriteInt(L"m_boolStartMinimized", 1) && legacy.WriteInt(L"m_boolAutoExpandFolders", 1) &&
            legacy.WriteInt(L"m_boolSortTreeByWriteSize", 0) && legacy.WriteString(L"m_csSelectedDrives", L"C:D:") &&
            legacy.WriteString(L"m_csLanguage", L"English"),
        "Write legacy setting names");
  Check(settings.LoadSettings() && !settings.collect_process_information_ && settings.do_not_log_files_ && settings.tray_icon_ &&
            settings.start_minimized_ && settings.auto_expand_folders_ && !settings.sort_tree_by_write_size_ &&
            settings.selected_disk_size_ == 3 && settings.selected_drives_ == L"C:D:",
        "Load legacy values and normalize BOOLs");
  settings.selected_drives_ = InterfaceSettings::kNoSelectedDrives;
  Check(!settings.IsDriveSelected(L"C:") && !settings.IsDriveSelected(L"D:"),
        "Explicitly no drives differs from legacy all-drives selection");
  settings.selected_drives_.Empty();
  Check(settings.IsDriveSelected(L"C:"), "Legacy empty drive selection still means all drives");
  settings.selected_drives_ = L"C:D:";
  const auto legacy_run = RegistrySection::Open(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", true);
  Check(legacy_run.WriteString(L"SsdReady", L"legacy.exe"), "Create isolated legacy autostart entry");
  settings.autostart_session_ = true;
  Check(settings.SaveSettings(), "Save settings and enable isolated autostart");
  const auto run = RegistrySection::Open(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
  Check(task_enabled && task_executable == RuntimeSettings::GetExecutablePath(), "Elevated autostart receives the exact executable path");
  Check(run.GetString(L"SsdReady", L"missing") == L"missing", "Legacy Run autostart is removed");
  task_failure = true;
  Check(!settings.SaveSettings() && GetLastError() == ERROR_ACCESS_DENIED, "Scheduled task failure reaches the caller");
  task_failure = false;
  Check(legacy.GetString(L"m_csLanguage") == L"English", "Unused language setting remains intact in the registry");
  settings.autostart_session_ = false;
  Check(settings.SaveSettings() && !task_enabled && run.GetString(L"SsdReady", L"missing") == L"missing", "Disable isolated autostart");
  Check(settings.SaveSettings(), "Saving disabled autostart repeatedly succeeds");
  settings.selected_disk_size_ = -1;
  Check(settings.SaveSettings() && legacy.GetInt(L"m_intSelectedDiskSize", 0) == 0xffffffff, "Preserve negative disk sentinel");
  InterfaceSettings reloaded;
  Check(reloaded.LoadSettings() && reloaded.selected_disk_size_ == -1 && reloaded.selected_drives_ == L"C:D:", "Settings round trip");

  SettingsRegistry machine(machine_root, L"Software\\CEZEO software\\SsdReady");
  const auto system = machine.OpenSection(L"System Settings", true);
  const auto machine_values = machine.OpenSection(L"Interface Settings", true);
  Check(machine_values.WriteInt(L"m_boolTrayIcon", 2), "Write distinguishable machine settings");
  Check(system.WriteInt(L"m_boolUseLocalMachineHive", 1), "Write legacy machine-hive preference");
  SettingsRegistry configured;
  Check(configured.OpenSection(L"Interface Settings").GetInt(L"m_boolTrayIcon", 0) == 2, "Load machine-selected existing settings");

  const auto runtime_section = registry.OpenSection(L"RunTime Settings", true);
  RuntimeSettings runtime;
  Check(runtime.user_data_folder_.IsEmpty() && runtime.system_tray_window_ == nullptr, "Runtime defaults are initialized");
#ifndef _DEBUG
  Check(runtime.SaveSettings() && runtime_section.GetInt(L"m_dwordBuild", 0) != 0, "Persist legacy build marker");
#else
  Check(runtime.SaveSettings(), "Runtime debug settings save");
#endif

  // These atomics are accessed in the same manner as UI and batch workers.
  std::thread writer([&settings] {
    for (int index = 0; index < 10000; ++index) {
      settings.collect_process_information_.store(index % 2 == 0, std::memory_order_relaxed);
      settings.do_not_log_files_.store(index % 2 != 0, std::memory_order_relaxed);
    }
  });
  for (int index = 0; index < 10000; ++index) {
    (void)settings.collect_process_information_.load(std::memory_order_relaxed);
    (void)settings.do_not_log_files_.load(std::memory_order_relaxed);
  }
  writer.join();
  Check(!settings.collect_process_information_ && settings.do_not_log_files_, "Concurrent flag updates finish consistently");

  // An unavailable registry must report failure through the settings API.
  bool save_succeeded = false;
  {
    SettingsRegistry restricted(nullptr, L"NewSettings");
    RegistryBinding restricted_binding(restricted);
    save_succeeded = settings.SaveSettings();
  }
  Check(!save_succeeded, "Propagate settings-save failure");
}
}  // namespace

int main() {
  try {
    Check(AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0) != FALSE, "Initialize MFC");
    IsolatedRegistry isolated;
    TestRegistryReadsAndOwnership(isolated.key());
    TestSettingsAndAutostart(isolated.key(), isolated.machine_key());
    std::cout << "Settings/registry defaults, legacy compatibility, malformed values, ownership, save failures and autostart passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
