// history_export: writes a SsdReady monitoring history as JSON, keyed by
// process name. Each value holds the process's total written bytes and the
// unique paths it wrote to. Reads version 1 and 2 histories with the app's codec.
// clang-format off
#include "pch.h"
// clang-format on

#include <KnownFolders.h>
#include <ShlObj.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "buffered_file.h"
#include "history_format.h"
#include "mini_spy_manager.h"

namespace {
CWinApp application;

struct ProcessExport {
  CString name;
  std::uint64_t written_bytes = 0;
  std::vector<const TreeItem*> items;
  // Folder mode only: a folder recurs in preorder after each of its subfolders.
  std::unordered_set<const TreeItem*> listed_folders;
};

enum class ListMode { kFiles, kFilesAndFolders, kFolders };

class JsonWriter {
 public:

  explicit JsonWriter(std::FILE* file) : file_(file) {
    buffer_.reserve(kFlushSize + 4096);
  }

  void Raw(const char* text) {
    buffer_ += text;
    FlushIfFull();
  }

  void Number(std::uint64_t value) {
    buffer_ += std::to_string(value);
  }

  // UTF-16 to UTF-8. Unpaired surrogates, which NTFS names may contain, become \u escapes.
  void String(const wchar_t* text, std::size_t length) {
    buffer_ += '"';
    for (std::size_t index = 0; index < length; ++index) {
      const unsigned value = text[index];
      if (value == L'"' || value == L'\\') {
        buffer_ += '\\';
        buffer_ += static_cast<char>(value);
      } else if (value < 0x20) {
        Escape(value);
      } else if (value < 0x80) {
        buffer_ += static_cast<char>(value);
      } else if (value < 0x800) {
        buffer_ += static_cast<char>(0xC0 | (value >> 6));
        buffer_ += static_cast<char>(0x80 | (value & 0x3F));
      } else if (value >= 0xD800 && value <= 0xDBFF && index + 1 < length && text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF) {
        const unsigned code_point = 0x10000 + ((value - 0xD800) << 10) + (text[++index] - 0xDC00);
        buffer_ += static_cast<char>(0xF0 | (code_point >> 18));
        buffer_ += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        buffer_ += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        buffer_ += static_cast<char>(0x80 | (code_point & 0x3F));
      } else if (value >= 0xD800 && value <= 0xDFFF) {
        Escape(value);
      } else {
        buffer_ += static_cast<char>(0xE0 | (value >> 12));
        buffer_ += static_cast<char>(0x80 | ((value >> 6) & 0x3F));
        buffer_ += static_cast<char>(0x80 | (value & 0x3F));
      }
    }
    buffer_ += '"';
    FlushIfFull();
  }

  bool Finish() {
    return Flush() && std::fflush(file_) == 0;
  }

 private:

  static constexpr std::size_t kFlushSize = 1024 * 1024;

  void Escape(unsigned value) {
    char escaped[8];
    std::snprintf(escaped, sizeof(escaped), "\\u%04x", value);
    buffer_ += escaped;
  }

  void FlushIfFull() {
    if (buffer_.size() >= kFlushSize)
      Flush();
  }

  bool Flush() {
    if (!ok_)
      return false;
    if (!buffer_.empty() && std::fwrite(buffer_.data(), 1, buffer_.size(), file_) != buffer_.size())
      ok_ = false;
    buffer_.clear();
    return ok_;
  }

