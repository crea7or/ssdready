# Worker performance benchmarks

Two independent Google Benchmark executables measure the application's actual
worker code. Change that code once and rebuild both the app and benchmarks.

| Executable | Shared production function | Timed work |
| --- | --- | --- |
| `retrieve_log_records_benchmark` | `MiniSpyManager::RetrieveLogRecordsBatch` | Decode driver-format batches, filter records, resolve real process names and volume paths, allocate events, enqueue batches, update read totals |
| `process_log_records_benchmark` | `MiniSpyManager::ProcessNextLogRecord` | Pop queued batches, find/create files and processes, update process links and tree totals, delete events |

The real `RetrieveLogRecords` and `ProcessLogRecords` threads call these same
functions. These simulated tests exclude `FilterSendMessage`, driver execution,
idle polling sleeps, GUI work and simultaneous producer/consumer contention.
They measure busy worker throughput, not end-to-end monitoring latency.

## Build

Requires VS2022 17.2 or later (C++20 / std::format), Desktop development with C++, the v143 MFC component,
Windows SDK and CMake tools. No driver, administrator privileges or running
SsdReady instance are required. Initial configuration downloads Google Benchmark
v1.9.4, pinned to commit `31c66f47f60d06790a4885a261d682ba339bd100`.

The app, installer, and benchmarks use C++20. Logging supports checked wide
format strings and accepts `CString`, `std::wstring`, and wide string views:

```cpp
AppLog::Get().Add(L"Loaded {} items from {} in {:.3f} seconds", count, path, elapsed);
```

Use `{}` placeholders (for example, `{:08x}` for HRESULTs), rather than printf
specifiers. A call with just one message keeps its text literal, including braces.

From the repository root, with CMake on PATH:

```powershell
cmake -S benchmarks -B build/google-benchmark -G "Visual Studio 17 2022" -A x64
cmake --build build/google-benchmark --config Release --parallel 4
ctest --test-dir build/google-benchmark -C Release --output-on-failure
```

