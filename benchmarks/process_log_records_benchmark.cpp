#include "benchmark_workload.h"

namespace {
void ProcessLogRecords(benchmark::State& state, ssdready_benchmark::Workload& workload, const ssdready_benchmark::Options& options) {
  workload.Reset();
  if (options.warm_cache) {
    workload.Retrieve();
    workload.Process();
  }
  for (auto iteration : state) {
    (void)iteration;
    state.PauseTiming();
    if (!options.warm_cache)
      workload.Reset();
    const auto initial_writes = workload.manager().GetTotalWritten();
    const auto initial_rounded = workload.manager().GetRoundedWritten();
    const auto initial_operations = workload.manager().GetTotalOperations();
    const auto queued = workload.Retrieve();
    const bool ready = queued == options.items && workload.manager().GetUnprocessedItemsCount() == options.items;
    state.ResumeTiming();
    if (!ready) {
      ssdready_benchmark::Fail(state, "Could not prepare the processing queue.");
      break;
    }
    const auto count = workload.Process();
    benchmark::DoNotOptimize(count);
    state.PauseTiming();
    const bool valid = workload.CheckProcessing(count, initial_writes, initial_rounded, initial_operations);
    state.ResumeTiming();
    if (!valid) {
      ssdready_benchmark::Fail(state, "Processing lost events or produced incorrect write totals.");
      break;
    }
  }
  workload.SetCounters(state, false);
}
}  // namespace

int main(int argc, char** argv) {
  return ssdready_benchmark::Run(argc, argv, "ProcessLogRecords", ProcessLogRecords);
}
