// clang-format off
#include "pch.h"
// clang-format on
#include "application_core.h"

#include "monitoring_tree_ctrl.h"
#include "resource.h"
#include "settings_registry.h"

namespace {
class ServiceHandle {
 public:

  explicit ServiceHandle(SC_HANDLE handle = nullptr) : handle_(handle) {
  }

  ~ServiceHandle() {
    if (handle_) {
      const DWORD error = GetLastError();
      CloseServiceHandle(handle_);
      SetLastError(error);
    }
  }

  ServiceHandle(const ServiceHandle&) = delete;
  ServiceHandle& operator=(const ServiceHandle&) = delete;

  SC_HANDLE get() const {
    return handle_;
  }

 private:

  SC_HANDLE handle_;
};

BOOL DriverError(LPCWSTR operation, DWORD error = GetLastError()) {
  SystemError report(operation, error);
  return FALSE;
}

BOOL RepairDriverImagePath(SC_HANDLE service) {
  DWORD required = 0;
  QueryServiceConfigW(service, nullptr, 0, &required);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return FALSE;
  if (required < sizeof(QUERY_SERVICE_CONFIGW) || required > 8192)
    return DriverError(L"Driver service configuration", ERROR_INVALID_DATA);
  std::vector<ULONG_PTR> buffer((required + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
  auto* configuration = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
  if (!QueryServiceConfigW(service, configuration, required, &required))
    return FALSE;
  const CString path(configuration->lpBinaryPathName);
  const int length = path.GetLength();
  if (length < 3 || path[0] != L'"' || path[length - 1] != L'"') {
    SetLastError(ERROR_INVALID_NAME);
    return FALSE;
  }
  const CString unquoted = path.Mid(1, length - 2);
  if (unquoted.Find(L'"') != -1) {
    SetLastError(ERROR_INVALID_NAME);
    return FALSE;
  }
  // A driver ImagePath is a filename, not an executable command line.
  if (!ChangeServiceConfigW(
          service, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, unquoted, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
    return FALSE;
  AppLog::Get().Add(L"Removed invalid quotes from SsdSpy driver image path.");
  return TRUE;
}

BOOL StartDriverService(SC_HANDLE service) {
  if (StartServiceW(service, 0, nullptr)) {
    AppLog::Get().Add(L"SsdSpy driver started.");
    return TRUE;
  }
  const DWORD error = GetLastError();
  if (error == ERROR_SERVICE_ALREADY_RUNNING) {
    AppLog::Get().Add(L"SsdSpy driver is already running.");
    return TRUE;
  }
  if (error == ERROR_INVALID_NAME) {
    if (!RepairDriverImagePath(service))
      return DriverError(L"Repair driver image path");
    if (StartServiceW(service, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
      AppLog::Get().Add(L"SsdSpy driver started after repairing its image path.");
      return TRUE;
    }
    return DriverError(L"StartService after path repair");
  }
  return DriverError(L"StartService", error);
}

BOOL ConfigureDriverInstances() {
  constexpr LPCWSTR base = L"SYSTEM\\CurrentControlSet\\Services\\SsdSpy\\Instances";
  const auto instances = RegistrySection::Open(HKEY_LOCAL_MACHINE, base, true);
  if (!instances || !instances.WriteString(L"DefaultInstance", L"SsdSpy - Top Instance"))
    return FALSE;

  struct Instance {
    LPCWSTR name;
    LPCWSTR altitude;
  };

  const Instance entries[] = {
      {L"SsdSpy - Bottom Instance", L"361000"}, {L"SsdSpy - Top Instance", L"385100"}, {L"SsdSpy - Middle Instance", L"370000"}};
  for (const auto& entry : entries) {
    const CString path = CString(base) + L"\\" + entry.name;
    const auto key = RegistrySection::Open(HKEY_LOCAL_MACHINE, path, true);
    if (!key || !key.WriteString(L"Altitude", entry.altitude) || !key.WriteInt(L"Flags", 1))
      return FALSE;
  }
  return TRUE;
}
}  // namespace

ApplicationCore::ApplicationCore(StartupMode mode) : startup_mode_(mode) {
  if (mode == StartupMode::kHeadless)
    return;
  CBitmap bitmap;
  if (bitmap.LoadBitmap(IDB_BITMAP_FILESYSTEM) && tree_images_.Create(16, 16, ILC_COLOR24 | ILC_MASK, 3, 1))
    tree_images_.Add(&bitmap, RGB(255, 0, 255));
  if (!runtime_settings_.LoadSettings())
    GetLog()->Add(L"Could not prepare the application data folder; history loading and saving are disabled.");
  settings_loaded_ = interface_settings_.LoadSettings() != FALSE;
  if (!settings_loaded_)
    GetLog()->Add(L"Could not load interface settings; defaults are in use.");
}

ApplicationCore::~ApplicationCore() {
  // Shutdown precedes destruction of resources and settings used by workers.
  // Best-effort persistence/logging must not throw from a destructor.
  try {
    manager_.Uninstall();
    if (startup_mode_ == StartupMode::kApplication && settings_loaded_ && !interface_settings_.SaveSettings())
      GetLog()->Add(L"Could not save interface settings.");
  } catch (CException* error) {
    error->Delete();
    OutputDebugStringW(L"SsdReady: exception during shutdown persistence.\n");
  } catch (...) {
    OutputDebugStringW(L"SsdReady: exception during shutdown persistence.\n");
  }
}

void ApplicationCore::SetTreeControl(MonitoringTreeCtrl* control) {
  if (control) {
    control->SetBkColor(GetControlBk());
    control->SetTextColor(GetTextColor());
    control->SetImageList(&tree_images_, TVSIL_NORMAL);
  }
  // Worker notifications read this pointer while updating the model. Detaching
  // waits for an active update before the UI can destroy the control.
  auto lock = manager_.LockData();
  tree_control_ = control;
}

HICON ApplicationCore::GetMainIcon() {
  if (!main_icon_)
    main_icon_ = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
  return main_icon_;
}

BOOL ApplicationCore::OpenSsdReadyDriver() {
  ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
  if (!manager.get())
    return DriverError(L"OpenSCManager");
  constexpr DWORD kServiceAccess = SERVICE_START | SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG;
  SC_HANDLE opened = OpenServiceW(manager.get(), L"SsdSpy", kServiceAccess);
  if (!opened && GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
    return DriverError(L"OpenService");
  if (!opened) {
    const ServiceHandle installer(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    if (!installer.get())
      return DriverError(L"OpenSCManager/CreateService");
    const CString executable = RuntimeSettings::GetExecutablePath();
    const int slash = executable.ReverseFind(L'\\');
    if (slash < 0)
      return DriverError(L"Driver executable path", ERROR_PATH_NOT_FOUND);
    const CString driver_path = executable.Left(slash + 1) + L"ssdspy.sys";
    opened = CreateServiceW(installer.get(),
                            L"SsdSpy",
                            L"SsdSpy filter driver",
                            kServiceAccess | DELETE,
                            SERVICE_FILE_SYSTEM_DRIVER,
                            SERVICE_DEMAND_START,
                            SERVICE_ERROR_NORMAL,
                            driver_path,
                            nullptr,
                            nullptr,
                            L"FltMgr\0",
                            nullptr,
                            nullptr);
    if (!opened)
      return DriverError(L"CreateService");
    const ServiceHandle service(opened);
    if (!ConfigureDriverInstances()) {
      const DWORD error = GetLastError();
      DeleteService(service.get());  // Roll back incomplete first installation.
      return DriverError(L"Configure driver instances", error);
    }
    return StartDriverService(service.get());
  }
  const ServiceHandle service(opened);
  return StartDriverService(service.get());
}

BOOL ApplicationCore::CloseSsdReadyDriver() {
  const ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
  if (!manager.get())
    return DriverError(L"OpenSCManager/stop");
  const ServiceHandle service(OpenServiceW(manager.get(), L"SsdSpy", SERVICE_STOP | DELETE));
  if (!service.get()) {
    const DWORD error = GetLastError();
    if (error == ERROR_SERVICE_DOES_NOT_EXIST || error == ERROR_SERVICE_MARKED_FOR_DELETE)
      return TRUE;
    return DriverError(L"OpenService/stop", error);
  }
  SERVICE_STATUS status{};
  const BOOL stopped = ControlService(service.get(), SERVICE_CONTROL_STOP, &status) || GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
  if (!stopped)
    DriverError(L"ControlService/stop");
  const BOOL deleted = DeleteService(service.get()) || GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE;
  if (!deleted)
    DriverError(L"DeleteService");
  return stopped && deleted;
}
