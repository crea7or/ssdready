// clang-format off
#include "pch.h"
// clang-format on
#include "history_format.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <limits>
#include <span>
#include <utility>

namespace {
constexpr DWORD kVersion = 2;
constexpr DWORD kMagic = 0x32524853;  // "SHR2", little endian.
constexpr std::size_t kBlockSize = 1024 * 1024;
constexpr std::size_t kMaxStringLength = 32768;
constexpr std::size_t kMaxDepth = 256;

BOOL Invalid(DWORD error = ERROR_INVALID_DATA) {
  SetLastError(error);
  return FALSE;
}

std::uint32_t Checksum(std::span<const unsigned char> bytes) {
  static constexpr auto table = [] {
    std::array<std::array<std::uint32_t, 256>, 8> values{};
    for (std::uint32_t index = 0; index < values[0].size(); ++index) {
      auto value = index;
      for (int bit = 0; bit < 8; ++bit)
        value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
      values[0][index] = value;
    }
    for (std::uint32_t index = 0; index < values[0].size(); ++index) {
      auto value = values[0][index];
      for (std::size_t slice = 1; slice < values.size(); ++slice) {
        value = values[0][value & 255] ^ (value >> 8);
        values[slice][index] = value;
      }
    }
    return values;
  }();
  std::uint32_t value = 0xffffffffu;
  // Eight independent lookups avoid the dependency chain of byte-at-a-time CRC.
  while (bytes.size() >= 8) {
    std::uint32_t first, second;
    std::memcpy(&first, bytes.data(), sizeof(first));
    std::memcpy(&second, bytes.data() + 4, sizeof(second));
    first ^= value;
    value = table[7][first & 255] ^ table[6][(first >> 8) & 255] ^ table[5][(first >> 16) & 255] ^ table[4][first >> 24] ^
            table[3][second & 255] ^ table[2][(second >> 8) & 255] ^ table[1][(second >> 16) & 255] ^ table[0][second >> 24];
    bytes = bytes.subspan(8);
  }
  for (auto byte : bytes)
    value = table[0][(value ^ byte) & 255] ^ (value >> 8);
  return value ^ 0xffffffffu;
}

// Dynamically loaded so older Windows can still read v1 and uncompressed v2.
// Buffer mode XPRESS is fast and needs no external library installation.
class CompressionApi {
 public:

  using Create = BOOL(WINAPI*)(DWORD, void*, HANDLE*);
  using Transform = BOOL(WINAPI*)(HANDLE, const void*, SIZE_T, void*, SIZE_T, SIZE_T*);
  using Close = BOOL(WINAPI*)(HANDLE);

  CompressionApi() {
    wchar_t directory[MAX_PATH];
    const UINT length = GetSystemDirectoryW(directory, _countof(directory));
    if (!length || length >= _countof(directory))
      return;
    const std::wstring path = std::wstring(directory, length) + L"\\cabinet.dll";
    module_ = LoadLibraryW(path.c_str());
    if (!module_)
      return;
    create_compressor = reinterpret_cast<Create>(GetProcAddress(module_, "CreateCompressor"));
    create_decompressor = reinterpret_cast<Create>(GetProcAddress(module_, "CreateDecompressor"));
    compress = reinterpret_cast<Transform>(GetProcAddress(module_, "Compress"));
    decompress = reinterpret_cast<Transform>(GetProcAddress(module_, "Decompress"));
    close_compressor = reinterpret_cast<Close>(GetProcAddress(module_, "CloseCompressor"));
    close_decompressor = reinterpret_cast<Close>(GetProcAddress(module_, "CloseDecompressor"));
  }

  ~CompressionApi() {
    if (module_)
      FreeLibrary(module_);
  }

  bool available() const {
    return create_compressor && create_decompressor && compress && decompress && close_compressor && close_decompressor;
  }

  static const CompressionApi& Get() {
    static CompressionApi api;
    return api;
  }

  Create create_compressor = nullptr, create_decompressor = nullptr;
  Transform compress = nullptr, decompress = nullptr;
  Close close_compressor = nullptr, close_decompressor = nullptr;

