// clang-format off
#include "pch.h"
// clang-format on

#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
CWinApp test_application;

void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

class TestCore {
 public:

  TestCore() : previous_(g_application_core), core_(ApplicationCore::StartupMode::kHeadless) {
    g_application_core = &core_;
    core_.GetManager()->SetLogStatus(true);
    auto volumes = std::make_unique<DriveInfoList>();
    auto& drive = volumes->emplace_back();
    drive.drive_letter[0] = L'C';
    drive.volume_name = L"\\Device\\HarddiskVolume42";
    core_.GetManager()->UpdateVolumes(std::move(volumes));
  }

  ~TestCore() {
    core_.GetManager()->Uninstall();
    g_application_core = previous_;
  }

  MiniSpyManager& manager() {
    return *core_.GetManager();
  }

 private:

  ApplicationCore* previous_;
  ApplicationCore core_;
};

struct Record {
  std::vector<ULONG_PTR> storage;
  DWORD bytes;

  explicit Record(const std::wstring& name = L"\\Device\\HarddiskVolume42\\Folder\\file.dat") {
    const auto size = offsetof(LOG_RECORD, Name) + (name.size() + 1) * sizeof(wchar_t);
    storage.resize((size + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
    bytes = static_cast<DWORD>(storage.size() * sizeof(ULONG_PTR));
    Set(offsetof(LOG_RECORD, Length), bytes);
    Set(offsetof(LOG_RECORD, RecordType), ULONG(RECORD_TYPE_NORMAL));
    RECORD_DATA data{};
    data.CallbackMajorId = 4;
    data.ProcessId = GetCurrentProcessId();
    data.WriteDataLength = 7;
    data.ReadDataLength = 3;
    Set(offsetof(LOG_RECORD, Data), data);
    std::memcpy(Name(), name.c_str(), (name.size() + 1) * sizeof(wchar_t));
  }

  template <class T>
  void Set(std::size_t offset, const T& value) {
    std::memcpy(reinterpret_cast<char*>(storage.data()) + offset, &value, sizeof(value));
  }

  wchar_t* Name() {
    return reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(storage.data()) + offsetof(LOG_RECORD, Name));
  }

  std::size_t Retrieve(MiniSpyManager& manager) {
    return manager.RetrieveLogRecordsBatch(storage.data(), bytes);
  }
};

void TestRecords() {
  TestCore core;
  auto& manager = core.manager();
  Check(manager.RetrieveLogRecordsBatch(nullptr, 12) == 0, "Null buffer is rejected");
  Record record;
  Check(manager.RetrieveLogRecordsBatch(reinterpret_cast<char*>(record.storage.data()) + 1, record.bytes - 1) == 0,
        "Misaligned buffer is rejected");
  for (const ULONG length : {ULONG(0), ULONG(offsetof(LOG_RECORD, Name)), record.bytes + ULONG(sizeof(ULONG_PTR)), ULONG_MAX}) {
    record.Set(offsetof(LOG_RECORD, Length), length);
    Check(record.Retrieve(manager) == 0, "Invalid record lengths are rejected");
  }
  record.Set(offsetof(LOG_RECORD, Length), record.bytes);
  std::wmemset(record.Name(), L'x', (record.bytes - offsetof(LOG_RECORD, Name)) / sizeof(wchar_t));
  Check(record.Retrieve(manager) == 0, "Unterminated names are rejected without reading beyond the record");
  record = Record();
  Check(record.Retrieve(manager) == 1, "Valid write is accepted");
  auto batch = manager.GetNextBatchToProcess();
  Check(batch && batch->size() == 1 && batch->front()->file_name == L"C:\\Folder\\file.dat", "Volume mapping is preserved");
  Check(manager.GetStatistics().read_bytes == 3, "Read accounting follows publication");

  // A valid prefix is retained if the following record is malformed.
  auto combined = record.storage;
  combined.insert(combined.end(), record.storage.begin(), record.storage.end());
  Check(manager.RetrieveLogRecordsBatch(combined.data(), record.bytes * 2) == 2, "Consecutive records tolerate native wire alignment");
  batch = manager.GetNextBatchToProcess();
  Check(batch && batch->size() == 2 && batch->back()->written_bytes == 7,
        "Copying record data preserves values across alignment boundaries");
  ULONG invalid_length = ULONG_MAX;
  std::memcpy(reinterpret_cast<char*>(combined.data()) + record.bytes, &invalid_length, sizeof(invalid_length));
  Check(manager.RetrieveLogRecordsBatch(combined.data(), record.bytes * 2) == 1, "Malformed trailing record cannot overflow traversal");
  manager.InitiateCleanUp();
  Check(record.Retrieve(manager) == 0, "Shutdown prevents new publication");
  Check(manager.ProcessNextLogRecord(true) && !manager.ProcessNextLogRecord(true), "Shutdown drains accepted records then exits");
  const auto statistics = manager.GetStatistics();
  Check(statistics.operations == 1 && statistics.written_bytes == 7 && statistics.rounded_written_bytes == 4096,
        "Draining preserves operation and byte totals");
}

void TestReparseData() {
  struct MountPoint {
    ULONG tag;
    USHORT data_length, unparsed, substitute_offset, substitute_length, print_offset, print_length;
  };

  static_assert(sizeof(MountPoint) == 16);
  TestCore core;
  auto& manager = core.manager();
  const std::wstring name = L"\\Device\\HarddiskVolume42\\Folder\\linked.dat";
  auto make_record = [&] {
    Record record(std::wstring(name.size() + 32, L'x'));
    record.Set(offsetof(LOG_RECORD, RecordType), ULONG(RECORD_TYPE_FILETAG));
    const MountPoint tag{
        IO_REPARSE_TAG_MOUNT_POINT, static_cast<USHORT>(8 + 4 + name.size() * 2), 0, 4, static_cast<USHORT>(name.size() * 2), 0, 0};
    std::memcpy(record.Name(), &tag, sizeof(tag));
    std::memcpy(reinterpret_cast<char*>(record.Name()) + sizeof(tag) + 4, name.data(), name.size() * sizeof(wchar_t));
    return record;
  };
  auto record = make_record();
  Check(record.Retrieve(manager) == 1, "Valid reparse record honors substitute offset");
  auto batch = manager.GetNextBatchToProcess();
  Check(batch && batch->front()->file_name == L"C:\\Folder\\linked.dat", "Reparse name is translated and terminated");
  for (const std::size_t field : {std::size_t(4), std::size_t(8), std::size_t(10)}) {
    record = make_record();
    const USHORT invalid = 0xffff;
    std::memcpy(reinterpret_cast<char*>(record.Name()) + field, &invalid, sizeof(invalid));
    Check(record.Retrieve(manager) == 0, "Out-of-bounds or odd reparse lengths and offsets are rejected");
  }
}

class HistoryDirectory {
 public:

  HistoryDirectory() {
    wchar_t current[32768];
    Check(GetCurrentDirectoryW(_countof(current), current) != 0, "Get test working directory");
    path_.Format(L"%s\\manager-test-%lu-%llu", current, GetCurrentProcessId(), GetTickCount64());
    Check(CreateDirectoryW(path_, nullptr) != FALSE, "Create isolated history directory");
  }

  ~HistoryDirectory() {
    DeleteFileW(file());
    DeleteFileW(file() + L".tmp");
    RemoveDirectoryW(path_);
  }

  const CString& path() const {
    return path_;
  }

  CString file() const {
    return path_ + L"\\Monitored.Files";
  }

  std::vector<char> Read() const {
    File input(FALSE, FALSE);
    Check(input.Open(file(), File::kModeRead), "Open saved history");
    std::vector<char> bytes(static_cast<std::size_t>(input.GetLength()));
    Check(input.Read(bytes.data(), static_cast<DWORD>(bytes.size())), "Read saved history");
    return bytes;
  }

  void Write(const std::vector<char>& bytes) const {
    File output(FALSE, FALSE);
    Check(output.Open(file(), File::kModeWrite | File::kCreate), "Open isolated history for replacement");
    Check(output.Write(const_cast<char*>(bytes.data()), static_cast<DWORD>(bytes.size())) && output.Close(), "Replace isolated history");
  }

 private:

