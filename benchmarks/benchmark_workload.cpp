#include "benchmark_workload.h"

#include <TlHelp32.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>

namespace {
CWinApp benchmark_app;
bool benchmark_failed = false;

std::uint64_t Number(const std::string& text) {
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
    throw std::invalid_argument("Options require unsigned decimal integers.");
  return std::stoull(text);
}

std::size_t Count(const std::string& text) {
  const auto value = Number(text);
  if (value > (std::numeric_limits<std::size_t>::max)())
    throw std::invalid_argument("Count exceeds size_t on this platform.");
  return static_cast<std::size_t>(value);
}
}  // namespace

namespace ssdready_benchmark {

Options ParseOptions(int* argc, char** argv) {
  Options options;
  int remaining = 1;
  for (int index = 1; index < *argc; ++index) {
    const std::string argument(argv[index]);
    const auto separator = argument.find('=');
    const auto key = argument.substr(0, separator);
    const auto value = separator == std::string::npos ? "" : argument.substr(separator + 1);
    if (key == "--items")
      options.items = Count(value);
    else if (key == "--unique_paths")
      options.unique_paths = Count(value);
    else if (key == "--processes")
      options.processes = Count(value);
    else if (key == "--seed") {
      const auto seed = Number(value);
      if (seed > UINT32_MAX)
        throw std::invalid_argument("Seed exceeds uint32_t.");
      options.seed = static_cast<std::uint32_t>(seed);
    } else if (key == "--cache") {
      if (value != "warm" && value != "cold")
        throw std::invalid_argument("Use --cache=warm or --cache=cold.");
      options.warm_cache = value == "warm";
    } else
      argv[remaining++] = argv[index];
  }
  *argc = remaining;
  argv[remaining] = nullptr;
  if (options.items == 0 || options.unique_paths == 0 || options.items > UINT32_MAX || options.unique_paths > UINT32_MAX)
    throw std::invalid_argument("Items and unique_paths must be between 1 and 4294967295.");
  return options;
}

Workload::Workload(const Options& options) : options_(options) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot enumerate this machine's processes.");
  PROCESSENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Process32First(snapshot, &entry)) {
    do {
      if (entry.th32ProcessID != 0)
        process_ids_.push_back(entry.th32ProcessID);
    } while (Process32Next(snapshot, &entry));
  }
  CloseHandle(snapshot);
  if (process_ids_.empty())
    throw std::runtime_error("The process snapshot was empty.");
  std::sort(process_ids_.begin(), process_ids_.end());
  std::mt19937 random(options.seed);
  std::mt19937 selection_random(options.seed);
  std::shuffle(process_ids_.begin(), process_ids_.end(), selection_random);
  if (options.processes != 0 && options.processes < process_ids_.size())
    process_ids_.resize(options.processes);

  std::set<std::size_t> used_paths;
  for (std::size_t index = 0; index < options.items; ++index) {
    const auto path_index = index < (std::min)(options.items, options.unique_paths) ? index : random() % options.unique_paths;
    used_paths.insert(path_index);
    const std::wstring path = L"\\Device\\HarddiskVolume42\\SsdReadyBenchmark\\bucket_" + std::to_wstring(path_index / 128) + L"\\file_" +
                              std::to_wstring(path_index) + L".dat";
    const std::size_t record_bytes =
        ((offsetof(LOG_RECORD, Name) + (path.size() + 1) * sizeof(WCHAR) + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR)) * sizeof(ULONG_PTR);
    if (record_bytes > MAX_LOG_RECORD_LENGTH)
      throw std::runtime_error("Generated path exceeds the driver record limit.");
    if (batches_.empty() || batches_.back().bytes + record_bytes > MiniSpyManager::kBufferSize) {
      batches_.emplace_back();
      batches_.back().storage.resize(MiniSpyManager::kBufferSize / sizeof(ULONG_PTR));
    }
    auto& batch = batches_.back();
    auto* record = reinterpret_cast<PLOG_RECORD>(reinterpret_cast<char*>(batch.storage.data()) + batch.bytes);
    std::memset(record, 0, record_bytes);
    record->Length = static_cast<ULONG>(record_bytes);
    record->SequenceNumber = random();
    record->RecordType = RECORD_TYPE_NORMAL;
    record->Data.ProcessId = process_ids_[random() % process_ids_.size()];
    record->Data.Status = static_cast<LONG>(random());
    record->Data.IrpFlags = random() & ~MiniSpyManager::kInputOperation;
    record->Data.Flags = random();
    record->Data.CallbackMajorId = 4;  // IRP_MJ_WRITE: each generated record must be accepted.
    record->Data.CallbackMinorId = 0;
    record->Data.WriteDataLength = 1 + random() % (1024 * 1024);
    record->Data.ReadDataLength = random() % (1024 * 1024);
    std::memcpy(record->Name, path.c_str(), (path.size() + 1) * sizeof(WCHAR));
    batch.bytes += static_cast<DWORD>(record_bytes);
    encoded_bytes_ += record_bytes;
    written_bytes_ += record->Data.WriteDataLength;
    rounded_bytes_ += ((static_cast<DWORD64>(record->Data.WriteDataLength) + 4095) / 4096) * 4096;
    read_bytes_ += record->Data.ReadDataLength;
  }
  path_count_ = used_paths.size();
  benchmark::AddCustomContext("process_snapshot_count", std::to_string(process_ids_.size()));
  benchmark::AddCustomContext("seed", std::to_string(options.seed));
  benchmark::AddCustomContext("cache", options.warm_cache ? "warm" : "cold");
  benchmark::AddCustomContext("items", std::to_string(options.items));
  benchmark::AddCustomContext("unique_paths", std::to_string(path_count_));

  g_application_core = new ApplicationCore(ApplicationCore::StartupMode::kHeadless);
  wchar_t windows_directory[MAX_PATH]{};
  if (!GetWindowsDirectory(windows_directory, MAX_PATH)) {
    delete g_application_core;
    g_application_core = nullptr;
    throw std::runtime_error("Cannot determine the Windows drive.");
  }
  auto volumes = std::make_unique<DriveInfoList>();
  auto& drive = volumes->emplace_back();
  drive.drive_letter[0] = windows_directory[0];
  drive.volume_name = L"\\Device\\HarddiskVolume42";
  manager().UpdateVolumes(std::move(volumes));
  manager().SetLogStatus(TRUE);
}

