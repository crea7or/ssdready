#include <iostream>
#include <stdexcept>
#include <thread>

// clang-format off
#include "pch.h"
// clang-format on

class ProcessNameResolverTestPeer {
 public:

  static std::size_t Count(const ProcessNameResolver& resolver) {
    return resolver.processes_.size();
  }

  static bool Has(const ProcessNameResolver& resolver, DWORD id) {
    return resolver.processes_.count(id) != 0;
  }
};

namespace {
CWinApp cache_test_app;
using Resolver = ProcessNameResolver;
using TimePoint = Resolver::TimePoint;

bool Check(bool result, const char* message) {
  if (!result)
    std::cerr << message << '\n';
  return result;
}

class FakeBackend : public Resolver::Backend {
 public:

  Resolver::ProcessHandle running{CreateEvent(nullptr, TRUE, FALSE, nullptr)};
  CString executable = L"first.exe";
  Resolver::Snapshot snapshot_names{{41, CString(L"System")}};
  std::size_t queries = 0;
  std::size_t snapshots = 0;

  Resolver::ProcessInfo QueryProcess(DWORD id) override {
    ++queries;
    Resolver::ProcessInfo result;
    if (id == 42) {
      HANDLE duplicate = nullptr;
      if (!DuplicateHandle(GetCurrentProcess(), running.get(), GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
        throw std::runtime_error("Cannot duplicate test lifetime handle.");
      result.handle.reset(duplicate);
      result.name = executable;
    }
    return result;
  }

  Resolver::Snapshot QuerySnapshot() override {
    ++snapshots;
    return snapshot_names;
  }

  bool HasExited(HANDLE handle) override {
    return WaitForSingleObject(handle, 0) != WAIT_TIMEOUT;
  }
};

class ManagerProbe : public MiniSpyManager {
 public:

  std::size_t CachedProcessCount() const {
    return ProcessNameResolverTestPeer::Count(process_name_resolver_);
  }
};

bool TestCachePolicy() {
  FakeBackend backend;
  Resolver resolver(backend);
  const auto start = TimePoint{} + std::chrono::hours(1);
  CString name;
  for (int second = 0; second < 10; ++second)
    resolver.Resolve(4, name, start + std::chrono::seconds(second));
  if (!Check(name == L"System" && backend.queries == 0 && backend.snapshots == 0, "System PID must not query or enumerate processes."))
    return false;
  resolver.Resolve(42, name, start);
  if (!Check(name == L"first.exe", "Direct resolution failed."))
    return false;
  for (int i = 0; i < 1000; ++i)
    resolver.Resolve(42, name, start);
  if (!Check(backend.queries == 1 && backend.snapshots == 0, "Cache hits repeated OS queries."))
    return false;

  resolver.Resolve(41, name, start);
  if (!Check(name == L"System", "Snapshot fallback discarded the executable name."))
    return false;
  for (int i = 0; i < 1000; ++i)
    resolver.Resolve(99, name, start);
  if (!Check(name == L"< unknown >" && backend.queries == 3 && backend.snapshots == 1,
             "Repeated missing PIDs bypassed the negative cache."))
    return false;
  for (DWORD id = 100; id < 200; ++id)
    resolver.Resolve(id, name, start);
  if (!Check(backend.snapshots == 1, "Fallback enumerated every distinct missing PID separately."))
    return false;

  backend.snapshot_names[99] = L"new.exe";
  resolver.Resolve(98, name, start + std::chrono::milliseconds(900));
  backend.snapshot_names[98] = L"late.exe";
  resolver.Resolve(99, name, start + std::chrono::milliseconds(999));
  if (!Check(name == L"< unknown >", "Negative cache expired prematurely."))
    return false;
  resolver.Resolve(99, name, start + std::chrono::seconds(1));
  if (!Check(name == L"new.exe" && backend.snapshots == 2, "Negative cache did not retry after its deadline."))
    return false;
  resolver.Resolve(98, name, start + std::chrono::seconds(1));
  if (!Check(name == L"late.exe" && backend.snapshots == 2, "A late lookup extended the stale snapshot's lifetime."))
    return false;
  backend.snapshot_names[41] = L"reused-snapshot-pid.exe";
  resolver.Resolve(41, name, start + std::chrono::seconds(2));
  if (!Check(name == L"reused-snapshot-pid.exe", "Snapshot names did not refresh after PID reuse."))
    return false;

  // Model an exited process, then reuse its ID after maintenance releases it.
  SetEvent(backend.running.get());
  resolver.Maintain(start + std::chrono::seconds(30));
  if (!Check(!ProcessNameResolverTestPeer::Has(resolver, 42), "Maintenance retained an exited process."))
    return false;
  backend.running.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
  backend.executable = L"second.exe";
  resolver.Resolve(42, name, start + std::chrono::seconds(30));
  if (!Check(name == L"second.exe", "PID reuse returned the previous process name."))
    return false;

  resolver.RequestClear();
  if (!Check(ProcessNameResolverTestPeer::Count(resolver) > 0, "RequestClear mutated the owner-thread cache."))
    return false;
  resolver.Maintain(start + std::chrono::seconds(31));
  if (!Check(ProcessNameResolverTestPeer::Count(resolver) == 0, "Owner-thread maintenance did not apply the clear request."))
    return false;
  const auto before = backend.snapshots;
  resolver.Resolve(41, name, start + std::chrono::seconds(31));
  if (!Check(backend.snapshots == before + 1, "Clear retained the fallback snapshot."))
    return false;
  resolver.Resolve(42, name, start + std::chrono::seconds(31));
  resolver.Maintain(start + std::chrono::hours(3));
  if (!Check(ProcessNameResolverTestPeer::Count(resolver) == 0, "Monotonic idle expiry did not prune cached entries."))
    return false;
  return true;
}

bool TestNativeResolutionAndConcurrentClear() {
  ManagerProbe manager;
  const DWORD id = GetCurrentProcessId();
  CString name;
  manager.ResolveProcessById(id, name);
  if (!Check(!name.IsEmpty() && name != L"< unknown >" && manager.CachedProcessCount() == 1,
             "The current real process did not populate the PID cache."))
    return false;
  CString resolved;
  manager.ResolveProcessById(id, resolved);
  if (!Check(resolved == name && manager.CachedProcessCount() == 1, "Repeated resolution changed the cached result."))
    return false;

  manager.ClearData(FALSE);
  manager.MaintainProcessNameCache();
  if (!Check(manager.CachedProcessCount() == 0, "Benchmark reset retained PID entries."))
    return false;
  manager.ResolveProcessById(id, resolved);
  if (!Check(resolved == name, "Native resolution failed after cache reset."))
    return false;
  manager.ResolveProcessById(4, resolved);
  if (!Check(resolved == L"System", "The native System process fallback did not return its snapshot name."))
    return false;
  manager.ResolveProcessById(0xffffffffUL, resolved);
  if (!Check(resolved == L"< unknown >", "A nonexistent process resolved to an executable name."))
    return false;

  std::atomic<bool> failed{false};
  std::thread clearer([&] {
    for (int i = 0; i < 5000; ++i)
      manager.ClearData(FALSE);
  });
  for (int i = 0; i < 20000; ++i) {
    manager.ResolveProcessById(id, resolved);
    if (resolved != name)
      failed = true;
  }
  clearer.join();
  if (!Check(!failed, "Concurrent UI clear requests broke retrieval-thread resolution."))
    return false;
  manager.ClearData(FALSE);
  manager.MaintainProcessNameCache();
  return Check(manager.CachedProcessCount() == 0, "The final clear request was lost.");
}

bool TestHandleLifetime() {
  FakeBackend backend;
  DWORD before = 0, after = 0;
  GetProcessHandleCount(GetCurrentProcess(), &before);
  {
    Resolver resolver(backend);
    CString name;
    for (int i = 0; i < 200; ++i) {
      resolver.Resolve(42, name);
      resolver.RequestClear();
      resolver.Maintain();
    }
    resolver.Resolve(42, name);  // Destruction must release this final handle too.
  }
  GetProcessHandleCount(GetCurrentProcess(), &after);
  return Check(before == after, "Resolver clear/destruction leaked owned process handles.");
}
}  // namespace

int main() {
  if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0))
    return 1;
  if (!TestCachePolicy() || !TestNativeResolutionAndConcurrentClear() || !TestHandleLifetime())
    return 1;
  std::cout << "Resolver fallback, negative caching, PID lifetimes, monotonic cleanup, concurrent reset and handle cleanup passed.\n";
  return 0;
}