 private:

  HMODULE module_ = nullptr;
};

class CompressionHandle {
 public:

  explicit CompressionHandle(bool writing) : writing_(writing) {
    const auto& api = CompressionApi::Get();
    if (api.available())
      (writing ? api.create_compressor : api.create_decompressor)(3 /* XPRESS, buffer mode */, nullptr, &handle_);
  }

  ~CompressionHandle() {
    const DWORD error = GetLastError();
    if (handle_) {
      const auto& api = CompressionApi::Get();
      (writing_ ? api.close_compressor : api.close_decompressor)(handle_);
    }
    SetLastError(error);
  }

  HANDLE get() const {
    return handle_;
  }

 private:

  bool writing_;
  HANDLE handle_ = nullptr;
};

class Writer {
 public:

  explicit Writer(File& file) : file_(file), raw_(kBlockSize), pending_(kBlockSize), compressed_(kBlockSize), compressor_(true) {
  }

  bool Bytes(const void* data, std::size_t size) {
    auto* input = static_cast<const unsigned char*>(data);
    while (size) {
      const auto count = (std::min)(size, raw_.size() - position_);
      std::memcpy(raw_.data() + position_, input, count);
      position_ += count;
      input += count;
      size -= count;
      if (position_ == raw_.size() && !Flush())
        return false;
    }
    return true;
  }

  bool Number(std::uint64_t value) {
    if (raw_.size() - position_ >= 10) {
      do {
        raw_[position_++] = static_cast<unsigned char>((value & 127) | (value >= 128 ? 128 : 0));
        value >>= 7;
      } while (value);
      return true;
    }
    unsigned char bytes[10];
    std::size_t count = 0;
    do {
      bytes[count++] = static_cast<unsigned char>((value & 127) | (value >= 128 ? 128 : 0));
      value >>= 7;
    } while (value);
    return Bytes(bytes, count);
  }

  bool String(const CString& value) {
    const auto length = static_cast<std::size_t>(value.GetLength());
    if (length > kMaxStringLength)
      return Invalid();
    bool wide = false;
    const auto* source = value.GetString();
    for (std::size_t index = 0; index < length; ++index)
      if (source[index] > 255) {
        wide = true;
        break;
      }
    if (!Number((length << 1) | static_cast<std::size_t>(wide)))
      return false;
    if (wide)
      return Bytes(value.GetString(), length * sizeof(wchar_t));
    // Widening/narrowing Latin-1 is lossless, including NULs. No locale codec.
    std::size_t offset = 0;
    while (offset < length) {
      const auto count = (std::min)(length - offset, raw_.size() - position_);
      auto* target = raw_.data() + position_;
      for (std::size_t index = 0; index < count; ++index)
        target[index] = static_cast<unsigned char>(source[offset + index]);
      position_ += count;
      offset += count;
      if (position_ == raw_.size() && !Flush())
        return false;
    }
    return true;
  }

  bool Finish() {
    const std::array<DWORD, 3> end{};
    return Flush() && Wait() && file_.Write(end.data(), sizeof(end));
  }

 private:

  bool Flush() {
    if (!position_)
      return true;
    if (!Wait())
      return false;
    raw_.swap(pending_);
    const auto count = std::exchange(position_, 0);
    if (count < kBlockSize / 4)
      return WriteBlock(count);
    // Encode the next block while a single worker compresses/writes this one.
    // Waiting before each swap keeps memory bounded and preserves block order.
    try {
      pending_write_ = std::async(std::launch::async, [this, count] {
        return WriteBlock(count) ? DWORD(ERROR_SUCCESS) : (GetLastError() ? GetLastError() : DWORD(ERROR_WRITE_FAULT));
      });
    } catch (const std::system_error&) {
      // Thread creation failure still permits a synchronous, lossless save.
      return WriteBlock(count);
    }
    return true;
  }

  bool Wait() {
    if (!pending_write_.valid())
      return true;
    const DWORD error = pending_write_.get();
    return error == ERROR_SUCCESS ? true : Invalid(error);
  }

