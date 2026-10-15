// clang-format off
#include "pch.h"
// clang-format on

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

#include "buffered_file.h"
#include "file_error.h"
#include "string_utils.h"

namespace {
CWinApp test_application;

void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

class MemoryFile : public File {
 public:

  MemoryFile() : File(false, false) {
  }

  std::vector<char> bytes;
  std::size_t position = 0;

  BOOL Read(void* output, std::size_t count) override {
    if (count > bytes.size() - position) {
      SetLastError(ERROR_HANDLE_EOF);
      return FALSE;
    }
    if (count)
      std::memcpy(output, bytes.data() + position, count);
    position += count;
    return TRUE;
  }

  BOOL Write(const void* source, std::size_t count) override {
    if (count) {
      const auto* begin = static_cast<const char*>(source);
      bytes.insert(bytes.end(), begin, begin + count);
    }
    return TRUE;
  }
};

void TestStrings() {
  MemoryFile file;
  const CString original(L"abcdef");
  Check(file.WriteString(original, 3), "Serialize truncated string");
  DWORD length;
  std::memcpy(&length, file.bytes.data(), sizeof(length));
  Check(length == 6 && file.bytes.size() == 10 && std::memcmp(file.bytes.data() + 4, L"abc", 6) == 0 && original == L"abcdef",
        "Truncated UTF-16 length and payload agree without mutating the source");
  Check(file.WriteString(CString(L"next")), "Serialize following string");
  CString value;
  Check(file.ReadString(value, 3) && value == L"abc" && file.ReadString(value) && value == L"next",
        "Following records retain their wire boundary after truncation");
  const wchar_t unicode[] = {L'\u03b1', L'\0', L'\u0436'};
  file.bytes.clear();
  file.position = 0;
  Check(file.WriteString(CString(unicode, 3)) && file.ReadString(value) && value.GetLength() == 3 && value[2] == unicode[2],
        "UTF-16 serialization preserves explicit lengths and embedded nulls");
  file.bytes.clear();
  file.position = 0;
  Check(file.WriteString(CString()) && file.ReadString(value) && value.IsEmpty(), "Empty strings round-trip");

  for (const DWORD bytes : {DWORD(3), DWORD(20), DWORD(0xfffffffe)}) {
    file.bytes.resize(sizeof(bytes));
    file.position = 0;
    std::memcpy(file.bytes.data(), &bytes, sizeof(bytes));
    value = L"unchanged";
    Check(!file.ReadString(value, 8) && value == L"unchanged" && GetLastError() == ERROR_INVALID_DATA,
          "Malformed or oversized UTF-16 strings leave the output unchanged");
  }
  for (std::size_t bytes : {std::size_t(0), std::size_t(2), std::size_t(5)}) {
    file.bytes.assign(bytes, 0);
    file.position = 0;
    if (bytes >= 4) {
      const DWORD payload_bytes = 4;
      std::memcpy(file.bytes.data(), &payload_bytes, sizeof(payload_bytes));
    }
    value = L"unchanged";
    Check(!file.ReadString(value) && value == L"unchanged", "Truncated prefixes and payloads retain the output");
  }
  file.bytes.clear();
  file.position = 0;
  std::size_t count = 19;
  Check(!file.ReadSize32(count) && count == 19, "Failed count reads retain the output");
  if constexpr (sizeof(std::size_t) > sizeof(DWORD)) {
    Check(!file.WriteSize32(std::size_t(MAXDWORD) + 1) && GetLastError() == ERROR_ARITHMETIC_OVERFLOW,
          "Version 1 count overflow is rejected");
  }
}

class TemporaryDirectory {
 public:

  TemporaryDirectory() {
    wchar_t current[32768];
    Check(GetCurrentDirectoryW(_countof(current), current) != 0, "Get test working directory");
    path_.Format(L"%s\\file-test-%lu-%llu", current, GetCurrentProcessId(), GetTickCount64());
    Check(CreateDirectoryW(path_, nullptr), "Create isolated file test directory");
  }

  ~TemporaryDirectory() {
    DeleteFileW(file());
    RemoveDirectoryW(path_);
  }

  CString file() const {
    return path_ + L"\\data.bin";
  }

 private:

  CString path_;
};

template <class Base>
class ShortFile : public Base {
 public:

  ShortFile() : Base(false, false) {
  }

  HANDLE handle() const {
    return this->handle_;
  }

 protected:

  BOOL ReadBlock(void* target, DWORD count, DWORD& received) override {
    return Base::ReadBlock(target, (std::min)(count, DWORD(997)), received);
  }

