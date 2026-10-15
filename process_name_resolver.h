#pragma once

#include <afxstr.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_map>

// Cache access belongs to the retrieval thread. RequestClear is thread-safe.
class ProcessNameResolver {
 public:

  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Snapshot = std::unordered_map<DWORD, CString>;

  struct HandleCloser {
    void operator()(void* handle) const {
      CloseHandle(handle);
    }
  };

  using ProcessHandle = std::unique_ptr<void, HandleCloser>;

  struct ProcessInfo {
    CString name;
    ProcessHandle handle;
  };

  // Separate OS queries from cache policy so tests can model exit and PID reuse.
  class Backend {
   public:

    virtual ~Backend() = default;
    virtual ProcessInfo QueryProcess(DWORD id) = 0;
    virtual Snapshot QuerySnapshot() = 0;
    virtual bool HasExited(HANDLE process) = 0;
  };

  ProcessNameResolver();
  explicit ProcessNameResolver(Backend& backend);

  void RequestClear() {
    clear_requested_.store(true, std::memory_order_release);
  }

  // Call serially on the retrieval thread, including while monitoring is idle.
  void Maintain(TimePoint now = Clock::now());
  void Resolve(DWORD id, CString& name, TimePoint now = Clock::now());

 private:

  friend class ProcessNameResolverTestPeer;

  struct Entry {
    ProcessInfo process;
    TimePoint last_used;
    TimePoint retry_at;
  };

  Backend& backend_;
  std::atomic_bool clear_requested_{false};
  std::unordered_map<DWORD, Entry> processes_;
  Snapshot snapshot_;
  TimePoint snapshot_retry_at_{};
  TimePoint next_cleanup_{};
};