  bool WriteBlock(std::size_t count) {
    SIZE_T stored = 0;
    bool compressed = false;
    if (compressor_.get()) {
      compressed = CompressionApi::Get().compress(
                       compressor_.get(), pending_.data(), count, compressed_.data(), compressed_.size(), &stored) != FALSE;
      if (!compressed && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return false;
      compressed = compressed && stored < count;
    }
    const std::array<DWORD, 3> header{
        static_cast<DWORD>(count), static_cast<DWORD>(compressed ? stored : count), Checksum(std::span(pending_.data(), count))};
    if (!file_.Write(header.data(), sizeof(header)) || !file_.Write(compressed ? compressed_.data() : pending_.data(), header[1]))
      return false;
    return true;
  }

  File& file_;
  std::vector<unsigned char> raw_, pending_, compressed_;
  CompressionHandle compressor_;
  std::size_t position_ = 0;
  // Declared last: destruction joins the worker before releasing its buffers.
  std::future<DWORD> pending_write_;
};

class Reader {
 public:

  Reader(File& file, std::uint64_t remaining)
      : file_(file), remaining_(remaining), raw_(kBlockSize), compressed_(kBlockSize), decompressor_(false) {
  }

  bool Bytes(void* target, std::size_t size) {
    auto* output = static_cast<unsigned char*>(target);
    while (size) {
      if (position_ == available_ && !Fill())
        return false;
      const auto count = (std::min)(size, available_ - position_);
      std::memcpy(output, raw_.data() + position_, count);
      output += count;
      position_ += count;
      size -= count;
    }
    return true;
  }

  bool Number(std::uint64_t& value) {
    value = 0;
    for (int shift = 0; shift <= 63; shift += 7) {
      if (position_ == available_ && !Fill())
        return false;
      const auto byte = raw_[position_++];
      if (shift == 63 && byte > 1)
        return Invalid();
      value |= std::uint64_t(byte & 127) << shift;
      if (!(byte & 128))
        return (shift == 0 || byte != 0) ? true : Invalid();
    }
    return Invalid();
  }

  bool Count(std::size_t& count) {
    std::uint64_t value;
    if (!Number(value))
      return false;
    if (value > (std::numeric_limits<std::uint32_t>::max)() || value > (std::numeric_limits<std::size_t>::max)())
      return Invalid();
    count = static_cast<std::size_t>(value);
    return true;
  }

  bool String(CString& value) {
    std::uint64_t tag;
    if (!Number(tag))
      return false;
    if ((tag >> 1) > kMaxStringLength)
      return Invalid();
    const auto length = static_cast<int>(tag >> 1);
    auto* target = value.GetBufferSetLength(length);
    bool ok = true;
    if (tag & 1) {
      ok = Bytes(target, static_cast<std::size_t>(length) * sizeof(wchar_t));
    } else {
      std::size_t offset = 0;
      while (offset < static_cast<std::size_t>(length)) {
        if (position_ == available_ && !Fill()) {
          ok = false;
          break;
        }
        const auto count = (std::min)(static_cast<std::size_t>(length) - offset, available_ - position_);
        const auto* source = raw_.data() + position_;
        for (std::size_t index = 0; index < count; ++index)
          target[offset + index] = source[index];
        position_ += count;
        offset += count;
      }
    }
    value.ReleaseBuffer(length);
    return ok;
  }

  bool Finish() {
    if (position_ != available_ || remaining_ != 12)
      return Invalid();
    std::array<DWORD, 3> end;
    return Physical(end.data(), sizeof(end)) && (end == std::array<DWORD, 3>{} || Invalid());
  }

 private:

  bool Physical(void* target, std::size_t size) {
    if (size > remaining_)
      return Invalid();
    if (!file_.Read(target, size))
      return false;
    remaining_ -= size;
    return true;
  }

