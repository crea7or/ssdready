// clang-format off
#include "pch.h"
// clang-format on
#include "mini_spy_manager.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <new>
#include <string_view>
#include <utility>

#include "buffered_file.h"
#include "history_format.h"
#include "monitoring_tree_ctrl.h"

namespace {
constexpr UCHAR kWriteMajorFunction = 4;
constexpr std::size_t kProcessingChunkSize = 64;

void LogFilterError(LPCWSTR operation, HRESULT result) {
  AppLog::Get().Add(L"{} failed (HRESULT {:08x})", operation, static_cast<ULONG>(result));
}
}  // namespace

MiniSpyManager::MiniSpyManager() : volumes_ptr_(GetVolumes()) {
  root_folder_.SetTreeItem(TVI_ROOT);
  statistics_.last_time = static_cast<std::uint64_t>(time(nullptr));
}

MiniSpyManager::~MiniSpyManager() {
  StopWorkers();
  delete new_volumes_ptr_.exchange(nullptr, std::memory_order_acquire);
}

bool MiniSpyManager::UpdateVolumes(DriveInfoListPtr new_volumes) {
  if (!new_volumes)
    return false;

  // The producer and consumer each acquire sole ownership of the pointer they
  // remove. A paused consumer must not cause newer device mappings to be lost.
  delete new_volumes_ptr_.exchange(new_volumes.release(), std::memory_order_acq_rel);
  return true;
}

void MiniSpyManager::ClearData(BOOL clear_tree) {
  auto lock = LockData();
  if (clear_tree && Core() && Core()->GetTreeControl())
    Core()->GetTreeControl()->DeleteAllItems();
  root_folder_.Clear();
  statistics_ = {};
  statistics_.last_time = static_cast<std::uint64_t>(time(nullptr));
  last_monitored_ = std::chrono::steady_clock::now();
  time_remainder_ = {};
  ResetProcessingRate();
  read_bytes_.store(0, std::memory_order_relaxed);
  items_by_path_.clear();
  process_name_resolver_.RequestClear();
  process_statistics_.Clear();
  if (clear_tree) {
    history_save_allowed_ = true;
    AppLog::Get().Add(L"Collected monitoring data cleared.");
  }
}

void MiniSpyManager::AddTotalWritten(std::uint64_t bytes) {
  statistics_.written_bytes += bytes;
  statistics_.rounded_written_bytes += (bytes + 4095) & ~std::uint64_t(4095);
}

void MiniSpyManager::UpdateMonitoringTime() {
  if (!GetLogStatus())
    return;
  const auto now = std::chrono::steady_clock::now();
  time_remainder_ += now - last_monitored_;
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_remainder_);
  statistics_.monitored_seconds += seconds.count();
  statistics_.last_time = static_cast<std::uint64_t>(time(nullptr));
  time_remainder_ -= seconds;
  last_monitored_ = now;
}

void MiniSpyManager::SetLogStatus(bool enabled) {
  auto lock = LockData();
  const bool changed = enabled != GetLogStatus();
  UpdateMonitoringTime();
  if (enabled && !GetLogStatus()) {
    last_monitored_ = std::chrono::steady_clock::now();
    statistics_.last_time = static_cast<std::uint64_t>(time(nullptr));
  }
  logging_enabled_.store(enabled, std::memory_order_relaxed);
  if (changed) {
    ResetProcessingRate();
    if (enabled && Core()) {
      const auto* settings = Core()->GetInterfaceSettings();
      AppLog::Get().Add(L"Monitoring started. File details: {}; process attribution: {}.",
                        settings->do_not_log_files_.load(std::memory_order_relaxed) ? L"disabled" : L"enabled",
                        settings->collect_process_information_.load(std::memory_order_relaxed) ? L"enabled" : L"disabled");
    } else if (enabled) {
      AppLog::Get().Add(L"Monitoring started.");
    } else {
      AppLog::Get().Add(L"Monitoring paused; already queued events will finish processing.");
    }
  }
}

MonitoringStatistics MiniSpyManager::GetStatistics() {
  auto lock = LockData();
  UpdateMonitoringTime();
  auto result = statistics_;
  result.read_bytes = GetTotalRead();
  return result;
}

void MiniSpyManager::ResetProcessingRate() {
  rate_sample_time_ = std::chrono::steady_clock::now();
  rate_sample_operations_ = statistics_.operations;
  processed_items_per_second_ = 0;
}

