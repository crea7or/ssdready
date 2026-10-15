// clang-format off
#include "pch.h"
// clang-format on
#include "runtime_settings.h"

#include <KnownFolders.h>
#include <ShlObj.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "build_version.h"
#include "settings_registry.h"

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Uuid.lib")

CString RuntimeSettings::GetExecutablePath() {
  for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity = (std::min)(capacity * 2, DWORD(32768))) {
    std::vector<wchar_t> buffer(capacity);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
    if (length == 0)
      return {};
    if (length < capacity)
      return CString(buffer.data(), static_cast<int>(length));
    if (capacity == 32768)
      break;
  }
  SetLastError(ERROR_INSUFFICIENT_BUFFER);
  return {};
}

BOOL RuntimeSettings::SaveSettings() {
#ifndef _DEBUG
  const auto section = Registry()->OpenSection(section_, true);
  return section && section.WriteInt(L"m_dwordBuild", kBuildNumber);
#else
  return TRUE;
#endif
}

BOOL RuntimeSettings::LoadSettings() {
  PWSTR app_data = nullptr;
  const HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &app_data);
  const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(app_data, &CoTaskMemFree);
  if (FAILED(result) || !app_data) {
    SetLastError(FAILED(result) && HRESULT_FACILITY(result) == FACILITY_WIN32 ? HRESULT_CODE(result) : ERROR_PATH_NOT_FOUND);
    return FALSE;
  }
  CString folder(app_data);
  folder += L"\\CEZEO software\\SsdReady";
  const int status = SHCreateDirectoryExW(nullptr, folder, nullptr);
  if (status != ERROR_SUCCESS && status != ERROR_ALREADY_EXISTS && status != ERROR_FILE_EXISTS) {
    SetLastError(status);
    return FALSE;
  }
  const DWORD attributes = GetFileAttributesW(folder);
  if (attributes == INVALID_FILE_ATTRIBUTES)
    return FALSE;
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    SetLastError(ERROR_DIRECTORY);
    return FALSE;
  }
  user_data_folder_ = std::move(folder);
  system_tray_window_ = FindWindowW(L"Shell_TrayWnd", nullptr);
  return TRUE;
}
