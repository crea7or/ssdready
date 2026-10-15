// clang-format off
#include "pch.h"
// clang-format on
#include "history_format.h"

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "buffered_file.h"

namespace {
CWinApp application;

void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

class MemoryFile : public File {
 public:

  MemoryFile() : File(false, false) {
  }

  std::vector<unsigned char> bytes;
  std::size_t position = 0;

  BOOL Read(void* target, std::size_t count) override {
    if (count > bytes.size() - position) {
      SetLastError(ERROR_HANDLE_EOF);
      return FALSE;
    }
    std::memcpy(target, bytes.data() + position, count);
    position += count;
    return TRUE;
  }

  BOOL Write(const void* source, std::size_t count) override {
    const auto* data = static_cast<const unsigned char*>(source);
    bytes.insert(bytes.end(), data, data + count);
    return TRUE;
  }

  std::uint64_t GetLength() override {
    return bytes.size();
  }
};

class HashFile : public File {
 public:

  HashFile() : File(false, false) {
  }

  std::uint64_t hash = 14695981039346656037ULL;

  BOOL Write(const void* source, std::size_t count) override {
    const auto* data = static_cast<const unsigned char*>(source);
    for (std::size_t index = 0; index < count; ++index) {
      hash ^= data[index];
      hash *= 1099511628211ULL;
    }
    return TRUE;
  }
};

class FailingFile : public MemoryFile {
 public:

  explicit FailingFile(std::size_t successful_writes) : remaining_(successful_writes) {
  }

  BOOL Write(const void* source, std::size_t count) override {
    if (!remaining_) {
      SetLastError(ERROR_DISK_FULL);
      return FALSE;
    }
    --remaining_;
    return MemoryFile::Write(source, count);
  }

 private:

