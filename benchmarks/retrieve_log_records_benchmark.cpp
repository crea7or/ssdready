#include "benchmark_workload.h"

namespace {
void RetrieveLogRecords(benchmark::State& state, ssdready_benchmark::Workload& workload, const ssdready_benchmark::Options& options) {
  workload.Reset();
  if (options.warm_cache)
    workload.WarmProcessNames();
  for (auto iteration : state) {
    (void)iteration;
    state.PauseTiming();
    if (!options.warm_cache)
      workload.Reset();
    const auto initial_reads = workload.manager().GetTotalRead();
    state.ResumeTiming();
    const auto count = workload.Retrieve();
    benchmark::DoNotOptimize(count);
    state.PauseTiming();
    const bool valid = workload.CheckRetrieval(count, initial_reads);
    workload.ClearQueue();
    state.ResumeTiming();
    if (!valid) {
      ssdready_benchmark::Fail(state, "Retrieval lost events or produced incorrect read totals.");
      break;
    }
  }
  workload.SetCounters(state, true);
}
}  // namespace

int main(int argc, char** argv) {
  return ssdready_benchmark::Run(argc, argv, "RetrieveLogRecords", RetrieveLogRecords);
}
