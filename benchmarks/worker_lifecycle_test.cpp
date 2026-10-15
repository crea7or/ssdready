// clang-format off
#include "pch.h"
// clang-format on

#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "buffered_file.h"
#include "monitoring_tree_ctrl.h"
#include "resource.h"

// Compile the actual implementations with local Windows boundary shims. No
// driver service is loaded, stopped, deleted, or registered by this executable.
namespace fake {
CWinApp test_application;
std::atomic<unsigned> sends{0};
std::atomic<bool> command_valid{true};
std::atomic<bool> blocking{false};
std::atomic<unsigned> batches_to_send{0};
DWORD start_error = ERROR_SUCCESS;
DWORD open_error = ERROR_SUCCESS;
DWORD stop_error = ERROR_SUCCESS;
HRESULT connect_result = S_OK;
unsigned fail_thread = 0;
unsigned thread_calls = 0;
int kernel_handles = 0;
int service_handles = 0;
unsigned stops = 0;
unsigned deletes = 0;
HANDLE port = nullptr;
bool creation_parameters_valid = false;
std::wstring image_path;
DWORD query_error = ERROR_SUCCESS;
DWORD change_error = ERROR_SUCCESS;
unsigned configuration_changes = 0;
unsigned starts = 0;
const auto native_create_thread = &::CreateThread;
const auto native_cancel = &::CancelSynchronousIo;
const auto native_close = &::CloseHandle;

void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

SC_HANDLE WINAPI OpenManager(LPCWSTR, LPCWSTR, DWORD) {
  ++service_handles;
  return reinterpret_cast<SC_HANDLE>(1);
}

SC_HANDLE WINAPI OpenService(SC_HANDLE, LPCWSTR, DWORD) {
  if (open_error) {
    SetLastError(open_error);
    return nullptr;
  }
  ++service_handles;
  return reinterpret_cast<SC_HANDLE>(2);
}

BOOL WINAPI CloseService(SC_HANDLE) {
  --service_handles;
  return TRUE;
}

SC_HANDLE WINAPI CreateService(SC_HANDLE,
                               LPCWSTR,
                               LPCWSTR,
                               DWORD,
                               DWORD,
                               DWORD,
                               DWORD,
                               LPCWSTR binary_path,
                               LPCWSTR,
                               LPDWORD,
                               LPCWSTR dependencies,
                               LPCWSTR,
                               LPCWSTR) {
  const CString executable = RuntimeSettings::GetExecutablePath();
  const CString expected = executable.Left(executable.ReverseFind(L'\\') + 1) + L"ssdspy.sys";
  creation_parameters_valid = expected == binary_path && std::wcscmp(dependencies, L"FltMgr") == 0 && dependencies[7] == L'\0';
  SetLastError(ERROR_ACCESS_DENIED);
  return nullptr;
}

BOOL WINAPI Start(SC_HANDLE, DWORD, LPCWSTR*) {
  ++starts;
  if (!image_path.empty() && image_path.front() == L'"') {
    SetLastError(ERROR_INVALID_NAME);
    return FALSE;
  }
  SetLastError(start_error);
  return start_error == ERROR_SUCCESS;
}

BOOL WINAPI QueryConfiguration(SC_HANDLE, LPQUERY_SERVICE_CONFIGW configuration, DWORD capacity, LPDWORD required) {
  if (query_error) {
    SetLastError(query_error);
    return FALSE;
  }
  *required = static_cast<DWORD>(sizeof(*configuration) + (image_path.size() + 1) * sizeof(wchar_t));
  if (!configuration || capacity < *required) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  *configuration = {};
  configuration->dwServiceType = SERVICE_FILE_SYSTEM_DRIVER;
  configuration->lpBinaryPathName = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(configuration) + sizeof(*configuration));
  std::memcpy(configuration->lpBinaryPathName, image_path.c_str(), (image_path.size() + 1) * sizeof(wchar_t));
  return TRUE;
}

BOOL WINAPI ChangeConfiguration(SC_HANDLE,
                                DWORD type,
                                DWORD start,
                                DWORD control,
                                LPCWSTR path,
                                LPCWSTR group,
                                LPDWORD tag,
                                LPCWSTR dependencies,
                                LPCWSTR account,
                                LPCWSTR password,
                                LPCWSTR display_name) {
  Check(type == SERVICE_NO_CHANGE && start == SERVICE_NO_CHANGE && control == SERVICE_NO_CHANGE && !group && !tag && !dependencies &&
            !account && !password && !display_name,
        "Repair changes only the driver image path");
  if (change_error) {
    SetLastError(change_error);
    return FALSE;
  }
  ++configuration_changes;
  image_path = path;
  return TRUE;
}