  bool Fill() {
    std::array<DWORD, 3> header;
    if (!Physical(header.data(), sizeof(header)))
      return false;
    if (!header[0] || header[0] > kBlockSize || !header[1] || header[1] > header[0])
      return Invalid();
    if (header[0] == header[1]) {
      if (!Physical(raw_.data(), header[1]))
        return false;
    } else {
      if (!decompressor_.get())
        return Invalid(ERROR_NOT_SUPPORTED);
      if (!Physical(compressed_.data(), header[1]))
        return false;
      SIZE_T decoded = 0;
      if (!CompressionApi::Get().decompress(decompressor_.get(), compressed_.data(), header[1], raw_.data(), header[0], &decoded) ||
          decoded != header[0])
        return Invalid();
    }
    if (Checksum(std::span(raw_.data(), header[0])) != header[2])
      return Invalid();
    position_ = 0;
    available_ = header[0];
    return true;
  }

  File& file_;
  std::uint64_t remaining_;
  std::vector<unsigned char> raw_, compressed_;
  CompressionHandle decompressor_;
  std::size_t position_ = 0, available_ = 0;
};
}  // namespace

class HistoryCodec {
 public:

  static bool Save(Writer& writer, const ProcessStatisticsStore& processes, const FolderTreeItem& root) {
    if (processes.records_.size() > MAXDWORD)
      return Invalid();
    if (!writer.Number(processes.records_.size()))
      return false;
    for (const auto& process : processes.records_)
      if (!writer.String(process.name) || !writer.Number(process.written_bytes))
        return false;
    std::uint64_t total;
    return SaveNode(writer, root, processes.records_.size(), 0, total) && writer.Finish();
  }

  static bool Load(Reader& reader, ProcessStatisticsStore& processes, FolderTreeItem& root) {
    std::size_t count;
    if (!reader.Count(count))
      return false;
    // Never reserve an untrusted, potentially enormous count before decoding.
    processes.records_.reserve((std::min)(count, std::size_t(4096)));
    processes.indexes_by_name_.reserve((std::min)(count, std::size_t(4096)));
    for (std::size_t index = 0; index < count; ++index) {
      ProcessStatistics record;
      record.process_index = index;
      if (!reader.String(record.name) || !reader.Number(record.written_bytes))
        return false;
      processes.indexes_by_name_.emplace(ProcessStatisticsStore::NameKey(record.name), index);
      processes.records_.push_back(std::move(record));
    }
    return LoadNode(reader, root, processes.records_.size(), 0) && reader.Finish();
  }

 private:

  static bool SaveNode(Writer& writer, const TreeItem& node, std::size_t process_count, std::size_t depth, std::uint64_t& total) {
    if (depth > kMaxDepth || node.process_links_.size() > MAXDWORD)
      return Invalid();
    total = node.values_.written_bytes;
    const bool aligned = (node.values_.rounded_bytes & 4095) == 0;
    if (!writer.Number(static_cast<DWORD>(node.GetObjectType()) | (aligned ? 2 : 0)) || !writer.String(node.alias_) ||
        !writer.Number(node.values_.written_bytes) ||
        !writer.Number(aligned ? node.values_.rounded_bytes >> 12 : node.values_.rounded_bytes) ||
        !writer.Number(node.process_links_.size()))
      return false;
    for (const auto& link : node.process_links_) {
      if (link.process_index >= process_count)
        return Invalid();
      if (!writer.Number(link.process_index) || !writer.Number(link.written_bytes))
        return false;
    }
    const auto& children = node.GetChildren();
    if (children.size() > MAXDWORD)
      return Invalid();
    if (!writer.Number(children.size()))
      return false;
    for (const auto& child : children) {
      std::uint64_t child_total;
      if (!SaveNode(writer, *child, process_count, depth + 1, child_total))
        return false;
      if (child_total > (std::numeric_limits<std::uint64_t>::max)() - total)
        return Invalid();
      total += child_total;
    }
    return true;
  }

