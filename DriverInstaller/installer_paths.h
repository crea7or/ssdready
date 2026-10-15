#pragma once

#include <string>
#include <string_view>

namespace driver_installer {
enum class Command { kInvalid, kInstall, kUninstall };
Command ParseCommand(std::wstring_view command_line);
std::wstring ParentDirectory(std::wstring_view path);
std::wstring AppendFileName(std::wstring_view directory, std::wstring_view name);
std::wstring GetModuleDirectory();
std::wstring GetDriverDirectory();
}  // namespace driver_installer
