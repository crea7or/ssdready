// clang-format off
#include "pch.h"
// clang-format on
#include <memory>
#include <string>
#include <system_error>

#include "installer_paths.h"

namespace {
class ServiceHandle {
 public:

  explicit ServiceHandle(SC_HANDLE handle) : handle_(handle) {
  }

  ~ServiceHandle() {
    if (handle_)
      CloseServiceHandle(handle_);
  }

  ServiceHandle(const ServiceHandle&) = delete;
  ServiceHandle& operator=(const ServiceHandle&) = delete;

  SC_HANDLE get() const {
    return handle_;
  }

 private:

  SC_HANDLE handle_;
};

DWORD WriteValue(const wchar_t* key, const wchar_t* name, DWORD type, const void* value, DWORD size) {
  HKEY opened = nullptr;
  LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr);
  if (status != ERROR_SUCCESS)
    return status;
  status = RegSetValueExW(opened, name, 0, type, static_cast<const BYTE*>(value), size);
  RegCloseKey(opened);
  return status;
}

DWORD ConfigureInstances() {
  constexpr wchar_t kRoot[] = L"SYSTEM\\CurrentControlSet\\Services\\SsdSpy\\Instances";
  constexpr wchar_t kDefault[] = L"SsdSpy - Top Instance";
  DWORD error = WriteValue(kRoot, L"DefaultInstance", REG_SZ, kDefault, sizeof(kDefault));
  if (error != ERROR_SUCCESS)
    return error;

  struct Instance {
    const wchar_t* name;
    const wchar_t* altitude;
  };

  const Instance instances[] = {{kDefault, L"385100"}, {L"SsdSpy - Middle Instance", L"370000"}, {L"SsdSpy - Bottom Instance", L"361000"}};
  for (const auto& instance : instances) {
    const auto key = std::wstring(kRoot) + L"\\" + instance.name;
    error = WriteValue(
        key.c_str(), L"Altitude", REG_SZ, instance.altitude, static_cast<DWORD>((wcslen(instance.altitude) + 1) * sizeof(wchar_t)));
    if (error != ERROR_SUCCESS)
      return error;
    const DWORD flags = 1;
    error = WriteValue(key.c_str(), L"Flags", REG_DWORD, &flags, sizeof(flags));
    if (error != ERROR_SUCCESS)
      return error;
  }
  return ERROR_SUCCESS;
}

DWORD InstallDriver(const std::wstring& path) {
  const ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
  if (!manager.get())
    return GetLastError();
  constexpr DWORD kAccess = SERVICE_START | SERVICE_CHANGE_CONFIG | DELETE;
  SC_HANDLE opened = OpenServiceW(manager.get(), L"SsdSpy", kAccess);
  bool created = false;
  if (!opened) {
    const DWORD error = GetLastError();
    if (error != ERROR_SERVICE_DOES_NOT_EXIST)
      return error;
    opened = CreateServiceW(manager.get(),
                            L"SsdSpy",
                            L"SsdSpy filter driver",
                            kAccess,
                            SERVICE_FILE_SYSTEM_DRIVER,
                            SERVICE_DEMAND_START,
                            SERVICE_ERROR_NORMAL,
                            path.c_str(),
                            nullptr,
                            nullptr,
                            L"FltMgr\0",
                            nullptr,
                            nullptr);
    if (!opened)
      return GetLastError();
    created = true;
  }
  const ServiceHandle service(opened);
  if (!created && !ChangeServiceConfigW(service.get(),
                                        SERVICE_NO_CHANGE,
                                        SERVICE_NO_CHANGE,
                                        SERVICE_NO_CHANGE,
                                        path.c_str(),
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr))
    return GetLastError();
  const DWORD configured = ConfigureInstances();
  if (configured != ERROR_SUCCESS) {
    if (created)
      DeleteService(service.get());
    return configured;
  }
  if (StartServiceW(service.get(), 0, nullptr))
    return ERROR_SUCCESS;
  const DWORD error = GetLastError();
  return error == ERROR_SERVICE_ALREADY_RUNNING ? ERROR_SUCCESS : error;
}