  static bool LoadNode(
      Reader& reader, TreeItem& node, std::size_t process_count, std::size_t depth, std::optional<std::uint64_t> tag = {}) {
    if (depth > kMaxDepth)
      return Invalid();
    std::uint64_t flags = tag.value_or(0);
    if (!tag && !reader.Number(flags))
      return false;
    if (flags > 3 || (flags & 1) != static_cast<DWORD>(node.GetObjectType()))
      return Invalid();
    std::uint64_t rounded;
    if (!reader.String(node.alias_) || !reader.Number(node.values_.written_bytes) || !reader.Number(rounded))
      return false;
    if ((flags & 2) && rounded > ((std::numeric_limits<std::uint64_t>::max)() >> 12))
      return Invalid();
    node.values_.rounded_bytes = flags & 2 ? rounded << 12 : rounded;
    node.values_.total_with_children = node.values_.written_bytes;
    std::size_t count;
    if (!reader.Count(count))
      return false;
    node.process_links_.reserve((std::min)(count, std::size_t(4096)));
    for (std::size_t index = 0; index < count; ++index) {
      ProcessWriteStatistics link;
      if (!reader.Count(link.process_index))
        return false;
      if (link.process_index >= process_count)
        return Invalid();
      if (!reader.Number(link.written_bytes))
        return false;
      node.process_links_.push_back(link);
    }
    if (!reader.Count(count))
      return false;
    if (node.GetObjectType() == TreeItemType::kFile && count)
      return Invalid();
    TreeItem::Children children;
    children.reserve((std::min)(count, std::size_t(4096)));
    for (std::size_t index = 0; index < count; ++index) {
      std::uint64_t child_tag;
      if (!reader.Number(child_tag))
        return false;
      if (child_tag > 3)
        return Invalid();
      std::unique_ptr<TreeItem> child;
      if (child_tag & 1)
        child = std::make_unique<FolderTreeItem>();
      else
        child = std::make_unique<FileTreeItem>();
      if (!LoadNode(reader, *child, process_count, depth + 1, child_tag))
        return false;
      child->parent_ = &node;
      if (child->GetWriteSizeTotal() > (std::numeric_limits<std::uint64_t>::max)() - node.values_.total_with_children)
        return Invalid();
      node.values_.total_with_children += child->GetWriteSizeTotal();
      children.push_back(std::move(child));
    }
    node.ReplaceChildren(std::move(children));
    return true;
  }
};

BOOL history_format::Save(File& file,
                          const MonitoringStatistics& statistics,
                          const ProcessStatisticsStore& processes,
                          const FolderTreeItem& root) {
  const auto checksum = Checksum(std::span(reinterpret_cast<const unsigned char*>(&statistics), sizeof(statistics)));
  if (!file.Write(&kVersion, sizeof(kVersion)) || !file.Write(&statistics, sizeof(statistics)) || !file.Write(&kMagic, sizeof(kMagic)) ||
      !file.Write(&checksum, sizeof(checksum)))
    return FALSE;
  Writer writer(file);
  return HistoryCodec::Save(writer, processes, root);
}

BOOL history_format::Load(File& file, MonitoringStatistics& statistics, ProcessStatisticsStore& processes, FolderTreeItem& root) {
  DWORD version;
  if (!file.Read(&version, sizeof(version)))
    return FALSE;
  if (version != 1 && version != kVersion)
    return Invalid();
  if (!file.Read(&statistics, sizeof(statistics)))
    return FALSE;
  if (version == 1)
    return processes.Load(file) && root.Load(&file);
  DWORD magic;
  if (!file.Read(&magic, sizeof(magic)))
    return FALSE;
  if (magic != kMagic)
    return Invalid();
  DWORD checksum;
  if (!file.Read(&checksum, sizeof(checksum)))
    return FALSE;
  if (checksum != Checksum(std::span(reinterpret_cast<const unsigned char*>(&statistics), sizeof(statistics))))
    return Invalid();
  const auto size = file.GetLength();
  constexpr std::size_t header_size = 3 * sizeof(DWORD) + sizeof(MonitoringStatistics);
  if (size < header_size)
    return Invalid();
  Reader reader(file, size - header_size);
  return HistoryCodec::Load(reader, processes, root);
}