double MiniSpyManager::GetProcessedItemsPerSecond() {
  auto lock = LockData();
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = now - rate_sample_time_;
  if (elapsed >= std::chrono::seconds(1)) {
    // operations counts completed events under this same mutex. Historical
    // totals are excluded by resetting the baseline after loading or clearing.
    const auto completed = statistics_.operations - rate_sample_operations_;
    processed_items_per_second_ = static_cast<double>(completed) / std::chrono::duration<double>(elapsed).count();
    if (processed_items_per_second_ > max_processed_items_per_second_)
      max_processed_items_per_second_ = processed_items_per_second_;
    rate_sample_operations_ = statistics_.operations;
    rate_sample_time_ = now;
  }
  return processed_items_per_second_;
}

double MiniSpyManager::GetMaxProcessedItemsPerSecond() {
  auto lock = LockData();
  return max_processed_items_per_second_;
}

BOOL MiniSpyManager::SaveMonitoredItems() try {
  const auto started = std::chrono::steady_clock::now();
  auto lock = LockData();
  if (!history_save_allowed_) {
    AppLog::Get().Add(L"History save skipped to protect an unreadable history file. Clear collected data to replace it.");
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
  CString path = Core()->GetRuntimeSettings()->user_data_folder_;
  if (path.IsEmpty()) {
    AppLog::Get().Add(L"History save unavailable: the application data folder is not configured.");
    SetLastError(ERROR_PATH_NOT_FOUND);
    return FALSE;
  }
  string_utils::EnsureTrailingSlash(path);
  path += L"Monitored.Files";
  const CString temporary = path + L".tmp";

  struct TemporaryFile {
    LPCWSTR path;
    bool created = false;

    ~TemporaryFile() {
      if (created) {
        const DWORD error = GetLastError();
        DeleteFileW(path);
        SetLastError(error);
      }
    }
  } cleanup{temporary};

  // Version 2 buffers and compresses its own bounded blocks.
  File file(FALSE, FALSE);
  if (!file.Open(temporary, File::kModeWrite | File::kCreate)) {
    const DWORD error = GetLastError();
    const CString context = L"Create temporary monitoring history: " + temporary;
    SystemError report(context, error);
    return FALSE;
  }
  cleanup.created = true;
  UpdateMonitoringTime();
  auto statistics = statistics_;
  statistics.read_bytes = GetTotalRead();
  BOOL saved = history_format::Save(file, statistics, process_statistics_, root_folder_);
  if (saved)
    saved = file.Flush();
  DWORD error = saved ? ERROR_SUCCESS : GetLastError();
  if (!file.Close()) {
    saved = FALSE;
    error = GetLastError();
  }
  if (saved && !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    saved = FALSE;
    error = GetLastError();
  }
  if (!saved) {
    const CString context = L"Save monitoring history (previous file retained): " + path;
    SystemError report(context, error);
  } else {
    AppLog::Get().Add(L"Saved monitoring history: {} ({} tree items, {} operations, {:.3f} seconds).",
                      path,
                      statistics.items,
                      statistics.operations,
                      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
  }
  return saved;
} catch (CMemoryException* error) {

  error->Delete();
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return FALSE;
} catch (const std::bad_alloc&) {
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return FALSE;
}

BOOL MiniSpyManager::LoadMonitoredItems() try {
  const auto started = std::chrono::steady_clock::now();
  auto lock = LockData();
  CString path = Core()->GetRuntimeSettings()->user_data_folder_;
  if (path.IsEmpty()) {
    AppLog::Get().Add(L"History load unavailable: the application data folder is not configured.");
    SetLastError(ERROR_PATH_NOT_FOUND);
    return FALSE;
  }
  // Keep the old file protected even if a malformed count exhausts memory
  // during parsing, before the complete temporary model can be committed.
  const bool saving_was_allowed = history_save_allowed_;
  history_save_allowed_ = false;
  string_utils::EnsureTrailingSlash(path);
  path += L"Monitored.Files";
  BufferedFile file(FALSE, FALSE);
  if (!file.Open(path, File::kModeRead)) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      history_save_allowed_ = saving_was_allowed;
      AppLog::Get().Add(L"No saved monitoring history found: {}", path);
    } else {
      const CString context = L"Open monitoring history: " + path;
      SystemError report(context, error);
    }
    SetLastError(error);
    return FALSE;
  }
  // Parse into owned temporary state. Failure must not mix new process indices
  // with the previous tree, or leave dangling pointers in the full-path cache.
  MonitoringStatistics statistics;
  ProcessStatisticsStore processes;
  FolderTreeItem root;
  const BOOL loaded = history_format::Load(file, statistics, processes, root);
  const DWORD error = loaded ? ERROR_SUCCESS : GetLastError();
  const BOOL closed = file.Close();
  if (!closed || !loaded) {
    const DWORD failure = loaded ? GetLastError() : error;
    history_save_allowed_ = false;
    const CString context = L"Load monitoring history (existing data retained; saving disabled): " + path;
    SystemError report(context, failure);
    return FALSE;
  }
  if (Core()->GetTreeControl())
    Core()->GetTreeControl()->DeleteAllItems();
  root_folder_.TakeContents(std::move(root));
  process_statistics_ = std::move(processes);
  statistics_ = statistics;
  statistics_.items = root_folder_.CountFiles();
  ResetProcessingRate();
  read_bytes_.store(statistics.read_bytes, std::memory_order_relaxed);
  items_by_path_.clear();
  process_name_resolver_.RequestClear();
  last_monitored_ = std::chrono::steady_clock::now();
  time_remainder_ = {};
  history_save_allowed_ = true;
  AppLog::Get().Add(L"Loaded monitoring history: {} ({} tree items, {} operations, {:.3f} seconds).",
                    path,
                    statistics.items,
                    statistics.operations,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
  return TRUE;
} catch (CMemoryException* error) {

  error->Delete();
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return FALSE;
} catch (const std::bad_alloc&) {
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return FALSE;
}

BOOL MiniSpyManager::Install() {
  if (installation_attempted_ && !GetCleanUpStatus())
    return threads_[0] && threads_[1];
  const bool load_history = !installation_attempted_;
  StopWorkers();
  if (driver_started_) {
    Core()->CloseSsdReadyDriver();
    driver_started_ = false;
  }
  installation_attempted_ = true;
  AppLog::Get().Add(L"Preparing monitoring workers and driver connection.");
  {
    auto lock = std::unique_lock<std::mutex>(queue_mutex_);
    stopping_.store(false, std::memory_order_relaxed);
  }
  // Retrying a failed worker session must retain writes already collected in
  // memory instead of replacing them with the previous startup history.
  if (load_history) {
    if (!LoadMonitoredItems() && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
      AppLog::Get().Add(L"Previous history could not be loaded; it will be left unchanged.");
    if (Core()->GetTreeControl()) {
      MonitoringTreeCtrl::UpdateBatch update(*Core()->GetTreeControl());
      root_folder_.AddToTreeCtrl();
    }
  }
  if (!Core()->OpenSsdReadyDriver()) {
    AppLog::Get().Add(L"Could not start SsdSpy driver; monitoring is unavailable");
    StopWorkers();
    return FALSE;
  }
  driver_started_ = true;
  const HRESULT result = FilterConnectCommunicationPort(MINISPY_PORT_NAME, 0, nullptr, 0, nullptr, &filter_port_);
  if (SUCCEEDED(result)) {
    AppLog::Get().Add(L"Connected to the SsdSpy communication port.");
    // Start the consumer before the producer so no unconsumed batches build up
    // if consumer creation fails.
    threads_[1] = CreateThread(nullptr, 0, ProcessLogRecords, this, 0, nullptr);
    if (threads_[1])
      threads_[0] = CreateThread(nullptr, 0, RetrieveLogRecords, this, 0, nullptr);
    if (threads_[0] && threads_[1]) {
      AppLog::Get().Add(L"Retrieval and processing worker threads started.");
      return TRUE;
    }
    SystemError report(threads_[1] ? L"Create retrieval worker" : L"Create processing worker");
  } else {
    LogFilterError(L"FilterConnectCommunicationPort", result);
  }
  StopWorkers();
  Core()->CloseSsdReadyDriver();
  driver_started_ = false;
  return FALSE;
}

void MiniSpyManager::StopWorkers() {
  const bool had_workers = threads_[0] || threads_[1];
  InitiateCleanUp();
  for (auto& thread : threads_) {
    if (!thread)
      continue;
    // Retry cancellation to cover the race between a worker checking its stop
    // flag and entering synchronous I/O. Never free its state before it exits.
    do {
      CancelSynchronousIo(thread);
    } while (WaitForSingleObject(thread, 50) == WAIT_TIMEOUT);
    CloseHandle(std::exchange(thread, nullptr));
  }
  if (filter_port_ && filter_port_ != INVALID_HANDLE_VALUE)
    CloseHandle(filter_port_);
  filter_port_ = nullptr;
  if (had_workers) {
    const auto totals = GetStatistics();
    AppLog::Get().Add(L"Monitoring workers stopped. Totals: {} operations, {} bytes written; {} events still queued.",
                      totals.operations,
                      totals.written_bytes,
                      GetUnprocessedItemsCount());
  }
}

BOOL MiniSpyManager::Uninstall() {
  StopWorkers();
  if (!installation_attempted_)
    return TRUE;
  installation_attempted_ = false;
  BOOL stopped = TRUE;
  if (driver_started_) {
    stopped = Core()->CloseSsdReadyDriver();
    driver_started_ = false;
  }
  const BOOL saved = SaveMonitoredItems();
  if (!saved)
    AppLog::Get().Add(L"Could not save monitoring history; previous saved history was retained.");
  return stopped && saved;
}

bool MiniSpyManager::ProcessNextLogRecord(bool wait_for_batch) {
  auto batch = GetNextBatchToProcess(wait_for_batch);
  if (!batch)
    return false;
  const auto* settings = Core()->GetInterfaceSettings();
  const bool log_files = !settings->do_not_log_files_.load(std::memory_order_relaxed);
  const bool collect_processes = settings->collect_process_information_.load(std::memory_order_relaxed);
  // Amortize locking while allowing the UI to obtain snapshots between chunks.
  for (std::size_t begin = 0; begin < batch->size(); begin += kProcessingChunkSize) {
    auto lock = LockData();
    const auto end = (std::min)(begin + kProcessingChunkSize, batch->size());
    for (std::size_t index = begin; index < end; ++index) {
      const auto& item = (*batch)[index];
      if (!log_files || !item.file_name.IsEmpty()) {
        if (log_files) {
          auto* tree_item = GetTreeItemFromPath(item.file_name);
          if (collect_processes) {
            const auto process_index = process_statistics_.AddWrite(item.process_name, item.written_bytes);
            tree_item->AddWrite(item.written_bytes, process_index);
          } else {
            tree_item->AddWrite(item.written_bytes);
          }
        }
        AddTotalWritten(item.written_bytes);
      }
      ++statistics_.operations;
    }
  }
  return true;
}

DWORD WINAPI MiniSpyManager::ProcessLogRecords(LPVOID parameter) {
  auto* manager = static_cast<MiniSpyManager*>(parameter);
  try {
    while (manager->ProcessNextLogRecord(true)) {}
    return ERROR_SUCCESS;
  } catch (CException* error) {
    error->Delete();
  } catch (...) {}
  manager->InitiateCleanUp();
  AppLog::Get().Add(L"Processing worker failed; monitoring stopped.");
  return ERROR_UNHANDLED_EXCEPTION;
}

void MiniSpyManager::ResolveProcessById(DWORD id, CString& process_name) {
  process_name_resolver_.Resolve(id, process_name);
}

void MiniSpyManager::VolumeToDriveName(LPCWSTR file_name, CString& drive_name) {
  if (auto* pending = new_volumes_ptr_.exchange(nullptr, std::memory_order_acquire)) {
    volumes_ptr_.reset(pending);
  }

  if (!volumes_ptr_ || !file_name || *file_name == L'\0') {
    drive_name = L"#:\\unknown.file";
    return;
  }

  const std::wstring_view file_path(file_name);
  for (const auto& drive : *volumes_ptr_) {
    const auto& volume_name = drive.volume_name;
    if (volume_name.empty() || !drive.HasDriveLetter() || file_path.compare(0, volume_name.size(), volume_name) != 0) {
      continue;
    }
    // Match a complete volume name, not a prefix of another volume number.
    if (file_path.size() > volume_name.size() && file_path[volume_name.size()] != L'\\') {
      continue;
    }
    drive_name = drive.drive_letter.data();
    drive_name += file_name + volume_name.size();
    return;
  }

  drive_name = L"#:";
  drive_name += file_name;
}

std::size_t MiniSpyManager::RetrieveLogRecordsBatch(void* buffer, DWORD bytes_returned) {
  if (bytes_returned == 0 || GetCleanUpStatus())
    return 0;
  if (!buffer || reinterpret_cast<std::uintptr_t>(buffer) % sizeof(ULONG_PTR) != 0) {
    SetLastError(ERROR_INVALID_DATA);
    return 0;
  }
  MaintainProcessNameCache();
  auto items = std::make_unique<EventBatch>();
  items->reserve(bytes_returned / (offsetof(LOG_RECORD, Name) + sizeof(wchar_t)));
  const auto* settings = Core()->GetInterfaceSettings();
  const bool collect_processes = settings->collect_process_information_.load(std::memory_order_relaxed);
  const bool log_files = !settings->do_not_log_files_.load(std::memory_order_relaxed);
  const bool logging = GetLogStatus();
  std::uint64_t read_bytes = 0;
  std::size_t used = 0;
  while (used < bytes_returned) {
    constexpr auto name_offset = offsetof(LOG_RECORD, Name);
    const auto remaining = bytes_returned - used;
    if (remaining < name_offset + sizeof(wchar_t))
      break;
    const auto* encoded = static_cast<const char*>(buffer) + used;
    ULONG length = 0;
    ULONG type = 0;
    std::memcpy(&length, encoded + offsetof(LOG_RECORD, Length), sizeof(length));
    std::memcpy(&type, encoded + offsetof(LOG_RECORD, RecordType), sizeof(type));
    if (length < name_offset + sizeof(wchar_t) || length > remaining || length % sizeof(ULONG_PTR) != 0) {
      AppLog::Get().Add(L"Invalid driver record length");
      break;
    }
    used += length;
    auto* name = reinterpret_cast<wchar_t*>(static_cast<char*>(buffer) + used - length + name_offset);
    if ((type & RECORD_TYPE_FILETAG) && !TranslateFileTag(name, length - name_offset))
      continue;
    const auto name_characters = (length - name_offset) / sizeof(wchar_t);
    const auto* end = std::wmemchr(name, L'\0', name_characters);
    if (!end) {
      AppLog::Get().Add(L"Unterminated driver record name");
      continue;
    }
    RECORD_DATA data;
    std::memcpy(&data, encoded + offsetof(LOG_RECORD, Data), sizeof(data));
    if (logging && data.CallbackMajorId == kWriteMajorFunction &&
        ((data.IrpFlags & kInputOperation) == 0 || std::wmemchr(name, L'$', end - name))) {
      auto& item = items->emplace_back();
      if (collect_processes)
        ResolveProcessById(static_cast<DWORD>(data.ProcessId), item.process_name);
      if (log_files)
        VolumeToDriveName(name, item.file_name);
      item.written_bytes = data.WriteDataLength;
      read_bytes += data.ReadDataLength;
    }
    if (type & (RECORD_TYPE_FLAG_OUT_OF_MEMORY | RECORD_TYPE_FLAG_EXCEED_MEMORY_ALLOWANCE)) {
      ++driver_warning_records_;
      const auto now = std::chrono::steady_clock::now();
      if (now - last_driver_warning_ >= std::chrono::seconds(5)) {
        AppLog::Get().Add(
            L"Driver memory pressure: {} flagged records since the previous warning (flags {:08x}).", driver_warning_records_, type);
        last_driver_warning_ = now;
        driver_warning_records_ = 0;
      }
    }
  }
  const auto accepted = items->size();
  if (accepted == 0)
    return 0;
  {
    auto lock = std::unique_lock<std::mutex>(queue_mutex_);
    if (GetCleanUpStatus())
      return 0;
    pending_batches_.push_back(std::move(items));
    queued_item_count_ += accepted;
    read_bytes_.fetch_add(read_bytes, std::memory_order_relaxed);
  }
  unprocessed_batches_ready_.notify_one();
  return accepted;
}

void MiniSpyManager::InitiateCleanUp() {
  {
    // Update the wait predicate under its mutex to prevent a missed wakeup.
    auto lock = std::unique_lock<std::mutex>(queue_mutex_);
    stopping_.store(true, std::memory_order_relaxed);
  }
  unprocessed_batches_ready_.notify_all();
  stop_ready_.notify_all();
}

MiniSpyManager::EventBatchPtr MiniSpyManager::GetNextBatchToProcess(bool wait_for_batch) {
  auto lock = std::unique_lock<std::mutex>(queue_mutex_);
  if (wait_for_batch) {
    unprocessed_batches_ready_.wait(lock, [this] {
      return GetCleanUpStatus() || !pending_batches_.empty();
    });
    // Drain accepted batches before saving final monitoring totals.
  }
  if (!pending_batches_.empty()) {
    EventBatchPtr first_item = std::move(pending_batches_.front());
    pending_batches_.pop_front();
    queued_item_count_ -= first_item->size();
    return first_item;
  }
  return nullptr;
}

std::size_t MiniSpyManager::GetUnprocessedItemsCount() {
  auto lock = std::unique_lock<std::mutex>(queue_mutex_);
  return queued_item_count_;
}

bool MiniSpyManager::WaitForStop() {
  auto lock = std::unique_lock<std::mutex>(queue_mutex_);
  return stop_ready_.wait_for(lock, std::chrono::milliseconds(100), [this] {
    return GetCleanUpStatus();
  });
}

DWORD WINAPI MiniSpyManager::RetrieveLogRecords(LPVOID parameter) {
  auto* manager = static_cast<MiniSpyManager*>(parameter);
  try {
    // ULONG_PTR storage satisfies the driver's record-alignment requirement.
    std::vector<ULONG_PTR> buffer(kBufferSize / sizeof(ULONG_PTR));

    struct Command {
      MINISPY_COMMAND command;
      ULONG reserved;
    };

    static_assert(sizeof(Command) == sizeof(COMMAND_MESSAGE), "Preserve driver command layout");
    Command command{GetMiniSpyLog, 0};
    DWORD errors = 0;
    while (!manager->GetCleanUpStatus()) {
      manager->MaintainProcessNameCache();
      DWORD received = 0;
      const HRESULT result = FilterSendMessage(manager->filter_port_, &command, sizeof(command), buffer.data(), kBufferSize, &received);
      if (manager->GetCleanUpStatus())
        break;
      if (FAILED(result)) {
        if (result == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)) {
          LogFilterError(L"FilterSendMessage (driver handle closed unexpectedly; monitoring stopped)", result);
          break;
        }
        if (result != HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS)) {
          LogFilterError(L"FilterSendMessage", result);
          if (++errors > 100) {
            AppLog::Get().Add(L"Monitoring stopped after more than 100 driver communication failures.");
            break;
          }
        }
        if (manager->WaitForStop())
          break;
        continue;
      }
      if (errors) {
        AppLog::Get().Add(L"Driver communication recovered after {} failed attempts.", errors);
        errors = 0;
      }
      if (received > kBufferSize) {
        AppLog::Get().Add(L"Driver returned more data than the retrieval buffer can hold");
        break;
      }
      if (received)
        manager->RetrieveLogRecordsBatch(buffer.data(), received);
      else if (manager->WaitForStop())
        break;
    }
    manager->InitiateCleanUp();
    return ERROR_SUCCESS;
  } catch (CException* error) {
    error->Delete();
  } catch (...) {}
  manager->InitiateCleanUp();
  AppLog::Get().Add(L"Retrieval worker failed; monitoring stopped.");
  return ERROR_UNHANDLED_EXCEPTION;
}