DWORD RemoveDriver() {
  const ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
  if (!manager.get())
    return GetLastError();
  const ServiceHandle service(OpenServiceW(manager.get(), L"SsdSpy", SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE));
  if (!service.get()) {
    const DWORD error = GetLastError();
    return error == ERROR_SERVICE_DOES_NOT_EXIST ? ERROR_SUCCESS : error;
  }
  SERVICE_STATUS status{};
  if (!ControlService(service.get(), SERVICE_CONTROL_STOP, &status)) {
    const DWORD error = GetLastError();
    if (error != ERROR_SERVICE_NOT_ACTIVE)
      return error;
  } else {
    // Stop is asynchronous: wait before deleting the service or its driver file.
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (status.dwCurrentState != SERVICE_STOPPED) {
      if (GetTickCount64() >= deadline)
        return ERROR_TIMEOUT;
      Sleep(100);
      if (!QueryServiceStatus(service.get(), &status))
        return GetLastError();
    }
  }
  if (DeleteService(service.get()))
    return ERROR_SUCCESS;
  const DWORD error = GetLastError();
  return error == ERROR_SERVICE_MARKED_FOR_DELETE ? ERROR_SUCCESS : error;
}

DWORD UpdateDriverFile(const std::wstring& source, const std::wstring& destination, bool remove) {
  BOOL redirected = FALSE;
  if (!IsWow64Process(GetCurrentProcess(), &redirected))
    return GetLastError();
  PVOID previous = nullptr;
  if (redirected && !Wow64DisableWow64FsRedirection(&previous))
    return GetLastError();
  // Limit disabled redirection to this one file operation; services retain the
  // native System32 path and UI/library loading keeps normal process behavior.
  const BOOL success = remove ? DeleteFileW(destination.c_str()) : CopyFileW(source.c_str(), destination.c_str(), FALSE);
  const DWORD error = success ? ERROR_SUCCESS : GetLastError();
  if (redirected)
    Wow64RevertWow64FsRedirection(previous);
  return error;
}

void ShowError(DWORD error) {
  wchar_t* raw_message = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr,
                 error,
                 0,
                 reinterpret_cast<LPWSTR>(&raw_message),
                 0,
                 nullptr);

  struct FreeMessage {
    void operator()(wchar_t* message) const {
      LocalFree(message);
    }
  };

  const std::unique_ptr<wchar_t, FreeMessage> message(raw_message);
  const auto fallback = L"Windows error " + std::to_wstring(error);
  MessageBoxW(nullptr, message ? message.get() : fallback.c_str(), L"SsdReady driver installer", MB_OK | MB_ICONERROR);
}
}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR command_line, int) {
  try {
    const auto command = driver_installer::ParseCommand(command_line ? command_line : L"");
    if (command == driver_installer::Command::kInvalid) {
      MessageBoxW(nullptr, L"Usage: driver_installer install | uninstall", L"SsdReady driver installer", MB_OK);
      return ERROR_INVALID_PARAMETER;
    }
    const auto source = driver_installer::AppendFileName(driver_installer::GetModuleDirectory(), L"ssdspy.sys");
    const auto destination = driver_installer::AppendFileName(driver_installer::GetDriverDirectory(), L"ssdspy.sys");
    DWORD error = ERROR_SUCCESS;
    if (command == driver_installer::Command::kUninstall) {
      error = RemoveDriver();
      if (error == ERROR_SUCCESS) {
        error = UpdateDriverFile(source, destination, true);
        if (error == ERROR_FILE_NOT_FOUND)
          error = ERROR_SUCCESS;
      }
    } else {
      error = UpdateDriverFile(source, destination, false);
      if (error == ERROR_SUCCESS)
        error = InstallDriver(destination);
    }
    if (error != ERROR_SUCCESS)
      ShowError(error);
    return static_cast<int>(error);
  } catch (const std::system_error& error) {
    ShowError(static_cast<DWORD>(error.code().value()));
    return error.code().value();
  } catch (const std::bad_alloc&) {
    ShowError(ERROR_NOT_ENOUGH_MEMORY);
    return ERROR_NOT_ENOUGH_MEMORY;
  }
}