BOOL WINAPI Stop(SC_HANDLE, DWORD, LPSERVICE_STATUS) {
  ++stops;
  SetLastError(stop_error);
  return stop_error == ERROR_SUCCESS;
}

BOOL WINAPI Delete(SC_HANDLE) {
  ++deletes;
  return TRUE;
}

HRESULT WINAPI Connect(LPCWSTR, DWORD, LPCVOID, WORD, LPSECURITY_ATTRIBUTES, HANDLE* target) {
  if (FAILED(connect_result))
    return connect_result;
  port = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Check(port != nullptr, "Create fake communication port");
  *target = port;
  ++kernel_handles;
  return S_OK;
}

HRESULT WINAPI Send(HANDLE handle, LPVOID input, DWORD input_bytes, LPVOID output, DWORD output_bytes, LPDWORD received) {
  const auto* command = static_cast<COMMAND_MESSAGE*>(input);
  if (input_bytes != sizeof(COMMAND_MESSAGE) || command->Command != GetMiniSpyLog || command->Reserved != 0 ||
      output_bytes != MiniSpyManager::kBufferSize)
    command_valid.store(false);
  sends.fetch_add(1);
  *received = 0;
  auto remaining = batches_to_send.load();
  if (remaining && batches_to_send.compare_exchange_strong(remaining, remaining - 1)) {
    constexpr wchar_t kName[] = L"\\Device\\HarddiskVolume42\\Pipeline\\file.dat";
    const DWORD bytes = ROUND_TO_SIZE(offsetof(LOG_RECORD, Name) + sizeof(kName), sizeof(ULONG_PTR));
    Check(bytes <= output_bytes, "Driver-format test record fits retrieval buffer");
    std::memset(output, 0, bytes);
    auto* record = static_cast<LOG_RECORD*>(output);
    record->Length = bytes;
    record->Data.CallbackMajorId = 4;
    record->Data.ProcessId = GetCurrentProcessId();
    record->Data.WriteDataLength = 37;
    record->Data.ReadDataLength = 11;
    std::memcpy(record->Name, kName, sizeof(kName));
    *received = bytes;
    return S_OK;
  }
  if (blocking.load()) {
    WaitForSingleObject(handle, INFINITE);
    return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
  }
  return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
}

HANDLE WINAPI
CreateWorker(LPSECURITY_ATTRIBUTES attributes, SIZE_T size, LPTHREAD_START_ROUTINE start, LPVOID context, DWORD flags, LPDWORD id) {
  if (++thread_calls == fail_thread) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  const auto handle = native_create_thread(attributes, size, start, context, flags, id);
  if (handle)
    ++kernel_handles;
  return handle;
}

BOOL WINAPI Cancel(HANDLE thread) {
  if (port)
    SetEvent(port);
  return native_cancel(thread);
}

BOOL WINAPI Close(HANDLE handle) {
  --kernel_handles;
  if (handle == port)
    port = nullptr;
  return native_close(handle);
}

void Reset() {
  Check(kernel_handles == 0 && service_handles == 0, "All worker, port, and service handles are closed");
  start_error = open_error = stop_error = ERROR_SUCCESS;
  connect_result = S_OK;
  thread_calls = fail_thread = stops = deletes = 0;
  sends.store(0);
  command_valid.store(true);
  blocking.store(false);
  batches_to_send.store(0);
  image_path = L"C:\\SsdReady Test\\ssdspy.sys";
  query_error = change_error = ERROR_SUCCESS;
  configuration_changes = starts = 0;
}
}  // namespace fake

