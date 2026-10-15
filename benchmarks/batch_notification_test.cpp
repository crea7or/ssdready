// clang-format off
#include "pch.h"
// clang-format on

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "benchmark_workload.h"

namespace {
using namespace std::chrono_literals;

void Check(MiniSpyManager& manager, bool condition, const char* message) {
  if (!condition) {
    // Release a blocked test consumer before unwinding its future.
    manager.InitiateCleanUp();
    throw std::runtime_error(message);
  }
}

struct Worker {
  MiniSpyManager& manager;
  std::thread thread;

  explicit Worker(MiniSpyManager& manager)
      : manager(manager), thread([&manager] {
          MiniSpyManager::ProcessLogRecords(&manager);
        }) {
  }

  ~Worker() {
    manager.InitiateCleanUp();
    thread.join();
  }
};

template <class Predicate>
bool WaitUntil(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  do {
    if (predicate())
      return true;
    std::this_thread::sleep_for(1ms);
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}

void TestNotifications() {
  ssdready_benchmark::Options options;
  options.items = 8;
  options.unique_paths = 4;
  options.processes = 2;
  ssdready_benchmark::Workload workload(options);
  auto& manager = workload.manager();

  Check(manager, !manager.GetNextBatchToProcess() && !manager.ProcessNextLogRecord(), "Nonblocking benchmark calls stay nonblocking");

  auto waiting = std::async(std::launch::async, [&] {
    return manager.GetNextBatchToProcess(true);
  });
  Check(manager, waiting.wait_for(30ms) == std::future_status::timeout, "Empty queue blocks the consumer");
  Check(
      manager, manager.RetrieveLogRecordsBatch(nullptr, 0) == 0 && !manager.GetNextBatchToProcess(), "Empty input does not queue a batch");
  Check(manager, waiting.wait_for(30ms) == std::future_status::timeout, "Empty input does not finish the wait");
  Check(manager, workload.Retrieve() == options.items, "Publish production batch");
  Check(manager, waiting.wait_for(2s) == std::future_status::ready, "Published batch wakes the consumer");
  auto batch = waiting.get();
  Check(manager, batch && batch->size() == options.items && manager.GetUnprocessedItemsCount() == 0, "Consume exactly one batch");

  Check(manager, workload.Retrieve() == options.items, "Publish before wait");
  batch = manager.GetNextBatchToProcess(true);
  Check(manager, batch && batch->size() == options.items, "Already queued batch needs no later notification");

  // Race waiter startup with enqueue repeatedly, exercising the wait predicate.
  for (int iteration = 0; iteration < 100; ++iteration) {
    auto raced = std::async(std::launch::async, [&] {
      return manager.GetNextBatchToProcess(true);
    });
    Check(manager, workload.Retrieve() == options.items, "Race batch publication");
    Check(manager, raced.wait_for(2s) == std::future_status::ready, "No missed notification at wait startup");
    batch = raced.get();
    Check(manager, batch && batch->size() == options.items, "Race preserves batch contents");
  }

  // Obtain an expected byte count, then exercise the actual processing thread.
  workload.Reset();
  Check(manager, workload.Retrieve() == options.items && workload.Process() == options.items, "Prepare expected totals");
  const auto bytes_per_batch = manager.GetTotalWritten();
  workload.Reset();
  {
    Worker worker(manager);
    for (std::uint64_t iteration = 1; iteration <= 32; ++iteration) {
      Check(manager, workload.Retrieve() == options.items, "Feed processing worker");
      Check(manager,
            WaitUntil([&] {
              auto lock = manager.LockData();
              return manager.GetTotalWritten() == iteration * bytes_per_batch;
            }),
            "Worker processes each newly published batch");
    }
    Check(manager, manager.GetUnprocessedItemsCount() == 0, "Worker drains the queue");
    // Allow it to return to the empty-queue wait before requesting shutdown.
    std::this_thread::sleep_for(30ms);
  }
  Check(manager, manager.GetTotalOperations() == 32 * options.items, "Worker preserves all operations");
  Check(manager, !manager.GetNextBatchToProcess(true), "Shutdown before a wait returns immediately");

  MiniSpyManager idle_manager;
  auto idle = std::async(std::launch::async, [&] {
    return idle_manager.GetNextBatchToProcess(true);
  });
  Check(idle_manager, idle.wait_for(30ms) == std::future_status::timeout, "Idle consumer waits");
  idle_manager.InitiateCleanUp();
  Check(idle_manager, idle.wait_for(2s) == std::future_status::ready && !idle.get(), "Shutdown wakes an idle consumer");
}
}  // namespace

int main() {
  try {
    if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0))
      return 1;
    TestNotifications();
    std::cout << "Batch wakeups, startup races, real worker processing, and idle shutdown passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