  std::FILE* file_;
  std::string buffer_;
  bool ok_ = true;
};

std::wstring NameKey(const CString& name) {
  std::wstring key(name.GetString(), name.GetLength());
  _wcslwr_s(key.data(), key.size() + 1);
  return key;
}

// Preorder keeps each folder's entries together. A node is listed once per
// process even when it links several history slots with that process's name.
// Folder mode lists a written file's folder, or a folder written directly.
void CollectItems(const TreeItem& node,
                  const std::vector<std::size_t>& slot_exports,
                  std::vector<ProcessExport>& exports,
                  ListMode mode) {
  const bool file = node.GetObjectType() == TreeItemType::kFile;
  const TreeItem* listed = mode == ListMode::kFolders && file ? node.GetParent() : &node;
  if (listed && listed->GetParent() && (file || mode != ListMode::kFiles)) {
    for (const auto& link : node.GetDirectProcessWrites()) {
      if (link.process_index >= slot_exports.size())
        continue;
      auto& entry = exports[slot_exports[link.process_index]];
      if (mode == ListMode::kFolders) {
        if (entry.listed_folders.insert(listed).second)
          entry.items.push_back(listed);
      } else if (entry.items.empty() || entry.items.back() != listed) {
        entry.items.push_back(listed);
      }
    }
  }
  for (std::size_t index = 0; index < node.GetChildrenCount(); ++index)
    CollectItems(*node.GetChild(index), slot_exports, exports, mode);
}

void WritePaths(JsonWriter& json, const std::vector<const TreeItem*>& items) {
  CString path;
  json.Raw("[");
  for (std::size_t index = 0; index < items.size(); ++index) {
    path.Empty();
    items[index]->GetFullPath(&path);
    json.Raw(index ? ",\n    " : "\n    ");
    json.String(path.GetString(), path.GetLength());
  }
  json.Raw(items.empty() ? "]" : "\n  ]");
}

// {"<folder path>": ["<name>", ...]}. Folder names end with a backslash, so
// each folder path plus name is the entry's full path, as with --flat.
void WriteGrouped(JsonWriter& json, const std::vector<const TreeItem*>& items) {
  // Preorder interleaves a folder's own entries with its subfolders' entries.
  std::vector<const TreeItem*> folders;
  std::unordered_map<const TreeItem*, std::vector<const TreeItem*>> names_by_folder;
  for (const auto* item : items) {
    auto& names = names_by_folder[item->GetParent()];
    if (names.empty())
      folders.push_back(item->GetParent());
    names.push_back(item);
  }
  CString text;
  json.Raw("{");
  for (std::size_t folder = 0; folder < folders.size(); ++folder) {
    text.Empty();
    folders[folder]->GetFullPath(&text);
    json.Raw(folder ? ",\n    " : "\n    ");
    json.String(text.GetString(), text.GetLength());
    json.Raw(": [");
    const auto& names = names_by_folder[folders[folder]];
    for (std::size_t name = 0; name < names.size(); ++name) {
      // A write to "<folder>\" leaves a nameless child folder; "" stands for the folder itself.
      text = names[name]->GetAlias();
      if (names[name]->GetObjectType() == TreeItemType::kFolder && !text.IsEmpty())
        text += L'\\';
      json.Raw(name ? ", " : "");
      json.String(text.GetString(), text.GetLength());
    }
    json.Raw("]");
  }
  json.Raw(folders.empty() ? "}" : "\n  }");
}

CString DefaultHistoryPath() {
  wchar_t* app_data = nullptr;
  CString path;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &app_data)))
    path = CString(app_data) + L"\\CEZEO software\\SsdReady\\Monitored.Files";
  CoTaskMemFree(app_data);
  return path;
}

std::string ErrorText(DWORD error) {
  return std::system_category().message(static_cast<int>(error)) + " (" + std::to_string(error) + ")";
}

