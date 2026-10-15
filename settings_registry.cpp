// clang-format off
#include "pch.h"
// clang-format on
#include "settings_registry.h"

#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace {
BOOL Succeeded(LSTATUS status) {
  if (status == ERROR_SUCCESS)
    return TRUE;
  SetLastError(status);
  return FALSE;
}
}  // namespace

RegistrySection::~RegistrySection() {
  if (key_)
    RegCloseKey(key_);
}

RegistrySection::RegistrySection(RegistrySection&& other) noexcept : key_(std::exchange(other.key_, nullptr)) {
}

RegistrySection& RegistrySection::operator=(RegistrySection&& other) noexcept {
  if (this != &other) {
    if (key_)
      RegCloseKey(key_);
    key_ = std::exchange(other.key_, nullptr);
  }
  return *this;
}

RegistrySection RegistrySection::Open(HKEY root, LPCWSTR path, bool write) {
  RegistrySection section;
  if (!root || !path || !*path) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return section;
  }
  const LSTATUS status =
      write ? RegCreateKeyExW(
                  root, path, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &section.key_, nullptr)
            : RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &section.key_);
  Succeeded(status);
  return section;
}

DWORD RegistrySection::GetInt(LPCWSTR name, DWORD default_value) const {
  if (!key_ || !name)
    return default_value;
  DWORD value = 0;
  DWORD bytes = sizeof(value);
  const LSTATUS status = RegGetValueW(key_, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes);
  return status == ERROR_SUCCESS && bytes == sizeof(value) ? value : default_value;
}

CString RegistrySection::GetString(LPCWSTR name, LPCWSTR default_value) const {
  const CString fallback(default_value ? default_value : L"");
  if (!key_ || !name)
    return fallback;
  std::array<wchar_t, 256> stack_buffer{};
  DWORD type = REG_NONE;
  // Reserve one character for a terminator, including REG_SZ values stored
  // without one. Query the raw bytes to reject odd UTF-16 lengths.
  DWORD bytes = sizeof(stack_buffer) - sizeof(wchar_t);
  LSTATUS status = RegQueryValueExW(key_, name, nullptr, &type, reinterpret_cast<BYTE*>(stack_buffer.data()), &bytes);
  if (status == ERROR_SUCCESS) {
    if (type != REG_SZ || bytes % sizeof(wchar_t) != 0 || bytes > sizeof(stack_buffer) - sizeof(wchar_t))
      return fallback;
    stack_buffer[bytes / sizeof(wchar_t)] = L'\0';
    return CString(stack_buffer.data());
  }
  // Retry a bounded number of times if another writer grows the value.
  for (int attempt = 0; status == ERROR_MORE_DATA && attempt < 3; ++attempt) {
    if (type != REG_SZ || bytes % sizeof(wchar_t) != 0 || bytes > static_cast<DWORD>((std::numeric_limits<int>::max)() - sizeof(wchar_t)))
      return fallback;
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    bytes = static_cast<DWORD>((buffer.size() - 1) * sizeof(wchar_t));
    status = RegQueryValueExW(key_, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes);
    if (status == ERROR_SUCCESS) {
      if (type != REG_SZ || bytes % sizeof(wchar_t) != 0 || bytes > (buffer.size() - 1) * sizeof(wchar_t))
        return fallback;
      buffer[bytes / sizeof(wchar_t)] = L'\0';
      return CString(buffer.data());
    }
  }
  return fallback;
}

BOOL RegistrySection::WriteInt(LPCWSTR name, DWORD value) const {
  if (!key_ || !name)
    return Succeeded(ERROR_INVALID_PARAMETER);
  return Succeeded(RegSetValueExW(key_, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)));
}

BOOL RegistrySection::WriteString(LPCWSTR name, LPCWSTR value) const {
  if (!key_ || !name || !value)
    return Succeeded(ERROR_INVALID_PARAMETER);
  const std::size_t length = wcslen(value);
  if (length > (std::numeric_limits<DWORD>::max)() / sizeof(wchar_t) - 1)
    return Succeeded(ERROR_INVALID_DATA);
  return Succeeded(
      RegSetValueExW(key_, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), static_cast<DWORD>((length + 1) * sizeof(wchar_t))));
}

BOOL RegistrySection::DeleteValue(LPCWSTR name) const {
  if (!key_ || !name)
    return Succeeded(ERROR_INVALID_PARAMETER);
  const LSTATUS status = RegDeleteValueW(key_, name);
  return status == ERROR_FILE_NOT_FOUND || Succeeded(status);
}

SettingsRegistry::SettingsRegistry() {
  const auto system = RegistrySection::Open(HKEY_LOCAL_MACHINE, path_ + L"\\System Settings");
  if (system.GetInt(L"m_boolUseLocalMachineHive", 0) != 0)
    root_ = HKEY_LOCAL_MACHINE;
}

RegistrySection SettingsRegistry::OpenSection(LPCWSTR section, bool write) const {
  if (!section || !*section) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return {};
  }
  return RegistrySection::Open(root_, path_ + L"\\" + section, write);
}