#define OpenSCManagerW fake::OpenManager
#define OpenServiceW fake::OpenService
#define CloseServiceHandle fake::CloseService
#define CreateServiceW fake::CreateService
#define StartServiceW fake::Start
#define QueryServiceConfigW fake::QueryConfiguration
#define ChangeServiceConfigW fake::ChangeConfiguration
#define ControlService fake::Stop
#define DeleteService fake::Delete
#define FilterConnectCommunicationPort fake::Connect
#define FilterSendMessage fake::Send
#define CreateThread fake::CreateWorker
#define CancelSynchronousIo fake::Cancel
#define CloseHandle fake::Close
#include "../application_core.cpp"
#include "../mini_spy_manager.cpp"
#undef OpenSCManagerW
#undef OpenServiceW
#undef CloseServiceHandle
#undef CreateServiceW
#undef StartServiceW
#undef QueryServiceConfigW
#undef ChangeServiceConfigW
#undef ControlService
#undef DeleteService
#undef FilterConnectCommunicationPort
#undef FilterSendMessage
#undef CreateThread
#undef CancelSynchronousIo
#undef CloseHandle

namespace {
using fake::Check;

void TestServiceErrors() {
  ApplicationCore core(ApplicationCore::StartupMode::kHeadless);
  fake::Reset();
  fake::start_error = ERROR_SERVICE_ALREADY_RUNNING;
  Check(core.OpenSsdReadyDriver(), "Already-running service is a successful start");
  fake::start_error = ERROR_INVALID_IMAGE_HASH;
  Check(!core.OpenSsdReadyDriver() && GetLastError() == ERROR_INVALID_IMAGE_HASH, "Driver signature failure is reported");
  fake::Reset();
  fake::stop_error = ERROR_ACCESS_DENIED;
  Check(!core.CloseSsdReadyDriver() && fake::deletes == 1, "Successful deletion cannot mask a stop failure");
  fake::stop_error = ERROR_SERVICE_NOT_ACTIVE;
  Check(core.CloseSsdReadyDriver(), "Already-stopped service is a successful stop");
  fake::Reset();
  fake::open_error = ERROR_SERVICE_DOES_NOT_EXIST;
  Check(core.CloseSsdReadyDriver(), "Missing service is a successful cleanup");
  Check(!core.OpenSsdReadyDriver() && fake::creation_parameters_valid, "Creation uses an unquoted driver filename and MULTI_SZ dependency");
  fake::Reset();
  fake::image_path = L"\"C:\\SsdReady Test\\ssdspy.sys\"";
  Check(core.OpenSsdReadyDriver() && fake::image_path == L"C:\\SsdReady Test\\ssdspy.sys" && fake::configuration_changes == 1 &&
            fake::starts == 2,
        "Invalid quoted driver path is repaired and started again, preserving spaces");
  fake::Reset();
  fake::image_path = L"\"\\??\\C:\\SsdReady Test\\ssdspy.sys\"";
  Check(core.OpenSsdReadyDriver() && fake::image_path == L"\\??\\C:\\SsdReady Test\\ssdspy.sys", "Repair preserves a native path prefix");
  fake::Reset();
  fake::image_path = L"\"C:\\SsdReady Test\\ssdspy.sys\"";
  fake::query_error = ERROR_ACCESS_DENIED;
  Check(!core.OpenSsdReadyDriver() && GetLastError() == ERROR_ACCESS_DENIED && fake::configuration_changes == 0,
        "Configuration query failure stops startup without modifying the path");
  fake::Reset();
  fake::image_path = L"\"C:\\SsdReady Test\\ssdspy.sys\"";
  fake::change_error = ERROR_ACCESS_DENIED;
  Check(!core.OpenSsdReadyDriver() && GetLastError() == ERROR_ACCESS_DENIED && fake::starts == 1,
        "Configuration write failure does not retry startup");
  fake::Reset();
  fake::start_error = ERROR_INVALID_NAME;
  Check(!core.OpenSsdReadyDriver() && GetLastError() == ERROR_INVALID_NAME && fake::configuration_changes == 0,
        "An unquoted invalid path is reported without guessing a replacement");
  fake::Reset();
}

class CoreSession {
 public:

  CoreSession() {
    wchar_t current[32768];
    Check(GetCurrentDirectoryW(_countof(current), current) != 0, "Get test directory");
    path_.Format(L"%s\\lifecycle-test-%lu-%llu", current, GetCurrentProcessId(), GetTickCount64());
    Check(CreateDirectoryW(path_, nullptr) != FALSE, "Create isolated lifecycle history directory");
    g_application_core = new ApplicationCore(ApplicationCore::StartupMode::kHeadless);
    Core()->GetRuntimeSettings()->user_data_folder_ = path_;
  }