int Usage() {
  std::wcerr << L"Usage: history_export [--include-folders | --folders-only] [--flat] [<history file>] <output.json>\n"
                L"The default history file is %APPDATA%\\CEZEO software\\SsdReady\\Monitored.Files.\n"
                L"\"files\" maps each folder path to the names written in it.\n"
                L"--include-folders also lists folders that received writes directly.\n"
                L"--folders-only lists only the folders where writes happened, under \"folders\".\n"
                L"--flat writes \"files\" as a list of full paths instead.\n";
  return 2;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (!AfxWinInit(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_HIDE)) {
    std::wcerr << L"MFC initialization failed.\n";
    return 1;
  }
  ListMode mode = ListMode::kFiles;
  bool flat = false;
  std::vector<CString> paths;
  for (int index = 1; index < argc; ++index) {
    const CString argument(argv[index]);
    if (argument == L"--include-folders" || argument == L"--folders-only") {
      const auto requested = argument == L"--folders-only" ? ListMode::kFolders : ListMode::kFilesAndFolders;
      if (mode != ListMode::kFiles && mode != requested)
        return Usage();
      mode = requested;
    } else if (argument == L"--flat") {
      flat = true;
    } else if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
      return Usage();
    } else {
      paths.push_back(argument);
    }
  }
  // A folder list is already flat; grouping would only repeat its parents.
  const bool grouped = !flat && mode != ListMode::kFolders;
  const char* const list_key = mode == ListMode::kFolders ? ", \"folders\": " : ", \"files\": ";
  if (paths.empty() || paths.size() > 2)
    return Usage();
  const CString input = paths.size() == 2 ? paths[0] : DefaultHistoryPath();
  const CString output = paths.back();

  const auto started = std::chrono::steady_clock::now();
  MonitoringStatistics statistics;
  ProcessStatisticsStore processes;
  FolderTreeItem root;
  {
    BufferedFile file(FALSE, FALSE);
    if (!file.Open(input, File::kModeRead | File::kShareDenyWrite)) {
      std::wcerr << L"Cannot open " << input.GetString() << L": ";
      std::cerr << ErrorText(GetLastError()) << '\n';
      return 1;
    }
    if (!history_format::Load(file, statistics, processes, root)) {
      std::wcerr << L"Cannot read history " << input.GetString() << L": ";
      std::cerr << ErrorText(GetLastError()) << '\n';
      return 1;
    }
  }
  const auto loaded = std::chrono::steady_clock::now();

  // Duplicate history slots for one executable name merge into one key.
  std::vector<ProcessExport> exports;
  std::vector<std::size_t> slot_exports;
  std::unordered_map<std::wstring, std::size_t> exports_by_name;
  for (const auto& record : processes.records()) {
    const auto inserted = exports_by_name.emplace(NameKey(record.name), exports.size());
    if (inserted.second)
      exports.push_back({record.name});
    exports[inserted.first->second].written_bytes += record.written_bytes;
    slot_exports.push_back(inserted.first->second);
  }
  CollectItems(root, slot_exports, exports, mode);
  for (auto& entry : exports)
    entry.listed_folders = {};

  std::vector<const ProcessExport*> ordered;
  ordered.reserve(exports.size());
  for (const auto& entry : exports)
    ordered.push_back(&entry);
  std::stable_sort(ordered.begin(), ordered.end(), [](const ProcessExport* left, const ProcessExport* right) {
    return left->written_bytes > right->written_bytes;
  });

  std::FILE* stream = nullptr;
  if (_wfopen_s(&stream, output, L"wb") != 0 || !stream) {
    std::wcerr << L"Cannot create " << output.GetString() << L".\n";
    return 1;
  }
  JsonWriter json(stream);
  std::size_t listed = 0;
  json.Raw("{");
  for (std::size_t process = 0; process < ordered.size(); ++process) {
    const auto& entry = *ordered[process];
    json.Raw(process ? ",\n  " : "\n  ");
    json.String(entry.name.GetString(), entry.name.GetLength());
    json.Raw(": {\"total_written_bytes\": ");
    json.Number(entry.written_bytes);
    json.Raw(list_key);
    if (grouped)
      WriteGrouped(json, entry.items);
    else
      WritePaths(json, entry.items);
    json.Raw("}");
    listed += entry.items.size();
  }
  json.Raw(ordered.empty() ? "}\n" : "\n}\n");
  const bool written = json.Finish();
  if (std::fclose(stream) != 0 || !written) {
    std::wcerr << L"Writing " << output.GetString() << L" failed.\n";
    return 1;
  }

  const auto finished = std::chrono::steady_clock::now();
  std::wcerr << L"Exported " << exports.size() << L" processes and " << listed << (mode == ListMode::kFolders ? L" folder" : L" path") << L" entries from "
             << statistics.items << L" history items to " << output.GetString() << L" (load "
             << std::chrono::duration<double>(loaded - started).count() << L" s, total "
             << std::chrono::duration<double>(finished - started).count() << L" s).\n";
  return 0;
}