  BOOL WriteBlock(const void* source, DWORD count, DWORD& sent) override {
    return Base::WriteBlock(source, (std::min)(count, DWORD(991)), sent);
  }
};

class FailingFile : public BufferedFile {
 public:

  explicit FailingFile(bool throwing = false) : BufferedFile(throwing, false) {
  }

  unsigned calls = 0;
  bool zero_write = false;

  bool is_open() const {
    return handle_ != INVALID_HANDLE_VALUE;
  }

 protected:

  BOOL WriteBlock(const void* source, DWORD count, DWORD& sent) override {
    ++calls;
    if (zero_write) {
      sent = 0;
      return TRUE;
    }
    if (calls == 1)
      return BufferedFile::WriteBlock(source, (std::min)(count, DWORD(3)), sent);
    SetLastError(ERROR_DISK_FULL);
    return FALSE;
  }
};

void TestNativeFiles() {
  static_assert(!std::is_copy_constructible_v<File>);
  static_assert(std::is_nothrow_destructible_v<File>);
  static_assert(std::is_nothrow_destructible_v<BufferedFile>);
  TemporaryDirectory directory;
  std::vector<char> source(1024 * 1024 + 17);
  for (std::size_t index = 0; index < source.size(); ++index)
    source[index] = static_cast<char>(index * 17);
  auto round_trip = [&](auto& file) {
    Check(file.Open(directory.file(), File::kModeWrite | File::kCreate), "Open output file");
    DWORD flags = HANDLE_FLAG_INHERIT;
    Check(GetHandleInformation(file.handle(), &flags) && !(flags & HANDLE_FLAG_INHERIT), "Handles are not inherited by child processes");
    Check(file.Write(source.data(), source.size()) && file.Close(), "Handle short writes and buffer boundaries");
    Check(file.Open(directory.file(), File::kModeRead) && file.GetLength() == source.size(), "Open complete output for reading");
    std::vector<char> loaded(source.size());
    Check(file.Read(loaded.data(), loaded.size()) && loaded == source, "Handle short reads without losing bytes");
    char extra;
    Check(!file.Read(&extra, 1) && GetLastError() == ERROR_HANDLE_EOF, "EOF reports a deterministic error code");
    file.Close();
  };
  ShortFile<File> direct;
  round_trip(direct);
  ShortFile<BufferedFile> buffered;
  round_trip(buffered);

  BufferedFile flush(false, false);
  File* base = &flush;
  Check(base->Open(directory.file(), File::kModeWrite | File::kCreate) && base->Write(source.data(), 10), "Prepare virtual flush");
  Check(!base->Seek(0, File::SeekOrigin::kBegin) && GetLastError() == ERROR_NOT_SUPPORTED,
        "Base pointers cannot bypass buffered seeking rules");
  Check(base->Flush() && base->GetLength() == 10 && base->Close(), "Virtual flush drains buffered data after rejected seek");

  for (bool throwing : {false, true}) {
    FailingFile failed(throwing);
    Check(failed.Open(directory.file(), File::kModeWrite | File::kCreate) && failed.Write(source.data(), 10), "Prepare disk-full failure");
    bool rejected = false;
    try {
      rejected = !failed.Close();
    } catch (File::Error error) {
      rejected = throwing && error == File::Error::kWrite;
    }
    Check(rejected && !failed.is_open() && failed.calls == 2 && GetLastError() == ERROR_DISK_FULL,
          "Final-flush failure closes the handle and preserves the error even when throwing");
    Check(!failed.Close() && failed.calls == 2 && GetLastError() == ERROR_DISK_FULL, "Failed buffers are never replayed on repeated close");
  }
  FailingFile zero;
  zero.zero_write = true;
  Check(zero.Open(directory.file(), File::kModeWrite | File::kCreate) && zero.Write(source.data(), 10), "Prepare zero-progress write");
  Check(!zero.Close() && zero.calls == 1 && GetLastError() == ERROR_WRITE_FAULT, "Zero-progress writes cannot spin indefinitely");

  ShortFile<File> large;
  Check(large.Open(directory.file(), File::kModeReadWrite | File::kCreate), "Open seek test");
  Check(large.Seek(0xffffffffLL, File::SeekOrigin::kBegin) && large.GetPosition() == 0xffffffffULL,
        "The old INVALID_SET_FILE_POINTER value is a valid file position");
  Check(large.Seek(2, File::SeekOrigin::kCurrent) && large.GetPosition() == 0x100000001ULL && large.Seek(-2, File::SeekOrigin::kCurrent) &&
            large.GetPosition() == 0xffffffffULL,
        "64-bit signed relative seeks preserve positions across 4 GiB");
  DWORD returned;
  if (DeviceIoControl(large.handle(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) {
    Check(large.Write(source.data(), 1) && large.GetLength() == 0x100000000ULL, "File sizes above 4 GiB are reported correctly");
    char last = 0;
    Check(large.Seek(-1, File::SeekOrigin::kEnd) && large.Read(&last, 1) && last == source[0], "Backward seeks from EOF preserve data");
  }
  Check(!large.Seek(-1, File::SeekOrigin::kBegin) && GetLastError() == ERROR_INVALID_PARAMETER, "Negative absolute positions are rejected");
  Check(large.Close(), "Close large sparse file");
  File invalid(false, false);
  Check(!invalid.Open(nullptr, File::kModeRead) && GetLastError() == ERROR_INVALID_PARAMETER, "Invalid open arguments are rejected");
  Check(!invalid.Read(nullptr, 1) && !invalid.Write(nullptr, 1), "Invalid data buffers are rejected");
}

void TestErrorsAndDestruction() {
  CStringArray baseline;
  AppLog::Get().CopyLog(baseline);
  SetLastError(ERROR_ACCESS_DENIED);
  {
    FileError error(L"sample.bin");
    Check(GetLastError() == ERROR_ACCESS_DENIED, "Error formatting preserves the captured Windows error");
    SetLastError(ERROR_DISK_FULL);
  }
  Check(GetLastError() == ERROR_ACCESS_DENIED, "Scoped errors restore the original code");
  CStringArray appended;
  AppLog::Get().CopyLog(appended, baseline.GetSize());
  Check(appended.GetSize() == 1 && appended[0].Find(L"sample.bin") >= 0, "File errors log exactly one message including the filename");
  { SystemError unknown(L"test", 0xe1234567); }
  AppLog::Get().CopyLog(baseline);
  {
    File quiet(false, false);
    Check(!quiet.Open(nullptr, File::kModeRead), "Prepare suppressed error");
  }
  AppLog::Get().CopyLog(appended, baseline.GetSize());
  Check(appended.IsEmpty(), "Disabling error logging is respected");

  TemporaryDirectory directory;
  HANDLE handle;
  {
    ShortFile<File> owner;
    Check(owner.Open(directory.file(), File::kModeWrite | File::kCreate), "Prepare owned handle");
    handle = owner.handle();
    SetLastError(12345);
  }
  Check(GetLastError() == 12345, "Destruction preserves the caller's error state");
  DWORD flags;
  Check(!GetHandleInformation(handle, &flags) && GetLastError() == ERROR_INVALID_HANDLE, "Destruction closes the owned handle");
}

void TestHelpers() {
  CStringArray parts;
  parts.Add(L"existing");
  const CString input(L"\\a\\\\b\\");
  string_utils::Split(input, L'\\', parts);
  Check(parts.GetSize() == 6 && parts[1].IsEmpty() && parts[2] == L"a" && parts[3].IsEmpty() && parts[4] == L"b" && parts[5].IsEmpty(),
        "Splitting preserves leading, repeated, trailing, and preexisting components");
  Check(input == L"\\a\\\\b\\", "Splitting does not modify the input");
  CString path(L"x");
  string_utils::EnsureTrailingSlash(path);
  Check(path == L"x\\", "One-character folders get a trailing slash");
  string_utils::EnsureTrailingSlash(path);
  Check(path == L"x\\", "Trailing slashes are not duplicated");
  path = L"C:/";
  string_utils::EnsureTrailingSlash(path);
  Check(path == L"C:/", "Forward-slash paths retain their separator");
  path.Empty();
  string_utils::EnsureTrailingSlash(path);
  Check(path.IsEmpty(), "Empty paths stay empty");
  Check(string_utils::FormatBytes(1023).Find(L" B") >= 0 && string_utils::FormatBytes(1024).Find(L" KiB") >= 0 &&
            string_utils::FormatBytes(1024 * 1024).Find(L" MiB") >= 0 && string_utils::FormatBytes(UINT64_MAX).Find(L" EiB") >= 0,
        "Byte formatting uses correct binary units at exact boundaries and across the uint64 range");
}
}  // namespace

int main() {
  try {
    if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0))
      return 1;
    TestStrings();
    TestNativeFiles();
    TestErrorsAndDestruction();
    TestHelpers();
    std::cout << "UTF-16 boundaries, short I/O, buffered failure handling, large seeks, scoped errors, and helpers passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
