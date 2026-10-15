#include "DriverInstaller/installer_paths.h"

#include <windows.h>

#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}
}  // namespace

int main() {
  using namespace driver_installer;
  try {
    for (const auto* command : {L"install", L"INSTALL", L" /Install \t", L"-install"})
      Check(ParseCommand(command) == Command::kInstall, "Exact install command");
    for (const auto* command : {L"uninstall", L"UNINSTALL", L" /Uninstall \t", L"-uninstall"})
      Check(ParseCommand(command) == Command::kUninstall, "Exact uninstall command");
    for (const auto* command : {L"", L" \t", L"reinstall", L"install uninstall", L"uninstalljunk"})
      Check(ParseCommand(command) == Command::kInvalid, "Ambiguous or accidental command rejected");
    Check(ParentDirectory(L"C:\\folder\\driver.exe") == L"C:\\folder\\", "Extract module folder");
    Check(ParentDirectory(L"\\\\server\\share\\file") == L"\\\\server\\share\\", "UNC folder");
    Check(ParentDirectory(L"file.exe").empty(), "Bare name has no folder");
    Check(AppendFileName(L"C:\\folder", L"ssdspy.sys") == L"C:\\folder\\ssdspy.sys", "Append separator");
    Check(AppendFileName(L"C:\\folder\\", L"ssdspy.sys") == L"C:\\folder\\ssdspy.sys", "Avoid duplicate separator");
    Check(AppendFileName(L"", L"file") == L"file", "Empty folder retains filename");
    const auto long_directory = std::wstring(2000, L'\u03bb');
    Check(AppendFileName(long_directory, L"file") == long_directory + L"\\file", "Long Unicode path retained");
    Check(!GetModuleDirectory().empty(), "Resolve actual test executable folder");
    const auto directory = GetDriverDirectory();
    Check(directory.size() > 17 && directory.substr(directory.size() - 17) == L"\\System32\\Drivers", "Native driver directory");
    std::cout << "Installer command/path checks passed; no services or files were changed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
