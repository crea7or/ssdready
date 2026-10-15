#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "drive_info.h"
#include "folder_tree_item.h"
#include "minispy.h"
#include "pending_write_event.h"
#include "process_name_resolver.h"
#include "process_statistics.h"

// Version 1 persists these seven uint64 fields in this exact order.
struct MonitoringStatistics {
  std::uint64_t operations = 0;
  std::uint64_t items = 0;
  std::uint64_t read_bytes = 0;
  std::uint64_t written_bytes = 0;
  std::uint64_t rounded_written_bytes = 0;
  std::uint64_t last_time = 0;
  std::uint64_t monitored_seconds = 0;
};

static_assert(sizeof(MonitoringStatistics) == 56, "Preserve version 1 manager statistics");

class MiniSpyManager {
 public:

  using EventPtr = std::unique_ptr<PendingWriteEvent>;
  using EventBatch = std::vector<EventPtr>;
  using EventBatchPtr = std::unique_ptr<EventBatch>;
  static constexpr DWORD kBufferSize = 262144;
  static constexpr ULONG kInputOperation = 0x40;

  MiniSpyManager();
  ~MiniSpyManager();
  MiniSpyManager(const MiniSpyManager&) = delete;
  MiniSpyManager& operator=(const MiniSpyManager&) = delete;

  // Only the retrieval worker consumes published snapshots and mutates its caches.
  bool UpdateVolumes(DriveInfoListPtr new_volumes);
  void AttachTo(const CString& drives);
  BOOL AttachDeviceToFilter(DriveInfo& drive);
  BOOL DetachDeviceFromFilter(DriveInfo& drive);
  void ClearData(BOOL clear_tree = TRUE);
  static DWORD WINAPI ProcessLogRecords(LPVOID parameter);
  static DWORD WINAPI RetrieveLogRecords(LPVOID parameter);
  // Driver-format buffers must be aligned. One producer calls this work unit.
  std::size_t RetrieveLogRecordsBatch(void* buffer, DWORD bytes_returned);
  bool ProcessNextLogRecord(bool wait_for_batch = false);
  BOOL Install();
  BOOL Uninstall();
  BOOL SaveMonitoredItems();
  BOOL LoadMonitoredItems();

  [[nodiscard]] std::unique_lock<std::mutex> LockData() {
    return std::unique_lock<std::mutex>(data_mutex_);
  }

  [[nodiscard]] std::unique_lock<std::mutex> LockDrives() {
    return std::unique_lock<std::mutex>(drives_mutex_);
  }

  FolderTreeItem* GetRootFolder() {
    return &root_folder_;
  }

  void VolumeToDriveName(LPCWSTR file_name, CString& drive_name);
  // Tree pointers/counters require LockData or a single-threaded caller. UI uses
  // owned snapshots instead of reading counters during worker updates.
  TreeItem* GetTreeItemFromPath(const CString& path);

  std::uint64_t GetTotalWritten() const {
    return statistics_.written_bytes;
  }

  std::uint64_t GetRoundedWritten() const {
    return statistics_.rounded_written_bytes;
  }

  std::uint64_t GetTotalOperations() const {
    return statistics_.operations;
  }

  std::uint64_t GetItemCount() const {
    return statistics_.items;
  }

  std::uint64_t GetTotalRead() const {
    return read_bytes_.load(std::memory_order_relaxed);
  }

  MonitoringStatistics GetStatistics();
  // Approximate recent processing throughput, refreshed at most once a second.
  double GetProcessedItemsPerSecond();
  // Highest sampled throughput during this manager's lifetime; not persisted.
  double GetMaxProcessedItemsPerSecond();

  void AddFileCount() {
    ++statistics_.items;
  }

  BOOL GetCleanUpStatus() const {
    return stopping_.load(std::memory_order_relaxed);
  }

  void InitiateCleanUp();
  void SetLogStatus(bool enabled);

  bool GetLogStatus() const {
    return logging_enabled_.load(std::memory_order_relaxed);
  }

  std::vector<ProcessStatistics> GetProcessStatistics();
  ProcessStatisticsSnapshot GetProcessStatistics(TreeItem* item, bool include_children);

  void MaintainProcessNameCache() {
    process_name_resolver_.Maintain();
  }

  void ResolveProcessById(DWORD id, CString& process_name);
  EventBatchPtr GetNextBatchToProcess(bool wait_for_batch = false);
  std::size_t GetUnprocessedItemsCount();

 protected:

  // Allows the resolver regression probe to verify cache lifetime.
  ProcessNameResolver process_name_resolver_;

 private:

  void StopWorkers();
  void UpdateMonitoringTime();  // data_mutex_ is held by the caller.
  void ResetProcessingRate();   // data_mutex_ is held by the caller.
  bool WaitForStop();
  static bool TranslateFileTag(wchar_t* name, std::size_t available);
  void AddTotalWritten(std::uint64_t bytes);

  HANDLE filter_port_ = nullptr;
  std::array<HANDLE, 2> threads_{};
  std::atomic<bool> logging_enabled_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<std::uint64_t> read_bytes_{0};
  bool driver_started_ = false;
  bool installation_attempted_ = false;
  bool history_save_allowed_ = true;
  MonitoringStatistics statistics_;
  std::chrono::steady_clock::time_point last_monitored_ = std::chrono::steady_clock::now();
  std::chrono::steady_clock::duration time_remainder_{};
  std::chrono::steady_clock::time_point rate_sample_time_ = std::chrono::steady_clock::now();
  std::uint64_t rate_sample_operations_ = 0;
  double processed_items_per_second_ = 0;
  double max_processed_items_per_second_ = 0;
  std::mutex data_mutex_;
  std::mutex drives_mutex_;
  std::unordered_map<std::wstring, TreeItem*> items_by_path_;
  ProcessStatisticsStore process_statistics_;
  std::atomic<DriveInfoList*> new_volumes_ptr_{nullptr};
  DriveInfoListPtr volumes_ptr_;
  std::mutex queue_mutex_;
  std::condition_variable unprocessed_batches_ready_;
  std::condition_variable stop_ready_;
  std::deque<EventBatchPtr> pending_batches_;
  std::size_t queued_item_count_ = 0;
  FolderTreeItem root_folder_;
};