std::vector<ProcessStatistics> MiniSpyManager::GetProcessStatistics() {
  auto lock = LockData();
  return process_statistics_.records();
}

ProcessStatisticsSnapshot MiniSpyManager::GetProcessStatistics(TreeItem* item, bool include_children) {
  auto lock = LockData();
  if (!item) {
    item = &root_folder_;
    include_children = true;
  }
  ProcessStatisticsSnapshot snapshot;
  snapshot.item_name = item->GetAlias();
  snapshot.written_bytes = include_children ? item->GetWriteSizeTotal() : item->GetWriteSize();
  const auto links = item->GetProcessWrites(include_children);
  snapshot.processes.reserve(links.size());
  for (const auto& link : links) {
    const auto* process = process_statistics_.Get(link.process_index);
    snapshot.processes.push_back({process ? process->name : CString(L"< unknown >"), link.written_bytes, link.process_index});
  }
  return snapshot;
}

TreeItem* MiniSpyManager::GetTreeItemFromPath(const CString& name) {
  const std::wstring_view path(name.GetString(), name.GetLength());
  const auto found = items_by_path_.find(path);
  if (found != items_by_path_.end())
    return found->second;
  // The driver has no directory flag. Infer ancestors structurally and promote
  // a previous leaf in place when a descendant arrives; never probe the disk.
  const BOOL is_file = !path.empty() && path.back() != L'\\' && path.back() != L':';
  CStringArray parts;
  string_utils::Split(name, L'\\', parts);
  auto* item = root_folder_.GetItemByPath(&parts, 0, is_file);
  constexpr std::size_t kMaxCachedPaths = 65536;
  if (items_by_path_.size() >= kMaxCachedPaths)
    items_by_path_.clear();
  items_by_path_.emplace(std::wstring(path), item);
  return item;
}

