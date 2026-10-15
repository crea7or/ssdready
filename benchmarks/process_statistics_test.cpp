// clang-format off
#include "pch.h"
// clang-format on

#include <cstring>
#include <iostream>
#include <stdexcept>

#include "benchmark_workload.h"
#include "process_list_ctrl.h"

namespace {
void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

class MemoryFile : public File {
 public:

  MemoryFile() : File(FALSE, FALSE) {
  }

  explicit MemoryFile(std::vector<unsigned char> data) : File(FALSE, FALSE), bytes(std::move(data)) {
  }

  std::vector<unsigned char> bytes;
  std::size_t position = 0;

  BOOL Read(void* buffer, std::size_t count) override {
    if (count > bytes.size() - position)
      return FALSE;
    std::memcpy(buffer, bytes.data() + position, count);
    position += count;
    return TRUE;
  }

  BOOL Write(const void* buffer, std::size_t count) override {
    const auto* data = static_cast<const unsigned char*>(buffer);
    bytes.insert(bytes.end(), data, data + count);
    return TRUE;
  }
};

template <class T>
void Append(MemoryFile& file, const T& value) {
  const auto* data = reinterpret_cast<const unsigned char*>(&value);
  file.bytes.insert(file.bytes.end(), data, data + sizeof(value));
}

// Encode the previous CEProcess format independently of the new serializer.
MemoryFile Legacy(const std::vector<ProcessStatistics>& records) {
  MemoryFile file;
  Append(file, static_cast<std::uint32_t>(records.size()));
  for (const auto& record : records) {
    Append(file, record.written_bytes);
    Append(file, static_cast<std::uint32_t>(record.process_index));
    const auto length = static_cast<std::uint32_t>(record.name.GetLength() * sizeof(wchar_t));
    Append(file, length);
    const auto* data = reinterpret_cast<const unsigned char*>(record.name.GetString());
    file.bytes.insert(file.bytes.end(), data, data + length);
  }
  return MemoryFile(std::move(file.bytes));
}

void TestStore() {
  ProcessStatisticsStore store;
  Check(store.AddWrite(CString(L"Writer.exe"), 1ULL << 40) == 0, "First stable ID");
  Check(store.AddWrite(CString(L"WRITER.EXE"), 3) == 0, "Case-insensitive name lookup");
  Check(store.AddWrite(CString(L"\u6587\u4ef6.exe"), 7) == 1, "Unicode name lookup");
  Check(store.Get(0)->written_bytes == (1ULL << 40) + 3, "64-bit totals");
  MemoryFile encoded;
  Check(store.Save(encoded), "Save statistics");
  const auto expected = Legacy(store.records());
  Check(encoded.bytes == expected.bytes, "Version 1 byte compatibility");

  ProcessStatisticsStore restored;
  Check(restored.Load(encoded), "Load statistics");
  Check(restored.AddWrite(CString(L"writer.exe"), 5) == 0, "Rebuild name index after load");
  Check(restored.Get(0)->written_bytes == (1ULL << 40) + 8, "Accumulate restored total");
  for (int index = 0; index < 10000; ++index) {
    CString name;
    name.Format(L"process_%d.exe", index);
    restored.AddWrite(name, index);
  }
  Check(restored.Get(1)->name == L"\u6587\u4ef6.exe" && restored.Get(10001)->process_index == 10001, "IDs survive vector growth");
  Check(!restored.Get(10002), "Out-of-range ID");

  auto duplicates = Legacy({{CString(L"Same.exe"), 10, 0}, {CString(L"SAME.EXE"), 20, 1}});
  Check(restored.Load(duplicates), "Duplicate historical names keep their saved slots");
  MemoryFile duplicate_save;
  Check(restored.Save(duplicate_save) && duplicate_save.bytes == duplicates.bytes, "Preserve duplicate IDs on save");
  Check(restored.AddWrite(CString(L"same.exe"), 3) == 0 && restored.Get(1)->written_bytes == 20, "Legacy lookup chooses first duplicate");

  auto truncated = Legacy({{CString(L"Truncated.exe"), 10, 0}});
  truncated.bytes.resize(10);
  Check(!restored.Load(truncated) && restored.Get(0)->written_bytes == 13, "Failed load keeps existing store");
  auto invalid = Legacy({{CString(L"WrongIndex.exe"), 10, 5}});
  Check(!restored.Load(invalid) && restored.Get(0)->written_bytes == 13, "Reject invalid historical index");

  auto snapshot = restored.records();
  restored.Clear();
  Check(snapshot.size() == 2 && snapshot[0].written_bytes == 13, "Owned snapshot survives Clear");
  Check(restored.AddWrite(CString(L"New.exe"), 1) == 0, "Clear resets name index and IDs");
}

void TestManagerAndUi() {
  ssdready_benchmark::Options options;
  options.items = 128;
  options.unique_paths = 32;
  options.processes = 8;
  ssdready_benchmark::Workload workload(options);
  Check(workload.Retrieve() == options.items && workload.Process() == options.items, "Production processing work units");
  auto& manager = workload.manager();
  auto all = manager.GetProcessStatistics(manager.GetRootFolder(), true);
  Check(!all.processes.empty() && all.written_bytes == manager.GetTotalWritten(), "Root statistics snapshot");
  auto own = manager.GetProcessStatistics(manager.GetRootFolder(), false);
  Check(own.processes.empty() && own.written_bytes == 0, "Own-node statistics exclude descendants");

  wchar_t directory[MAX_PATH]{};
  Check(GetWindowsDirectory(directory, MAX_PATH) != 0, "Windows directory");
  CString path;
  path.Format(L"%c:\\SsdReadyBenchmark\\bucket_0\\file_0.dat", directory[0]);
  TreeItem* file;
  {
    auto lock = manager.LockData();
    file = manager.GetTreeItemFromPath(path);
  }
  auto scoped = manager.GetProcessStatistics(file, false);
  std::uint64_t scoped_sum = 0;
  for (const auto& process : scoped.processes)
    scoped_sum += process.written_bytes;
  Check(!scoped.processes.empty() && scoped_sum == scoped.written_bytes && scoped_sum < all.written_bytes,
        "File snapshot preserves scope-specific totals");
  Check(workload.CheckProcessing(options.items, 0, 0, 0), "Global and file attribution agree");

  // Hidden controls exercise callbacks and pointer lifetime without opening UI.
  CWnd parent;
  Check(parent.CreateEx(0, AfxRegisterWndClass(0), L"Statistics test", WS_POPUP, CRect(0, 0, 400, 200), nullptr, 0),
        "Create hidden parent");
  ProcessListCtrl list;
  Check(list.Create(WS_CHILD | LVS_REPORT, CRect(0, 0, 400, 200), &parent, 1), "Create process list");
  list.InsertColumn(0, L"Process", LVCFMT_LEFT, 250);
  list.InsertColumn(1, L"Written", LVCFMT_LEFT, 150);
  list.SetProcesses({{CString(L"Small.exe"), 1, 0}, {CString(L"Large.exe"), 100000, 1}});
  Check(list.GetItemText(0, 0) == L"Large.exe" && list.GetItemText(1, 0) == L"Small.exe", "Sort and display independent UI rows");
  Check(!list.GetItemText(0, 1).IsEmpty(), "Formatted write total");
  list.SetProcesses(all.processes);
  const auto displayed_name = list.GetItemText(0, 0);
  manager.ClearData(FALSE);
  manager.MaintainProcessNameCache();
  Check(manager.GetProcessStatistics().empty() && !all.processes.empty() && list.GetItemText(0, 0) == displayed_name,
        "UI and snapshots survive statistics and resolver reset");
  list.SetProcesses({});
  Check(list.GetItemCount() == 0, "Replacing UI snapshot removes old rows");
  list.DestroyWindow();
  parent.DestroyWindow();
}

void TestSavedFile(const wchar_t* path) {
  File file(FALSE, FALSE);
  Check(file.Open(path, File::kModeRead | File::kShareDenyNone), "Open existing monitored file read-only");
  std::uint32_t version = 0;
  Check(file.Read(&version, sizeof(version)) && version == 1, "Existing file is version 1");
  constexpr DWORD64 offset = sizeof(version) + 7 * sizeof(DWORD64);
  Check(file.Seek(offset, File::SeekOrigin::kBegin), "Skip manager header");
  ProcessStatisticsStore store;
  Check(store.Load(file), "Load existing process statistics");
  const auto length = file.GetPosition() - offset;
  Check(length <= MAXDWORD, "Process block fits read size");
  MemoryFile original;
  original.bytes.resize(static_cast<std::size_t>(length));
  Check(file.Seek(offset, File::SeekOrigin::kBegin) && file.Read(original.bytes.data(), static_cast<DWORD>(length)),
        "Read original process block");
  MemoryFile saved;
  Check(store.Save(saved) && saved.bytes == original.bytes, "Existing process block saves byte-for-byte");
  std::cout << "Existing monitored file: " << store.records().size() << " processes; " << length << " bytes round-trip unchanged.\n";
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    Check(AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0), "Initialize MFC");
    if (argc == 2) {
      TestSavedFile(argv[1]);
    } else {
      TestStore();
      TestManagerAndUi();
      std::cout << "Process statistics, persistence, production attribution, and UI snapshots passed.\n";
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