Workload::~Workload() {
  Reset();
  delete g_application_core;
  g_application_core = nullptr;
}

void Workload::ClearQueue() {
  while (manager().GetNextBatchToProcess()) {}
}

void Workload::Reset() {
  ClearQueue();
  manager().ClearData(FALSE);
  // Apply the producer-owned reset outside the timed cold lookup.
  manager().MaintainProcessNameCache();
}

void Workload::WarmProcessNames() {
  for (DWORD id : process_ids_) {
    CString name;
    manager().ResolveProcessById(id, name);
  }
}

std::size_t Workload::Retrieve() {
  std::size_t accepted = 0;
  for (auto& batch : batches_)
    accepted += manager().RetrieveLogRecordsBatch(batch.storage.data(), batch.bytes);
  return accepted;
}

std::size_t Workload::Process() {
  const auto initial_operations = manager().GetTotalOperations();
  while (manager().ProcessNextLogRecord()) {}
  return static_cast<std::size_t>(manager().GetTotalOperations() - initial_operations);
}

bool Workload::CheckRetrieval(std::size_t count, DWORD64 initial_reads) const {
  return count == options_.items && manager().GetUnprocessedItemsCount() == options_.items &&
         manager().GetTotalRead() - initial_reads == read_bytes_;
}

bool Workload::CheckProcessing(std::size_t count, DWORD64 initial_writes, DWORD64 initial_rounded, DWORD64 initial_operations) const {
  const auto globals = manager().GetProcessStatistics();
  const auto snapshot = manager().GetProcessStatistics(manager().GetRootFolder(), true);
  DWORD64 linked_writes = 0;
  for (const auto& process : snapshot.processes) {
    if (process.process_index >= globals.size() || globals[process.process_index].written_bytes != process.written_bytes)
      return false;
    linked_writes += process.written_bytes;
  }
  return manager().GetRootFolder()->GetWriteSizeTotal() == manager().GetTotalWritten() && linked_writes == manager().GetTotalWritten() &&
         manager().GetItemCount() == path_count_ + (path_count_ + 127) / 128 + 2 && count == options_.items &&
         manager().GetUnprocessedItemsCount() == 0 && manager().GetTotalWritten() - initial_writes == written_bytes_ &&
         manager().GetRoundedWritten() - initial_rounded == rounded_bytes_ &&
         manager().GetTotalOperations() - initial_operations == options_.items;
}

void Workload::SetCounters(benchmark::State& state, bool retrieval) const {
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(options_.items));
  // Throughput here is encoded driver bytes for retrieval, logical write bytes for processing.
  const auto bytes = retrieval ? encoded_bytes_ : written_bytes_;
  if (state.iterations() <= (std::numeric_limits<std::int64_t>::max)() / static_cast<std::int64_t>(bytes))
    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
  state.counters["events"] = static_cast<double>(options_.items);
  state.counters["paths"] = static_cast<double>(path_count_);
  state.counters["processes"] = static_cast<double>(process_ids_.size());
  state.counters["batches"] = static_cast<double>(batches_.size());
}

void Fail(benchmark::State& state, const char* message) {
  benchmark_failed = true;
  state.SkipWithError(message);
}

int Run(int argc, char** argv, const char* name, void (*function)(benchmark::State&, Workload&, const Options&)) {
  try {
    const auto options = ParseOptions(&argc, argv);
    if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0))
      throw std::runtime_error("Cannot initialize MFC.");
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
      return 1;
    Workload workload(options);
    benchmark::RegisterBenchmark(name,
                                 [function, options, &workload](benchmark::State& state) {
                                   function(state, workload, options);
                                 })
        ->Unit(benchmark::kMillisecond)
        ->UseRealTime();
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return benchmark_failed ? 1 : 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
}  // namespace ssdready_benchmark