void MiniSpyManager::AttachTo(const CString& drives) {
  auto lock = LockDrives();
  auto volumes = GetVolumes();
  if (!volumes) {
    AppLog::Get().Add(L"Could not enumerate monitorable drives; no new attachments were requested.");
    return;
  }
  std::size_t selected = 0, attached = 0, failures = 0;
  for (auto& drive : *volumes) {
    if (drive.HasDriveLetter() && (drives.IsEmpty() || drives.Find(drive.drive_letter.data()) != -1)) {
      ++selected;
      if (drive.instances > 0 || AttachDeviceToFilter(drive))
        ++attached;
      else
        ++failures;
    }
  }
  AppLog::Get().Add(L"Drive monitoring: {} selected volumes, {} attached, {} attachment failures.", selected, attached, failures);
  if (selected == 0)
    AppLog::Get().Add(L"No eligible drives match the selection. Shadow copies and volumes without a drive letter are excluded.");
  UpdateVolumes(std::move(volumes));
}

BOOL MiniSpyManager::AttachDeviceToFilter(DriveInfo& drive) {
  WCHAR instance_name[INSTANCE_NAME_MAX_CHARS + 1]{};
  const HRESULT result = FilterAttach(kFilterName, drive.volume_name.c_str(), nullptr, sizeof(instance_name), instance_name);
  if (SUCCEEDED(result)) {
    ++drive.instances;
    Core()->GetLog()->Add(L"Attached to drive {} ({}), instance: {}.", drive.drive_letter.data(), drive.volume_name, instance_name);
  } else {
    Core()->GetLog()->Add(
        L"Could not attach to drive {} ({}): HRESULT {:08x}.", drive.drive_letter.data(), drive.volume_name, static_cast<ULONG>(result));
  }
  return SUCCEEDED(result);
}