  ~CoreSession() {
    delete g_application_core;
    g_application_core = nullptr;
    DeleteFileW(path_ + L"\\Monitored.Files");
    DeleteFileW(path_ + L"\\Monitored.Files.tmp");
    RemoveDirectoryW(path_);
  }

 private:

  CString path_;
};

void TestWorkers() {
  for (unsigned failure : {1u, 2u}) {
    fake::Reset();
    CoreSession session;
    auto& manager = *Core()->GetManager();
    fake::fail_thread = failure;
    Check(!manager.Install(), "Partial worker startup reports failure");
    Check(manager.GetCleanUpStatus() && fake::kernel_handles == 0 && fake::service_handles == 0,
          "Partial startup cancels and joins every created worker and closes its port");
    Check(manager.Uninstall() && manager.Uninstall(), "Shutdown after partial startup is safe and idempotent");
  }
  fake::Reset();
  {
    CoreSession session;
    fake::connect_result = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    Check(!Core()->GetManager()->Install() && fake::thread_calls == 0 && fake::stops == 1,
          "Port connection failure cleans the driver without starting workers");
  }
  fake::Reset();
  {
    CoreSession session;
    auto& manager = *Core()->GetManager();
    Check(manager.SaveMonitoredItems(), "Prepare old startup history");
    fake::fail_thread = 2;
    Check(!manager.Install(), "Prepare failed worker session");
    {
      auto lock = manager.LockData();
      manager.GetTreeItemFromPath(CString(L"C:\\Retry\\file.dat"))->AddWrite(31);
    }
    fake::fail_thread = 0;
    Check(manager.Install() && manager.GetRootFolder()->GetWriteSizeTotal() == 31,
          "Startup retry retains in-memory writes instead of reloading stale saved history");
  }
  fake::Reset();
  {
    CoreSession session;
    auto& manager = *Core()->GetManager();
    fake::blocking.store(true);
    Check(manager.Install() && manager.Install() && fake::thread_calls == 2, "Repeated install does not create duplicate workers");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (fake::sends.load() == 0 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    Check(fake::sends.load() != 0 && fake::command_valid.load(), "Real retrieval entry sends an initialized driver command");
    // ApplicationCore destruction must cancel a blocked retrieval call and join
    // both real worker entry points without an explicit Uninstall call.
  }
  fake::Reset();
}

void TestCompletePipeline() {
  fake::Reset();
  {
    CoreSession session;
    auto& manager = *Core()->GetManager();
    auto volumes = std::make_unique<DriveInfoList>();
    auto& volume = volumes->emplace_back();
    volume.volume_name = L"\\Device\\HarddiskVolume42";
    volume.drive_letter[0] = L'C';
    manager.UpdateVolumes(std::move(volumes));
    manager.SetLogStatus(true);
    // Exercise the repair on the same session that starts both real workers.
    fake::image_path = L"\"C:\\SsdReady Test\\ssdspy.sys\"";
    Check(manager.Install(), "Start both workers after repairing the driver path");
    fake::batches_to_send.store(32);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    MonitoringStatistics statistics;
    do {
      statistics = manager.GetStatistics();
      if (statistics.operations == 32)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    Check(statistics.operations == 32 && statistics.written_bytes == 32 * 37 && statistics.read_bytes == 32 * 11 &&
              statistics.rounded_written_bytes == 32 * 4096,
          "Real retrieval and processing threads deliver every driver record to the counters");
    Check(manager.GetUnprocessedItemsCount() == 0 && !manager.GetCleanUpStatus(),
          "Processing drains the queue and both workers remain active");
    {
      auto lock = manager.LockData();
      Check(manager.GetRootFolder()->GetWriteSizeTotal() == statistics.written_bytes &&
                manager.GetTreeItemFromPath(CString(L"C:\\Pipeline\\file.dat"))->GetWriteSize() == statistics.written_bytes,
            "Driver records update file and ancestor totals");
    }
    const auto processes = manager.GetProcessStatistics();
    Check(processes.size() == 1 && processes.front().written_bytes == statistics.written_bytes, "Driver records update process totals");
  }
  fake::Reset();
}
}  // namespace

int main() {
  try {
    if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0))
      return 1;
    TestServiceErrors();
    TestWorkers();
    TestCompletePipeline();
    std::cout << "Service failures, driver-path repair, real retrieval-to-processing delivery, and worker shutdown passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
