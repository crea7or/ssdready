#pragma once

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <benchmark/benchmark.h>
#pragma pop_macro("max")
#pragma pop_macro("min")

#include <cstdint>
#include <string>
#include <vector>

#include "pch.h"

namespace ssdready_benchmark {

struct Options {
  std::size_t items = 10000;
  std::size_t unique_paths = 2048;
  std::size_t processes = 0;  // Zero selects all processes in the snapshot.
  std::uint32_t seed = 42;
  bool warm_cache = true;
};

struct Batch {
  std::vector<ULONG_PTR> storage;  // Same alignment as malloc in the real thread.
  DWORD bytes = 0;
};

class Workload {
 public:

  explicit Workload(const Options& options);
  ~Workload();
  Workload(const Workload&) = delete;
  Workload& operator=(const Workload&) = delete;

  void Reset();
  void ClearQueue();
  void WarmProcessNames();
  std::size_t Retrieve();
  std::size_t Process();
  bool CheckRetrieval(std::size_t count, DWORD64 initial_reads) const;
  bool CheckProcessing(std::size_t count, DWORD64 initial_writes, DWORD64 initial_rounded, DWORD64 initial_operations) const;
  void SetCounters(benchmark::State& state, bool retrieval) const;

  MiniSpyManager& manager() const {
    return *g_application_core->GetManager();
  }

 private:

  Options options_;
  std::vector<DWORD> process_ids_;
  std::vector<Batch> batches_;
  std::size_t path_count_ = 0;
  DWORD64 encoded_bytes_ = 0;
  DWORD64 written_bytes_ = 0;
  DWORD64 rounded_bytes_ = 0;
  DWORD64 read_bytes_ = 0;
};

// Removes our options before forwarding the remaining arguments to Google Benchmark.
Options ParseOptions(int* argc, char** argv);
void Fail(benchmark::State& state, const char* message);
int Run(int argc, char** argv, const char* name, void (*function)(benchmark::State&, Workload&, const Options&));

}  // namespace ssdready_benchmark