BOOL MiniSpyManager::DetachDeviceFromFilter(DriveInfo& drive) {
  const HRESULT result = FilterDetach(kFilterName, drive.volume_name.c_str(), nullptr);
  if (SUCCEEDED(result)) {
    // A null instance name removes the highest matching instance.
    if (drive.instances > 0)
      --drive.instances;
    Core()->GetLog()->Add(L"Detached from drive {} ({}).", drive.drive_letter.data(), drive.volume_name);
  } else {
    Core()->GetLog()->Add(
        L"Could not detach from drive {} ({}): HRESULT {:08x}.", drive.drive_letter.data(), drive.volume_name, static_cast<ULONG>(result));
  }
  return SUCCEEDED(result);
}

bool MiniSpyManager::TranslateFileTag(wchar_t* name, std::size_t available) {
  struct MountPoint {
    ULONG tag;
    USHORT data_length;
    USHORT unparsed_length;
    USHORT substitute_offset;
    USHORT substitute_length;
    USHORT print_offset;
    USHORT print_length;
    WCHAR path[1];
  };

  constexpr auto path_offset = offsetof(MountPoint, path);
  if (available < path_offset)
    return false;
  const auto* tag = reinterpret_cast<const MountPoint*>(name);
  if (tag->tag != IO_REPARSE_TAG_MOUNT_POINT || tag->data_length < path_offset - 8 || tag->data_length > available - 8 ||
      tag->substitute_offset % sizeof(wchar_t) || tag->substitute_length % sizeof(wchar_t))
    return false;
  const std::size_t path_bytes = tag->data_length - (path_offset - 8);
  if (tag->substitute_offset > path_bytes || tag->substitute_length > path_bytes - tag->substitute_offset)
    return false;
  const auto length = tag->substitute_length;
  const auto* source = reinterpret_cast<const char*>(tag->path) + tag->substitute_offset;
  std::memmove(name, source, length);
  name[length / sizeof(wchar_t)] = L'\0';
  return true;
}