  std::size_t remaining_;
};

struct Model {
  MonitoringStatistics statistics{};
  ProcessStatisticsStore processes;
  FolderTreeItem root;
};

std::uint64_t Fingerprint(const Model& model) {
  HashFile hash;
  Check(hash.Write(&model.statistics, sizeof(model.statistics)) && model.processes.Save(hash) && model.root.Save(&hash),
        "Fingerprint full model");
  return hash.hash;
}

template <typename T>
void Append(MemoryFile& file, const T& value) {
  Check(file.Write(&value, sizeof(value)), "Append fixture");
}

void LegacyString(MemoryFile& file, const CString& name) {
  Append(file, DWORD(name.GetLength() * 2));
  Check(file.Write(name.GetString(), name.GetLength() * 2), "Fixture UTF-16");
}

void Node(MemoryFile& file,
          const CString& name,
          std::uint64_t total,
          std::uint64_t direct,
          std::uint64_t rounded,
          const std::vector<ProcessWriteStatistics>& links,
          DWORD children) {
  Append(file, DWORD(0));
  LegacyString(file, name);
  Append(file, total);
  Append(file, direct);
  Append(file, rounded);
  Append(file, DWORD(links.size()));
  for (const auto& link : links) {
    Append(file, link.written_bytes);
    Append(file, DWORD(link.process_index));
  }
  Append(file, children);
}

void MakeFixture(MemoryFile& file, std::size_t children, const CString& root_name = CString(L"C:")) {
  Append(file, DWORD(1));
  MonitoringStatistics statistics{100, children, 999, children * 5, children * 4096, 123456789, 555};
  Append(file, statistics);
  Append(file, DWORD(2));
  for (DWORD index = 0; index < 2; ++index) {
    Append(file, std::uint64_t(index == 0 ? UINT64_MAX : 0));
    Append(file, index);
    LegacyString(file, CString(index == 0 ? L"Tool.exe" : L"TOOL.EXE"));
  }
  Node(file, root_name, children * 5, 0, 0, {}, DWORD(children));
  for (std::size_t index = 0; index < children; ++index) {
    Append(file, DWORD(TreeItemType::kFile));
    CString name;
    name.Format(L"repeated-long-file-name-%06zu-\u00e9-\u6f22-\xd83d\xde03-\xd800.dat", index);
    Node(file, name, 5, 5, index % 2 == 0 ? 4096 : UINT64_MAX, {{5, index % 2}}, 0);
  }
}

bool Decode(MemoryFile& file, Model& model) {
  file.position = 0;
  return history_format::Load(file, model.statistics, model.processes, model.root) != FALSE;
}

void TestInvalidPayloads(const MemoryFile& header_source) {
  // Build raw frames independently, with valid CRCs, to test semantic validation.
  auto invalid = [&](const std::vector<unsigned char>& payload) {
    MemoryFile file;
    file.bytes.assign(header_source.bytes.begin(), header_source.bytes.begin() + 68);
    Append(file, DWORD(payload.size()));
    Append(file, DWORD(payload.size()));
    DWORD crc = 0xffffffff;
    for (auto byte : payload) {
      crc ^= byte;
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    Append(file, crc ^ 0xffffffffu);
    Check(file.Write(payload.data(), payload.size()), "Write invalid payload");
    Append(file, std::array<DWORD, 3>{});
    Model model;
    Check(!Decode(file, model), "Invalid payload with valid checksum is rejected");
  };
  invalid({0x80, 0, 3, 0, 0, 0, 0, 0});                                                  // Noncanonical count.
  invalid({0x80, 0x80, 0x80, 0x80, 0x10});                                               // Count above UINT32_MAX.
  invalid({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 2});                    // Varint overflow.
  invalid({0, 4, 0, 0, 0, 0, 0});                                                        // Unknown flags.
  invalid({0, 3, 0x82, 0x80, 4});                                                        // Oversized string.
  invalid({0, 3, 0, 0, 0, 1, 0, 0, 0});                                                  // Link without a process.
  invalid({0, 3, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1, 0, 0});  // Rounded counter overflow.
  invalid({0, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1});                                      // File with children.
  invalid({0, 3, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0});
  std::vector<unsigned char> deep{0};
  for (int depth = 0; depth < 258; ++depth)
    deep.insert(deep.end(), {3, 0, 0, 0, 0, static_cast<unsigned char>(depth == 257 ? 0 : 1)});
  invalid(deep);
}

void TestFormats() {
  MemoryFile legacy;
  MakeFixture(legacy, 2);
  Model original;
  Check(Decode(legacy, original), "Independent version 1 fixture loads");
  const auto fingerprint = Fingerprint(original);
  MemoryFile compact;
  Check(history_format::Save(compact, original.statistics, original.processes, original.root), "Save version 2 fixture");
  Model restored;
  Check(Decode(compact, restored) && Fingerprint(restored) == fingerprint,
        "Version 2 preserves Unicode, unpaired surrogates, duplicate process slots and 64-bit counters");
  auto lock = Core()->GetManager()->LockData();
  const auto before = restored.root.GetWriteSizeTotal();
  CStringArray path;
  CString first;
  first.Format(L"repeated-long-file-name-%06zu-\u00e9-\u6f22-\xd83d\xde03-\xd800.dat", std::size_t(0));
  path.Add(first);
  auto* child = restored.root.GetItemByPath(&path, 0, TRUE);
  Check(child->GetParent() == &restored.root, "Loaded nodes have repaired parent pointers");
  child->AddWrite(7, 0);
  Check(restored.root.GetWriteSizeTotal() == before + 7, "New writes propagate through restored parents");
  lock.unlock();
  MemoryFile empty;
  Model blank;
  Check(history_format::Save(empty, blank.statistics, blank.processes, blank.root), "Save empty tree");
  DWORD raw, stored;
  std::memcpy(&raw, empty.bytes.data() + 68, 4);
  std::memcpy(&stored, empty.bytes.data() + 72, 4);
  Check(raw == stored, "Small incompressible block uses raw storage");
  Model empty_restored;
  Check(Decode(empty, empty_restored), "Raw version 2 block loads");
  for (std::size_t writes = 0; writes < 7; ++writes) {
    FailingFile failed(writes);
    Check(!history_format::Save(failed, blank.statistics, blank.processes, blank.root) && GetLastError() == ERROR_DISK_FULL,
          "Header, raw block and final marker write failures propagate");
  }
  TestInvalidPayloads(empty);
  const wchar_t latin1[] = {L'C', 0, L'\u00e9'};
  MemoryFile latin_legacy;
  MakeFixture(latin_legacy, 0, CString(latin1, 3));
  Model latin;
  Check(Decode(latin_legacy, latin), "Load Latin-1 and embedded NUL fixture");
  MemoryFile latin_compact;
  Check(history_format::Save(latin_compact, latin.statistics, latin.processes, latin.root), "Save Latin-1 fixture");
  Model latin_restored;
  Check(Decode(latin_compact, latin_restored) && Fingerprint(latin_restored) == Fingerprint(latin), "Latin-1 and embedded NUL round-trip");
  for (std::size_t length = 0; length < empty.bytes.size(); ++length) {
    MemoryFile truncated;
    truncated.bytes.assign(empty.bytes.begin(), empty.bytes.begin() + length);
    Model target;
    Check(!Decode(truncated, target), "Every truncated file fails");
  }
  for (const std::size_t offset : {std::size_t(0),
                                   std::size_t(4),
                                   std::size_t(60),
                                   std::size_t(64),
                                   std::size_t(68),
                                   std::size_t(72),
                                   std::size_t(76),
                                   std::size_t(80)}) {
    MemoryFile corrupt;
    corrupt.bytes = empty.bytes;
    corrupt.bytes[offset] ^= 0x80;
    Model target;
    Check(!Decode(corrupt, target), "Corrupt version, marker, lengths, checksum and payload fail");
  }
  MemoryFile trailing;
  trailing.bytes = empty.bytes;
  trailing.bytes.push_back(0);
  Model target;
  Check(!Decode(trailing, target), "Trailing data is rejected");
  MemoryFile large_legacy;
  MakeFixture(large_legacy, 25000);
  Model large;
  Check(Decode(large_legacy, large), "Load block-boundary fixture");
  MemoryFile large_compact;
  Check(history_format::Save(large_compact, large.statistics, large.processes, large.root), "Save compressed multi-block fixture");
  Model large_restored;
  Check(Decode(large_compact, large_restored) && Fingerprint(large_restored) == Fingerprint(large),
        "Multi-block compression and exact model round-trip");
  Check(large_compact.bytes.size() < large_legacy.bytes.size() / 2, "Version 2 reduces disk size");
  for (std::size_t writes : {std::size_t(4), std::size_t(5), std::size_t(6), std::size_t(7)}) {
    FailingFile failed(writes);
    Check(!history_format::Save(failed, large.statistics, large.processes, large.root) && GetLastError() == ERROR_DISK_FULL,
          "Background compression/write failures propagate to the saving thread");
  }
  large_compact.bytes[80] ^= 0x40;
  Model damaged;
  Check(!Decode(large_compact, damaged), "Corrupted compressed data fails safely");
  std::cout << "V1 compatibility, V2 round-trip, raw/compressed multi-block data, corruption and Unicode passed.\n";
}

double LoadPath(const std::filesystem::path& path, Model& model) {
  const auto start = std::chrono::steady_clock::now();
  BufferedFile file(false, false);
  Check(file.Open(path.c_str(), File::kModeRead | File::kShareDenyNone) &&
            history_format::Load(file, model.statistics, model.processes, model.root) && file.Close(),
        "Benchmark load");
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

double SavePath(const std::filesystem::path& path, const Model& model, bool compact) {
  const auto start = std::chrono::steady_clock::now();
  if (compact) {
    File file(false, false);
    Check(file.Open(path.c_str(), File::kModeWrite | File::kCreate) &&
              history_format::Save(file, model.statistics, model.processes, model.root) && file.Close(),
          "Benchmark V2 save");
  } else {
    BufferedFile file(false, false);
    const DWORD version = 1;
    Check(file.Open(path.c_str(), File::kModeWrite | File::kCreate) && file.Write(&version, sizeof(version)) &&
              file.Write(&model.statistics, sizeof(model.statistics)) && model.processes.Save(file) && model.root.Save(&file) &&
              file.Close(),
          "Benchmark V1 save");
  }
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void Benchmark(const std::filesystem::path& input, const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  const auto v1 = directory / L"history-v1.files", v2 = directory / L"history-v2.files";
  Check(std::filesystem::weakly_canonical(input) != std::filesystem::weakly_canonical(v1) &&
            std::filesystem::weakly_canonical(input) != std::filesystem::weakly_canonical(v2),
        "Benchmark outputs must differ from input");
  Model model;
  const auto initial = LoadPath(input, model);
  const auto expected = Fingerprint(model);
  std::cout << "Initial load: " << initial << "s; input bytes=" << std::filesystem::file_size(input)
            << "; processes=" << model.processes.records().size() << std::endl;
  std::array<double, 4> totals{};
  for (int run = 0; run < 3; ++run) {
    const auto save1 = SavePath(v1, model, false), save2 = SavePath(v2, model, true);
    double load1, load2;
    {
      Model loaded;
      load1 = LoadPath(v1, loaded);
      Check(Fingerprint(loaded) == expected, "V1 benchmark model equality");
    }
    {
      Model loaded;
      load2 = LoadPath(v2, loaded);
      Check(Fingerprint(loaded) == expected, "V2 benchmark model equality");
    }
    totals[0] += save1;
    totals[1] += save2;
    totals[2] += load1;
    totals[3] += load2;
    std::cout << "run=" << run + 1 << " save_v1_s=" << save1 << " save_v2_s=" << save2 << " load_v1_s=" << load1 << " load_v2_s=" << load2
              << std::endl;
  }
  std::cout << "mean save_v1_s=" << totals[0] / 3 << " save_v2_s=" << totals[1] / 3 << " load_v1_s=" << totals[2] / 3
            << " load_v2_s=" << totals[3] / 3 << " v1_bytes=" << std::filesystem::file_size(v1)
            << " v2_bytes=" << std::filesystem::file_size(v2) << std::endl;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    Check(AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0), "Initialize MFC");
    ApplicationCore core(ApplicationCore::StartupMode::kHeadless);
    g_application_core = &core;
    if (argc == 3)
      Benchmark(argv[1], argv[2]);
    else {
      Check(argc == 1, "Usage: history_format_test [input output-directory]");
      TestFormats();
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << ": Windows error " << GetLastError() << std::endl;
    return 1;
  } catch (CException* error) {
    error->Delete();
    std::cerr << "MFC exception\n";
    return 1;
  }
}
