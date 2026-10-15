// clang-format off
#include "pch.h"
// clang-format on
#include "installer_paths.h"

#include <algorithm>
#include <cwctype>
#include <system_error>

namespace driver_installer {
Command ParseCommand(std::wstring_view command_line) {
  const auto begin = command_line.find_first_not_of(L" \t\r\n");
  if (begin == command_line.npos)
    return Command::kInvalid;
  command_line = command_line.substr(begin, command_line.find_last_not_of(L" \t\r\n") - begin + 1);
  if (command_line.front() == L'/' || command_line.front() == L'-')
    command_line.remove_prefix(1);
  std::wstring normalized(command_line);
  for (auto& character : normalized)
    character = static_cast<wchar_t>(std::towlower(character));
  if (normalized == L"install")
    return Command::kInstall;
  if (normalized == L"uninstall")
    return Command::kUninstall;
  return Command::kInvalid;
}

std::wstring ParentDirectory(std::wstring_view path) {
  const auto slash = path.find_last_of(L"\\/");
  return slash == path.npos ? std::wstring{} : std::wstring(path.substr(0, slash + 1));
}

std::wstring AppendFileName(std::wstring_view directory, std::wstring_view name) {
  std::wstring result(directory);
  if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
    result += L'\\';
  result += name;
  return result;
}

std::wstring GetModuleDirectory() {
  std::wstring path(256, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0)
      throw std::system_error(GetLastError(), std::system_category(), "GetModuleFileNameW");
    if (length < path.size()) {
      path.resize(length);
      return ParentDirectory(path);
    }
    if (path.size() >= 32768)
      throw std::system_error(ERROR_FILENAME_EXCED_RANGE, std::system_category());
    path.resize((std::min)(path.size() * 2, std::size_t{32768}));
  }
}

std::wstring GetDriverDirectory() {
  std::wstring directory(256, L'\0');
  for (;;) {
    const UINT length = GetWindowsDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if (length == 0)
      throw std::system_error(GetLastError(), std::system_category(), "GetWindowsDirectoryW");
    if (length < directory.size()) {
      directory.resize(length);
      return AppendFileName(directory, L"System32\\Drivers");
    }
    directory.resize(static_cast<std::size_t>(length) + 1);
  }
}
}  // namespace driver_installer