If CMake is not on PATH, the installed Professional edition supplies it at
`C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
Use the adjacent `ctest.exe` for smoke tests. Build Release for tuning; Debug
includes allocation checks and has different performance. Benchmarks use the
same static MFC/runtime configuration as the app's Release build. The benchmark
runtime uses the production `ApplicationCore` in its headless startup mode.

## Run

```powershell
$bin = "C:\Sources\ssdready\build\google-benchmark\Release"
& "$bin\retrieve_log_records_benchmark.exe" --items=100000 --unique_paths=10000 --seed=42 --cache=warm --benchmark_min_time=1s --benchmark_repetitions=5
& "$bin\process_log_records_benchmark.exe" --items=100000 --unique_paths=10000 --seed=42 --cache=cold --benchmark_min_time=1s --benchmark_repetitions=5
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--items=N` | `10000` | Accepted write events per timed iteration |
| `--unique_paths=N` | `2048` | Distinct synthetic files, capped by the event count |
| `--processes=N` | `0` | Limit the seeded sample of real processes; zero uses every nonzero PID in the launch snapshot |
| `--seed=N` | `42` | Seed for synthetic fields and process sampling |
| `--cache=warm` | warm | Prime caches before timing, retain them across iterations |
| `--cache=cold` | | Clear application caches before every timed iteration |

For a fixed iteration count instead of adaptive calibration, use
`--benchmark_min_time=5x`. This is useful for expensive cold process lookups or
very large event counts. The standard Google Benchmark flags, including JSON
output and repetitions, remain available:

```powershell
& "$bin\retrieve_log_records_benchmark.exe" --items=100000 --unique_paths=10000 --seed=42 --cache=warm --benchmark_repetitions=5 --benchmark_out=build/google-benchmark/retrieve-before.json --benchmark_out_format=json
# Change the shared production code, then rebuild before collecting "after".
cmake --build build/google-benchmark --config Release --parallel 4
& "$bin\retrieve_log_records_benchmark.exe" --items=100000 --unique_paths=10000 --seed=42 --cache=warm --benchmark_repetitions=5 --benchmark_out=build/google-benchmark/retrieve-after.json --benchmark_out_format=json
```

Repeat for processing with the same parameters. Lower `Time`/`CPU` and higher
`items_per_second` indicate improvement. `Time` is wall time for **N events**,
not one event. CPU time on Windows has coarse resolution; short runs can report
zero. Use larger event counts and multiple repetitions for useful CPU figures.
Retrieval `bytes_per_second` counts encoded input bytes. Processing counts
logical write bytes accounted for; it does not perform those disk writes.

## Workload and validation

Toolhelp enumerates this machine's real PIDs. The unchanged application resolver
queries their process names and uses its normal cache/fallback behavior. A
protected or exited process can still resolve to `< unknown >`; the benchmark
does not substitute invented names. Synthetic fields come from a seeded PRNG:
sequence, status, flags, write sizes (1 byte to 1 MiB) and read sizes (0 to
1 MiB minus 1). Record type, major/minor code, lengths, terminators and alignment
are valid write-record metadata so every generated event is accepted.

Every requested distinct path appears at least once; remaining events reuse
random paths. Files are distributed among folders of at most 128 distinct
files. The fixture maps synthetic `HarddiskVolume42` to the machine's Windows
drive and uses `SsdReadyBenchmark\bucket_<N>\file_<N>.dat` paths. It creates
no files. Cold processing still executes the production `GetFileAttributes`
calls for these paths.

Workload generation, enumeration, initialization, queue preparation for the
consumer, validation and cleanup are outside timing. Warm retrieval primes the
PID cache; its queue starts empty without reserved capacity, so event allocation
and vector growth remain timed. Warm processing first consumes one complete
workload outside timing, then measures updates to existing file/process entries.
Cold means empty **application lookup caches**, not flushed OS/filesystem caches
or a reset allocator. The benchmark's headless core omits UI/driver startup,
settings persistence and loading/saving `Monitored.Files`.

Warm-up calls the same `ResolveProcessById` resolver as production retrieval.
Cold reset uses `ClearData(FALSE)` for file and process model entries, followed
by `MaintainProcessNameCache()` to apply the requested PID-cache reset outside
timing. In the application, only the retrieval worker mutates the name cache;
UI clearing publishes an atomic reset request.

Each iteration checks event counts, queue sizes and read/write totals. Processing
also checks per-write 4 KiB rounding, operation counts, expected model-node count,
tree sums and process-link sums against global process totals. A failed check
reports a benchmark error and returns a nonzero exit code. CTest covers both
executables in warm/cold modes over three iterations with a small deterministic
workload. A separate resolver test checks that a real process name is cached,
cleared by `ClearData`, and resolved again after reset. It also checks snapshot
fallback names, one-second negative caching, shared snapshot expiry, process
exit/PID reuse, monotonic cleanup, concurrent UI reset requests and handle
cleanup. Direct resolution uses `QueryFullProcessImageNameW` and retains a
process handle to preserve the PID's identity. Cleanup runs every 30 seconds,
including while monitoring is idle or processing continuously; snapshots and
fallback results expire after one second.

The `drive_info` test checks independent UI/worker snapshots, volume-name
boundaries and 20,000 atomic snapshot handoffs. The optional `drive_info_native`
test enumerates real volumes repeatedly and checks for leaked handles. Windows
requires an administrator token for filter-volume enumeration, so CTest marks
this test skipped when access is denied. Run the same CTest command from an
administrator terminal to include it. Neither test attaches or loads a driver.

The `model_classes` test checks the legacy tree wire format, partial-load rollback,
root-counter reset, cached totals, duplicate process-link aggregation, lazy folder
indexes after reload, repeated UI expansion, and concurrent logging notifications.
It prints the model-object sizes for the target platform. To validate a real
version 1 file, pass its path and a new output path to `model_classes_test.exe`;
the input is read-only and the output must not already exist.

The `batch_notification` test exercises blocking queue waits, publication before
and during waiter startup, empty batches, and shutdown wakeups. It also feeds
the actual processing worker repeatedly and checks that no operations are lost.
The production consumer waits on a condition variable; benchmark work-unit calls
remain nonblocking so timing and queue-drain loops retain their existing behavior.

The `process_statistics` test verifies case-insensitive historical lookup,
64-bit totals, stable IDs across vector growth, legacy serialization, duplicate
saved names, failed-load handling, and scoped manager snapshots. It creates a
hidden process list to exercise display callbacks, sorting, and row ownership
through a manager reset. Historical records remain separate from the PID cache;
only clearing monitoring data removes their IDs and accumulated totals.

To check an existing version 1 file without writing to it:

```powershell
$history = Join-Path $env:APPDATA 'CEZEO software\SsdReady\Monitored.Files'
.\build\google-benchmark\Release\process_statistics_test.exe $history
```

This loads and serializes only its process-statistics block in memory and
compares every byte with the original block.

Keep the machine, build configuration, parameters and running process set
consistent when comparing results. Process availability, permissions and name
resolution can change between launches even with the same seed. Output records
the seed, cache mode, event/path counts and process-pool size. Avoid running the
two executables simultaneously for performance comparisons.

Google Benchmark flag and timing documentation:
<https://github.com/google/benchmark/blob/v1.9.4/docs/user_guide.md>.

The `settings_registry` test covers initialized defaults, legacy registry names,
user/machine hive selection, malformed values, long and unterminated strings,
move-only key ownership, failed writes, and quoted autostart commands. It redirects
both registry hives only inside its own process to temporary test keys and removes
them afterward; installed settings and the real Windows Run entry are untouched.

The `mini_spy_manager` test covers invalid driver record lengths and alignment,
bounded filename and reparse parsing, volume mapping, queue draining on shutdown,
concurrent statistics snapshots, version 1 migration and version 2 history preservation. History tests
use a temporary directory and check failed loads, failed final replacements,
cache invalidation, and repaired parent pointers after a successful reload.
It also checks approximate completed-items-per-second reporting, cached reads,
idle decay, pause/resume and reset behavior, and exclusion of restored history
and events still waiting in the queue from the processing rate.

The `worker_lifecycle` test compiles the actual core and manager implementations
with simulated service/filter API boundaries. It checks driver errors, unquoted
absolute driver image paths, repair of an already-quoted driver path (including
spaces and native path prefixes), the service dependency list, partial worker startup,
retries, initialized protocol commands, and automatic shutdown while retrieval
is blocked. It also feeds driver-format records through both actual worker
entry points and verifies queue draining, operation counts, read/write totals,
file/ancestor totals, and process totals. No real driver service or service-registry
entry is modified.

The `file_utilities` test checks the original UTF-16 wire layout, truncated and
malformed strings, partial reads/writes, buffered final-flush errors, virtual
flush calls, handle ownership, non-inheritable handles, 64-bit seeking, error
logging, and the string helpers. Files live in a temporary test directory. Where
the filesystem supports sparse files, the test also verifies a 4 GiB file length
without allocating 4 GiB of disk space. `File` and `BufferedFile` use `size_t` for
memory buffer lengths and retain DWORD limits only at Win32 and version 1 wire
boundaries. String reads leave their output unchanged on failure; capped string
writes serialize the capped payload length.

The `ui_controls` test uses hidden native MFC windows to check keyboard and
programmatic tree selection, activation of the clicked label instead of the
selected item, ignored expansion buttons and whitespace, repeated expansion
and collapse, folder sorting with totals above 2^63, borrowed
combo-box values, drive-list text callbacks, native double buffering, repeated icon replacement without
GDI-handle growth, buffered painting, modal cleanup, failed dialog resources,
resizing, and daily-write estimates. Its manifest enables common-controls v6,
as in the application. It does not start monitoring or open the main window.
It also creates a hidden instance of the production log viewer to check list
resizing, button anchoring, minimum dimensions, incremental updates, and following
new messages while preserving the scroll position when browsing older entries.

The `installer_paths` test checks exact install/uninstall command parsing,
Unicode and long paths, executable-directory extraction, and native driver
path construction. It reads Windows directory information but does not install,
stop, delete, or configure a service. Installer service operations require
separate integration validation on a machine with the driver.

The remaining application controls and dialogs now use descriptive class names,
snake_case filenames and fields, and RAII for owned resources. Registry value
names, version 1 history loading, resource IDs, and the MiniSpy driver
protocol remain compatible with existing installations.

The `volume_enumeration` test exercises the production drive enumerator with
simulated Windows APIs. It verifies exclusion by `GPT_BASIC_DATA_ATTRIBUTE_SHADOW_COPY`,
including snapshots with ordinary volume names, metadata-only handle access,
and the Windows shadow-device name fallback when attributes cannot be queried.
Hidden and read-only ordinary volumes remain available when a drive letter
resolves. Unlettered volumes and malformed DOS names are excluded before opening
the volume or counting attachments. It also checks enumeration failures,
truncated attribute replies, resized buffers, empty results and handle cleanup,
without opening real volumes or changing filter attachments.

The `history_format` test checks both history versions, exact model round trips,
Latin-1/UTF-16 names, 64-bit counters, raw and compressed block boundaries,
malformed varints/counts/references, checksums, truncation, nesting and overflow.
It also compares three load/save runs on an existing history without overwriting
the input:

```powershell
build\google-benchmark\Release\history_format_test.exe "C:\path\Monitored.Files" "C:\Sources\ssdready\build\history-format-measurement"
```

The output directory receives separate version 1 and version 2 files. The tool
uses the production codecs and verifies every loaded model. See
[history format details](../docs/history-format.md) for the wire layout,
compatibility and measurement limits.
