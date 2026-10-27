// clang-format off
#include "pch.h"
// clang-format on
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#include "app_log.h"
#include "benchmark_workload.h"
#include "buffered_file.h"
#include "file_tree_item.h"
#include "monitoring_tree_ctrl.h"

namespace {
void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

class MemoryFile : public File {
 public:

  MemoryFile() : File(FALSE, FALSE) {
  }

  std::vector<unsigned char> bytes;
  std::size_t position = 0;

  BOOL Read(void* target, std::size_t count) override {
    if (count > bytes.size() - position)
      return FALSE;
    std::memcpy(target, bytes.data() + position, count);
    position += count;
    return TRUE;
  }

  BOOL Write(const void* source, std::size_t count) override {
    const auto* data = static_cast<const unsigned char*>(source);
    bytes.insert(bytes.end(), data, data + count);
    return TRUE;
  }
};

template <class T>
void Append(MemoryFile& file, const T& value) {
  const auto* data = reinterpret_cast<const unsigned char*>(&value);
  file.bytes.insert(file.bytes.end(), data, data + sizeof(value));
}

struct Node {
  CString alias;
  TreeItemType type;
  DWORD64 bytes;
  DWORD64 rounded;
  std::vector<ProcessWriteStatistics> links;
  std::vector<Node> children;
};

DWORD64 Total(const Node& node) {
  DWORD64 bytes = node.bytes;
  for (const auto& child : node.children)
    bytes += Total(child);
  return bytes;
}

// Encode the original version 1 layout independently of the class serializers.
void Encode(MemoryFile& file, const Node& node) {
  Append(file, DWORD(0));
  const auto length = DWORD(node.alias.GetLength() * sizeof(wchar_t));
  Append(file, length);
  const auto* name = reinterpret_cast<const unsigned char*>(node.alias.GetString());
  file.bytes.insert(file.bytes.end(), name, name + length);
  Append(file, Total(node));
  Append(file, node.bytes);
  Append(file, node.rounded);
  Append(file, DWORD(node.links.size()));
  for (const auto& link : node.links) {
    Append(file, link.written_bytes);
    Append(file, DWORD(link.process_index));
  }
  Append(file, DWORD(node.children.size()));
  for (const auto& child : node.children) {
    Append(file, static_cast<DWORD>(child.type));
    Encode(file, child);
  }
}

TreeItem* Find(TreeItem& root, const std::vector<CString>& names, bool file = true) {
  CStringArray parts;
  for (const auto& name : names)
    parts.Add(name);
  return root.GetItemByPath(&parts, 0, file);
}

void TestPersistenceAndReset() {
  const Node source{CString(L"C:"),
                    TreeItemType::kFolder,
                    5,
                    4096,
                    {{5, 0}},
                    {{CString(L"Folder"),
                      TreeItemType::kFolder,
                      0,
                      0,
                      {},
                      {{CString(L"First.dat"), TreeItemType::kFile, 10, 8192, {{7, 0}, {3, 1}}, {}},
                       {CString(L"Second.dat"), TreeItemType::kFile, 20, 4096, {{20, 0}}, {}}}}}};
  MemoryFile legacy;
  Encode(legacy, source);
  FolderTreeItem root;
  Check(root.Load(&legacy), "Read legacy tree layout");
  Check(root.GetWriteSizeTotal() == 35 && root.GetWriteSize() == 5, "Loaded cached and direct totals");
  MemoryFile saved;
  Check(root.Save(&saved) && saved.bytes == legacy.bytes, "Version 1 tree bytes round-trip unchanged");
  const auto links = root.GetProcessWrites(true);
  Check(links.size() == 2 && links[0].process_index == 0 && links[0].written_bytes == 32 && links[1].written_bytes == 3,
        "Subtree aggregation merges process IDs in encounter order");
  Check(root.GetProcessWrites(false).size() == 1 && root.GetProcessWrites(false)[0].written_bytes == 5,
        "Direct process links exclude children");
  auto* first = Find(root, {CString(L"folder"), CString(L"FIRST.DAT")});
  Check(first->GetParent() && first->GetWriteSize() == 10, "Restored parent and case-insensitive lookup");
  CString path;
  first->GetFullPath(&path);
  Check(path == L"C:\\Folder\\First.dat", "Shared full-path implementation");
  first->AddWrite(1, 1);
  Check(root.GetWriteSizeTotal() == 36 && first->GetWriteSizeTotal() == 11, "Write propagates cached totals");

  MemoryFile current;
  Check(root.Save(&current), "Save updated tree");
  current.bytes.pop_back();
  Check(!root.Load(&current) && root.GetWriteSizeTotal() == 36 && Find(root, {CString(L"Folder"), CString(L"First.dat")}) == first,
        "Truncated load preserves the original node and child addresses");
  root.Clear();
  Check(root.GetChildrenCount() == 0 && root.GetWriteSize() == 0 && root.GetWriteSizeTotal() == 0 && root.GetProcessWrites(true).empty(),
        "Clear resets children, direct counters, cached counters, and process links");
  legacy.position = 0;
  Check(root.Load(&legacy) && root.GetWriteSizeTotal() == 35, "Reload after clear");

  // Correct a stale cached field without changing any own-node write statistics.
  MemoryFile stale;
  Encode(stale, source);
  const std::size_t cached_offset = 8 + source.alias.GetLength() * sizeof(wchar_t);
  const DWORD64 wrong = 999;
  std::memcpy(stale.bytes.data() + cached_offset, &wrong, sizeof(wrong));
  Check(root.Load(&stale) && root.GetWriteSizeTotal() == 35, "Load rebuilds stale cached totals");
  MemoryFile invalid;
  Append(invalid, DWORD(7));
  Check(!root.Load(&invalid) && root.GetWriteSizeTotal() == 35, "Unsupported node version leaves data intact");

  ProcessWriteStatistics link{123, 7};
  MemoryFile broken_link;
  Append(broken_link, DWORD64(999));
  Check(!link.Load(&broken_link) && link.written_bytes == 123 && link.process_index == 7, "Truncated process link load is atomic");

  MemoryFile duplicate;
  Encode(duplicate, {CString(L"dup"), TreeItemType::kFile, 10, 4096, {{4, 0}, {6, 0}}, {}});
  FileTreeItem duplicate_file;
  Check(duplicate_file.Load(&duplicate) && duplicate_file.GetProcessWrites(false).size() == 1 &&
            duplicate_file.GetProcessWrites(false)[0].written_bytes == 10,
        "Duplicate saved links still aggregate correctly");
}

void TestFolderIndexAndUi() {
  auto* root = Core()->GetManager()->GetRootFolder();
  root->Clear();
  std::vector<TreeItem*> original;
  for (int index = 0; index < 128; ++index) {
    CString name;
    name.Format(L"file_%03d.dat", index);
    original.push_back(Find(*root, {CString(L"C:"), CString(L"wide"), name}));
  }
  Check(Find(*root, {CString(L"c:"), CString(L"WIDE"), CString(L"FILE_000.DAT")}) == original[0], "Indexed case-insensitive lookup");
  Check(Find(*root, {CString(L"C:"), CString(L"wide"), CString(L"file_127.dat")}) == original[127],
        "Child addresses survive vector growth");
  MemoryFile saved;
  Check(root->Save(&saved) && root->Load(&saved), "Replace a tree with a populated child index");
  Check(Find(*root, {CString(L"C:"), CString(L"wide"), CString(L"FILE_127.DAT")})->CompareAlias(L"file_127.dat"),
        "Reload discards obsolete child index pointers");

  CWnd parent;
  Check(parent.CreateEx(0, AfxRegisterWndClass(0), L"Tree model test", WS_POPUP, CRect(0, 0, 400, 200), nullptr, 0), "Hidden tree parent");
  MonitoringTreeCtrl tree;
  Check(tree.Create(WS_CHILD | TVS_HASBUTTONS, CRect(0, 0, 400, 200), &parent, 1), "Hidden tree control");
  Core()->SetTreeControl(&tree);
  root->AddToTreeCtrl();
  root->AddToTreeCtrl();
  Check(tree.GetCount() == 1, "Repeated materialization does not duplicate root nodes");
  const auto drive = tree.GetRootItem();
  tree.Expand(drive, TVE_EXPAND);
  const auto folder = tree.GetChildItem(drive);
  Check(folder && tree.GetCount() == 2, "Drive expansion materializes one level");
  tree.Expand(folder, TVE_EXPAND);
  Check(tree.GetCount() == 130, "Folder expansion materializes saved children");
  tree.Expand(folder, TVE_COLLAPSE);
  tree.Expand(folder, TVE_EXPAND);
  Check(tree.GetCount() == 130, "Re-expansion retains the existing UI children");
  auto* file = Find(*root, {CString(L"C:"), CString(L"wide"), CString(L"file_000.dat")});
  file->AddWrite(4096, 0);
  Check(tree.GetItemText(file->GetTreeItem()).Find(L"file_000.dat") == 0, "Tree display callback uses the rewritten model");
  auto refresh = [&] {
    MSG message;
    while (PeekMessage(&message, tree.GetSafeHwnd(), WM_APP, WM_APP + 64, PM_REMOVE))
      DispatchMessage(&message);
  };
  auto* largest = Find(*root, {CString(L"C:"), CString(L"wide"), CString(L"file_127.dat")});
  largest->AddWrite(8192, 0);
  tree.NotifyItemsChanged();
  refresh();
  Check(tree.GetItemData(tree.GetChildItem(folder)) == reinterpret_cast<DWORD_PTR>(largest),
        "Growing an existing file updates write-size sorting without adding children");
  Core()->GetInterfaceSettings()->sort_tree_by_write_size_ = false;
  tree.RefreshSortOrder();
  refresh();
  Check(tree.GetItemText(tree.GetChildItem(folder)).Find(L"file_000.dat") == 0,
        "Changing sort options reorders already materialized children");
  Core()->GetInterfaceSettings()->sort_tree_by_write_size_ = true;
  tree.DeleteAllItems();
  Core()->SetTreeControl(nullptr);
  tree.DestroyWindow();
  parent.DestroyWindow();
  root->Clear();
}

int DrainLogMessages(HWND window) {
  MSG message;
  int count = 0;
  while (PeekMessage(&message, window, AppLog::kLogUpdatedMessage, AppLog::kLogUpdatedMessage, PM_REMOVE))
    ++count;
  return count;
}

void TestLog() {
  auto& log = AppLog::Get();
  CStringArray baseline;
  log.CopyLog(baseline);
  const auto first = baseline.GetSize();
  HWND viewer = CreateWindowEx(0, L"STATIC", L"Log test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr);
  Check(viewer != nullptr, "Message-only log viewer");
  log.SetViewer(viewer);
  std::vector<std::thread> writers;
  for (int worker = 0; worker < 4; ++worker)
    writers.emplace_back([&log] {
      for (int index = 0; index < 250; ++index)
        log.Add(L"Concurrent log message");
    });
  for (auto& writer : writers)
    writer.join();
  Check(DrainLogMessages(viewer) == 1, "Concurrent log notifications coalesce");
  CStringArray messages;
  log.CopyLog(messages, first);
  Check(messages.GetSize() == 1000 && messages[0].Mid(19) == L" Concurrent log message", "Concurrent logs and timestamp format");
  log.CopyLog(messages, first + 1000);
  Check(messages.IsEmpty(), "Incremental log copy excludes existing messages");
  log.CopyLog(messages, -1);
  Check(messages.GetSize() == first + 1000, "Negative log offset is clamped");
  log.CopyLog(messages, first + 2000);
  Check(messages.IsEmpty(), "Offset past end is clamped");
  log.Add(L"Second update");
  Check(DrainLogMessages(viewer) == 1, "Reading log enables the next notification");
  log.SetViewer(nullptr);
  log.Add(L"Detached viewer");
  Check(DrainLogMessages(viewer) == 0, "Detached viewer receives no notification");
  DestroyWindow(viewer);
}

void TestBoundedLog() {
  auto& log = AppLog::Get();
  for (std::size_t index = 0; index < AppLog::kMaxMessages + 100; ++index)
    log.Add(L"Bounded log item {}", index);
  CStringArray messages;
  INT_PTR retained = 0;
  const auto end = log.CopyLog(messages, 0, &retained);
  Check(messages.GetSize() == AppLog::kMaxMessages && retained > 0, "Log evicts old messages at its fixed limit");
  log.CopyLog(messages, end - 1);
  Check(messages.GetSize() == 1, "Incremental log cursor remains correct after eviction");
}

void TestFormattedLog() {
  auto& log = AppLog::Get();
  CStringArray messages;
  log.CopyLog(messages);
  const auto first = messages.GetSize();
  const CString path(L"C:\\logs\\\u0444\u0430\u0439\u043b.txt");
  const std::wstring name = L"\u6d4b\u8bd5.exe";
  const std::wstring_view state = L"running";
  const std::uint64_t bytes = 0x123456789abcdef0ULL;
  const std::size_t count = (std::numeric_limits<std::size_t>::max)();
  log.Add(L"{} / {} / {} / {}", path, name, state, L"wide text");
  log.Add(L"bytes={:016x}; count={}; time={:.3f}", bytes, count, 1.25);
  log.Add(L"{{count}} = {0}; again {0}", 42);
  log.Add(L"[{:>8.3}]", CString(L"abcdef"));
  log.Add(L"Literal %s and {braces}");
  log.Add(CString(L"CString literal {braces}"));
  const std::wstring long_text(8192, L'x');
  log.Add(L"Long: {}", long_text);
  log.CopyLog(messages, first);
  Check(messages.GetSize() == 7, "All formatted and literal messages recorded");
  Check(messages[0].Mid(20) == L"C:\\logs\\\u0444\u0430\u0439\u043b.txt / \u6d4b\u8bd5.exe / running / wide text",
        "CString, std::wstring, views, and wide literals preserve Unicode");
  const CString expected_count(sizeof(std::size_t) == 8 ? L"18446744073709551615" : L"4294967295");
  Check(messages[1].Mid(20) == L"bytes=123456789abcdef0; count=" + expected_count + L"; time=1.250",
        "Full width integers, hexadecimal, and floating precision");
  Check(messages[2].Mid(20) == L"{count} = 42; again 42", "Escaped braces and positional arguments");
  Check(messages[3].Mid(20) == L"[     abc]", "CString supports string alignment and precision");
  Check(messages[4].Mid(20) == L"Literal %s and {braces}" && messages[5].Mid(20) == L"CString literal {braces}",
        "Single argument messages remain literal");
  Check(messages[6].GetLength() == 20 + 4096 && messages[6].Right(4090) == CString(long_text.c_str(), 4090),
        "Oversized formatted messages are bounded");

  const auto concurrent_first = first + messages.GetSize();
  std::vector<std::thread> writers;
  for (int worker = 0; worker < 4; ++worker)
    writers.emplace_back([&log, worker] {
      for (int index = 0; index < 50; ++index)
        log.Add(L"Worker {} item {}", worker, index);
    });
  for (auto& writer : writers)
    writer.join();
  log.CopyLog(messages, concurrent_first);
  Check(messages.GetSize() == 200, "Concurrent formatting records every message");
  std::array<std::array<bool, 50>, 4> seen{};
  for (INT_PTR index = 0; index < messages.GetSize(); ++index) {
    int worker = -1, item = -1;
    Check(swscanf_s(messages[index].GetString() + 20, L"Worker %d item %d", &worker, &item) == 2 && worker >= 0 && worker < 4 &&
              item >= 0 && item < 50,
          "Concurrent formatted messages remain intact");
    Check(!seen[worker][item], "Concurrent messages have no duplicates");
    seen[worker][item] = true;
  }
}

void TestNativeFile(const wchar_t* input, const wchar_t* output) {
  Check(_wcsicmp(input, output) != 0 && !File::Exists(output), "Compatibility output must be a new file distinct from input");
  BufferedFile file(FALSE, FALSE);
  Check(file.Open(input, File::kModeRead | File::kShareDenyNone), "Read existing monitored file");
  DWORD version;
  std::array<DWORD64, 7> manager_info{};
  Check(file.Read(&version, sizeof(version)) && version == 1 && file.Read(manager_info.data(), sizeof(manager_info)),
        "Read version 1 header");
  ProcessStatisticsStore processes;
  Check(processes.Load(file), "Read existing process IDs");
  FolderTreeItem root;
  Check(root.Load(&file), "Read existing monitored tree");
  Check(file.Close(), "Close original read-only file");
  const auto links = root.GetProcessWrites(true);
  for (const auto& link : links)
    Check(link.process_index < processes.records().size(), "Restored tree process IDs are valid");
  BufferedFile saved(FALSE, FALSE);
  Check(saved.Open(output, File::kModeWrite | File::kCreate), "Open separate compatibility output");
  Check(saved.Write(&version, sizeof(version)) && saved.Write(manager_info.data(), sizeof(manager_info)) && processes.Save(saved) &&
            root.Save(&saved) && saved.Close(),
        "Save separate compatibility output");
  std::cout << "Loaded and saved existing tree: " << processes.records().size() << " processes, " << links.size()
            << " aggregate process links, " << root.GetWriteSizeTotal() << " written bytes.\n";
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    Check(AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0), "Initialize MFC");
    if (argc == 3)
      TestNativeFile(argv[1], argv[2]);
    else {
      ssdready_benchmark::Options options;
      options.items = 8;
      options.unique_paths = 4;
      options.processes = 2;
      ssdready_benchmark::Workload workload(options);
      TestPersistenceAndReset();
      TestFolderIndexAndUi();
      TestLog();
      TestFormattedLog();
      TestBoundedLog();
      std::cout << "Model persistence, child lookup, reset, tree UI, and plain/formatted concurrent logging passed.\n";
    }
    std::cout << "x64/Win32 sizes: NamedObject=" << sizeof(NamedObject) << "; file=" << sizeof(FileTreeItem)
              << "; folder=" << sizeof(FolderTreeItem) << "; process link=" << sizeof(ProcessWriteStatistics)
              << "; event=" << sizeof(PendingWriteEvent) << ".\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
