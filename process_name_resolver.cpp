// clang-format off
#include "pch.h"
// clang-format on

#include "process_name_resolver.h"

#include <TlHelp32.h>

#include <cwchar>
#include <vector>

namespace {
constexpr auto kRetryInterval = std::chrono::seconds(1);
constexpr auto kCleanupInterval = std::chrono::seconds(30);
constexpr auto kIdleLifetime = std::chrono::hours(2);

class WindowsProcessBackend : public ProcessNameResolver::Backend {
 public:

  ProcessNameResolver::ProcessInfo QueryProcess(DWORD id) override {
    ProcessNameResolver::ProcessInfo result;
    result.handle.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, id));
    if (!result.handle)
      return result;

    wchar_t buffer[MAX_PATH];
    DWORD length = MAX_PATH;
    if (QueryFullProcessImageNameW(result.handle.get(), 0, buffer, &length)) {
      AssignBasename(buffer, result.name);
    } else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      // Allocate only for unusually long executable paths.
      std::vector<wchar_t> long_path(32768);
      length = static_cast<DWORD>(long_path.size());
      if (QueryFullProcessImageNameW(result.handle.get(), 0, long_path.data(), &length))
        AssignBasename(long_path.data(), result.name);
    }
    if (result.name.IsEmpty())
      result.handle.reset();
    return result;
  }

  ProcessNameResolver::Snapshot QuerySnapshot() override {
    ProcessNameResolver::Snapshot names;
    HANDLE raw_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (raw_snapshot == INVALID_HANDLE_VALUE)
      return names;
    ProcessNameResolver::ProcessHandle snapshot(raw_snapshot);
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.get(), &entry)) {
      do {
        names.emplace(entry.th32ProcessID, entry.szExeFile);
      } while (Process32NextW(snapshot.get(), &entry));
    }
    return names;
  }

  bool HasExited(HANDLE process) override {
    return WaitForSingleObject(process, 0) != WAIT_TIMEOUT;
  }

 private:

  static void AssignBasename(const wchar_t* path, CString& name) {
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    name = slash ? slash + 1 : path;
  }
};

WindowsProcessBackend& NativeBackend() {
  static WindowsProcessBackend backend;
  return backend;
}
}  // namespace

ProcessNameResolver::ProcessNameResolver() : ProcessNameResolver(NativeBackend()) {
}

ProcessNameResolver::ProcessNameResolver(Backend& backend) : backend_(backend) {
}

void ProcessNameResolver::Maintain(TimePoint now) {
  if (clear_requested_.load(std::memory_order_acquire) && clear_requested_.exchange(false, std::memory_order_acquire)) {
    processes_.clear();
    snapshot_.clear();
    snapshot_retry_at_ = TimePoint{};
    next_cleanup_ = now + kCleanupInterval;
  }
  if (now < next_cleanup_)
    return;

  for (auto it = processes_.begin(); it != processes_.end();) {
    const auto& entry = it->second;
    if (now - entry.last_used >= kIdleLifetime ||
        (entry.process.handle ? backend_.HasExited(entry.process.handle.get()) : now >= entry.retry_at)) {
      it = processes_.erase(it);
    } else {
      ++it;
    }
  }
  if (now >= snapshot_retry_at_)
    snapshot_.clear();
  next_cleanup_ = now + kCleanupInterval;
}

void ProcessNameResolver::Resolve(DWORD id, CString& name, TimePoint now) {
  Maintain(now);
  auto found = processes_.find(id);
  if (found != processes_.end()) {
    auto& entry = found->second;
    if (entry.process.handle || now < entry.retry_at) {
      // An owned process handle pins the process object and its PID, even after
      // exit, so this name cannot be attributed to a different process.
      entry.last_used = now;
      name = entry.process.name;
      return;
    }
    processes_.erase(found);
  }

  auto process = backend_.QueryProcess(id);
  const bool directly_resolved = process.handle && !process.name.IsEmpty();
  if (!directly_resolved) {
    process.handle.reset();
    if (now >= snapshot_retry_at_) {
      snapshot_ = backend_.QuerySnapshot();
      snapshot_retry_at_ = now + kRetryInterval;
    }
    const auto snapshot_name = snapshot_.find(id);
    process.name = snapshot_name != snapshot_.end() && !snapshot_name->second.IsEmpty() ? snapshot_name->second : CString(L"< unknown >");
  }
  name = process.name;
  // A fallback entry must not outlive the snapshot it was read from.
  processes_.emplace(id, Entry{std::move(process), now, directly_resolved ? (TimePoint::max)() : snapshot_retry_at_});
}