  CString path_;
};

void TestHistory() {
  TestCore core;
  HistoryDirectory history;
  Core()->GetRuntimeSettings()->user_data_folder_ = history.path();
  auto& manager = core.manager();
  Check(!manager.LoadMonitoredItems() && GetLastError() == ERROR_FILE_NOT_FOUND, "Missing history is a valid fresh-session state");
  Record record;
  Check(record.Retrieve(manager) == 1 && manager.ProcessNextLogRecord(), "Prepare saved tree");
  Check(manager.SaveMonitoredItems(), "Save valid history");
  const auto saved = history.Read();
  DWORD version;
  MonitoringStatistics wire;
  std::memcpy(&version, saved.data(), sizeof(version));
  std::memcpy(&wire, saved.data() + sizeof(version), sizeof(wire));
  Check(version == 2 && wire.operations == 1 && wire.written_bytes == 7 && wire.read_bytes == 3,
        "Version 2 statistics preserve all monitoring counters");
  // Independently create a version 1 manager file, including its process slots.
  {
    File legacy(FALSE, FALSE);
    const DWORD legacy_version = 1;
    const auto processes = manager.GetProcessStatistics();
    Check(legacy.Open(history.file(), File::kModeWrite | File::kCreate) && legacy.Write(&legacy_version, sizeof(legacy_version)) &&
              legacy.Write(&wire, sizeof(wire)) && legacy.WriteSize32(processes.size()),
          "Write legacy manager header");
    for (const auto& process : processes)
      Check(legacy.Write(&process.written_bytes, sizeof(process.written_bytes)) && legacy.WriteSize32(process.process_index) &&
                legacy.WriteString(process.name, _MAX_PATH),
            "Write legacy process slots");
    Check(manager.GetRootFolder()->Save(&legacy) && legacy.Close(), "Write legacy tree");
  }
  Check(manager.LoadMonitoredItems() && manager.GetStatistics().written_bytes == 7, "Manager loads version 1 without losing statistics");
  Check(manager.SaveMonitoredItems(), "A legacy history is saved as version 2");
  const auto migrated = history.Read();
  std::memcpy(&version, migrated.data(), sizeof(version));
  Check(version == 2, "Version 1 history migrates on its next successful save");
  Check(record.Retrieve(manager) == 1 && manager.ProcessNextLogRecord(), "Populate path cache before reload");
  Check(manager.LoadMonitoredItems(), "Reload valid history");
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  Check(manager.GetProcessedItemsPerSecond() == 0, "Restored history is not counted as newly processed events");
  Check(manager.GetMaxProcessedItemsPerSecond() == 0, "Restored historical totals do not set the current run's peak");
  Check(record.Retrieve(manager) == 1 && manager.ProcessNextLogRecord(), "Cached path is safe after reload");
  Check(manager.GetRootFolder()->GetWriteSizeTotal() == 14, "Reloaded parent pointers propagate updates to the actual root");
  const auto expected = manager.GetStatistics();
  auto* original_root = manager.GetRootFolder();
  auto invalid = saved;
  version = 3;
  std::memcpy(invalid.data(), &version, sizeof(version));
  history.Write(invalid);
  Check(!manager.LoadMonitoredItems() && GetLastError() == ERROR_INVALID_DATA, "Unknown history versions fail");
  Check(manager.GetStatistics().written_bytes == expected.written_bytes && manager.GetRootFolder() == original_root,
        "Failed load retains the existing model");
  Check(!manager.SaveMonitoredItems() && history.Read() == invalid, "Failed load prevents overwriting unreadable history");
  history.Write(std::vector<char>(saved.begin(), saved.begin() + 20));
  Check(!manager.LoadMonitoredItems() && manager.GetStatistics().operations == expected.operations, "Truncated history retains counters");
  history.Write(saved);
  Check(manager.LoadMonitoredItems(), "A subsequent successful load restores saving");
  HANDLE held = CreateFileW(history.file(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(held != INVALID_HANDLE_VALUE, "Hold old history against replacement");
  const BOOL saved_while_held = manager.SaveMonitoredItems();
  CloseHandle(held);
  Check(!saved_while_held && history.Read() == saved, "Failed final replacement preserves the previous history");
  Check(GetFileAttributesW(history.file() + L".tmp") == INVALID_FILE_ATTRIBUTES, "Failed save cleans its temporary file");
  history.Write(invalid);
  Check(!manager.LoadMonitoredItems(), "Prepare protected invalid history");
  manager.ClearData();
  Check(manager.SaveMonitoredItems(), "Explicit user reset allows replacement of invalid history");
}

void TestSnapshots() {
  TestCore core;
  auto& manager = core.manager();
  Core()->GetInterfaceSettings()->do_not_log_files_.store(true);
  Record record;
  std::thread consumer([&] {
    MiniSpyManager::ProcessLogRecords(&manager);
  });
  bool coherent = true;
  for (std::size_t index = 0; index < 500; ++index) {
    record.Retrieve(manager);
    const auto snapshot = manager.GetStatistics();
    coherent =
        coherent && snapshot.written_bytes == snapshot.operations * 7 && snapshot.rounded_written_bytes == snapshot.operations * 4096;
  }
  manager.InitiateCleanUp();
  consumer.join();
  const auto snapshot = manager.GetStatistics();
  Check(coherent && snapshot.operations == 500 && snapshot.written_bytes == 3500 && snapshot.read_bytes == 1500,
        "Concurrent UI snapshots stay coherent and shutdown drains all accepted batches");
}

void TestProcessingRate() {
  TestCore core;
  auto& manager = core.manager();
  Core()->GetInterfaceSettings()->do_not_log_files_.store(true);
  Record record;
  Check(manager.GetProcessedItemsPerSecond() == 0, "Initial processing rate is zero");
  Check(manager.GetMaxProcessedItemsPerSecond() == 0, "New application run starts with a zero peak");
  for (int index = 0; index < 64; ++index)
    Check(record.Retrieve(manager) == 1, "Prepare queued events for rate measurement");
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  Check(manager.GetProcessedItemsPerSecond() == 0, "Queued events do not count as completed events");
  Check(manager.GetMaxProcessedItemsPerSecond() == 0, "Queued events do not raise the peak");
  while (manager.ProcessNextLogRecord()) {}
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  const double rate = manager.GetProcessedItemsPerSecond();
  Check(rate > 0 && rate <= 64, "Rate measures recently completed events per elapsed second");
  Check(manager.GetMaxProcessedItemsPerSecond() == rate, "First nonzero sample becomes the peak");
  Check(manager.GetProcessedItemsPerSecond() == rate, "Immediate repeated reads retain the cached rate");
  manager.SetLogStatus(false);
  Check(manager.GetProcessedItemsPerSecond() == 0, "Pausing resets the rate");
  Check(manager.GetMaxProcessedItemsPerSecond() == rate, "Pausing retains the current run's peak");
  manager.SetLogStatus(true);
  Check(manager.GetProcessedItemsPerSecond() == 0, "Resuming starts a fresh sampling interval");
  Check(manager.GetMaxProcessedItemsPerSecond() == rate, "Resuming retains the current run's peak");
  for (int index = 0; index < 256; ++index)
    Check(record.Retrieve(manager) == 1, "Prepare completed events after resuming");
  while (manager.ProcessNextLogRecord()) {}
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  const double peak = manager.GetProcessedItemsPerSecond();
  Check(peak > rate && manager.GetMaxProcessedItemsPerSecond() == peak, "A higher rate raises the peak");
  Check(record.Retrieve(manager) == 1 && manager.ProcessNextLogRecord(), "Prepare a smaller completed sample");
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  const double lower_rate = manager.GetProcessedItemsPerSecond();
  Check(lower_rate > 0 && lower_rate < peak && manager.GetMaxProcessedItemsPerSecond() == peak, "A lower rate retains the peak");
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  Check(manager.GetProcessedItemsPerSecond() == 0, "Idle rate returns to zero");
  Check(manager.GetMaxProcessedItemsPerSecond() == peak, "Idle samples retain the peak");
  manager.ClearData(FALSE);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  Check(manager.GetProcessedItemsPerSecond() == 0, "Clearing counters resets the rate baseline without underflow");
  Check(manager.GetMaxProcessedItemsPerSecond() == peak, "Clearing collected data retains the current run's peak");
}
}  // namespace

int main() {
  try {
    if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0))
      return 1;
    TestRecords();
    TestReparseData();
    TestHistory();
    TestSnapshots();
    TestProcessingRate();
    std::cout << "Driver record bounds, reparse data, transactional history, concurrent snapshots and processing rate passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
