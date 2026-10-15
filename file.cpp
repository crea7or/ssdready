// clang-format off
#include "pch.h"
// clang-format on
#include "file.h"

#include <algorithm>
#include <utility>

#include "file_error.h"

File::File(bool throw_on_error, bool log_errors) : throw_on_error_(throw_on_error), log_errors_(log_errors) {
}

File::~File() noexcept {
  // Never dispatch virtual Close or throw/log during base destruction.
  const DWORD error = GetLastError();
  if (handle_ != INVALID_HANDLE_VALUE)
    CloseHandle(handle_);
  SetLastError(error);
}

BOOL File::Fail(Error operation, DWORD error) {
  if (log_errors_) {
    // Allocation or logger failure must not replace the original I/O failure.
    try {
      FileError report(file_name_, error);
    } catch (CException* exception) {
      exception->Delete();
    } catch (...) {}
  }
  SetLastError(error);
  if (throw_on_error_)
    throw operation;
  return FALSE;
}

BOOL File::Open(LPCWSTR path, DWORD flags) {
  constexpr DWORD allowed = 3 | 0x70 | kCreate | kNoTruncate;
  const DWORD mode = flags & 3;
  const DWORD share_mode = flags & 0x70;
  if (!path || (flags & ~allowed) || mode > kModeReadWrite || share_mode > kShareDenyNone)
    return Fail(Error::kOpen, ERROR_INVALID_PARAMETER);
  if (handle_ != INVALID_HANDLE_VALUE && !Close())
    return FALSE;
  file_name_ = path;
  const DWORD access = mode == kModeRead ? GENERIC_READ : mode == kModeWrite ? GENERIC_WRITE : GENERIC_READ | GENERIC_WRITE;
  DWORD sharing = 0;
  if (share_mode == kShareDenyWrite || share_mode == kShareDenyNone)
    sharing |= FILE_SHARE_READ;
  if (share_mode == kShareDenyRead || share_mode == kShareDenyNone)
    sharing |= FILE_SHARE_WRITE;
  const DWORD creation = (flags & kCreate) ? ((flags & kNoTruncate) ? OPEN_ALWAYS : CREATE_ALWAYS) : OPEN_EXISTING;
  handle_ = CreateFileW(path, access, sharing, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
  return handle_ != INVALID_HANDLE_VALUE ? TRUE : Fail(Error::kOpen, GetLastError());
}

BOOL File::ReadBlock(void* target, DWORD count, DWORD& received) {
  return ReadFile(handle_, target, count, &received, nullptr);
}

BOOL File::WriteBlock(const void* source, DWORD count, DWORD& sent) {
  return WriteFile(handle_, source, count, &sent, nullptr);
}

BOOL File::Read(void* target, std::size_t count) {
  if (count && !target)
    return Fail(Error::kRead, ERROR_INVALID_PARAMETER);
  auto* output = static_cast<char*>(target);
  while (count) {
    const DWORD requested = static_cast<DWORD>((std::min)(count, std::size_t(MAXDWORD)));
    DWORD received = 0;
    if (!ReadBlock(output, requested, received))
      return Fail(Error::kRead, GetLastError());
    if (received == 0)
      return Fail(Error::kRead, ERROR_HANDLE_EOF);
    if (received > requested)
      return Fail(Error::kRead, ERROR_INVALID_DATA);
    output += received;
    count -= received;
  }
  return TRUE;
}

BOOL File::Write(const void* source, std::size_t count) {
  if (count && !source)
    return Fail(Error::kWrite, ERROR_INVALID_PARAMETER);
  const auto* input = static_cast<const char*>(source);
  while (count) {
    const DWORD requested = static_cast<DWORD>((std::min)(count, std::size_t(MAXDWORD)));
    DWORD sent = 0;
    if (!WriteBlock(input, requested, sent))
      return Fail(Error::kWrite, GetLastError());
    if (sent == 0)
      return Fail(Error::kWrite, ERROR_WRITE_FAULT);
    if (sent > requested)
      return Fail(Error::kWrite, ERROR_INVALID_DATA);
    input += sent;
    count -= sent;
  }
  return TRUE;
}

BOOL File::ReadString(CString& value, std::size_t max_characters) {
  DWORD bytes = 0;
  if (!Read(&bytes, sizeof(bytes)))
    return FALSE;
  const auto characters = bytes / sizeof(wchar_t);
  if (bytes % sizeof(wchar_t) || characters > INT_MAX || (max_characters && characters > max_characters))
    return Fail(Error::kRead, ERROR_INVALID_DATA);
  CString loaded;
  if (bytes) {
    auto* buffer = loaded.GetBufferSetLength(static_cast<int>(characters));
    BOOL read;
    try {
      read = Read(buffer, bytes);
    } catch (...) {
      loaded.ReleaseBuffer(static_cast<int>(characters));
      throw;
    }
    loaded.ReleaseBuffer(static_cast<int>(characters));
    if (!read)
      return FALSE;
  }
  value = std::move(loaded);
  return TRUE;
}

BOOL File::WriteString(const CString& value, std::size_t max_characters) {
  std::size_t characters = static_cast<std::size_t>(value.GetLength());
  if (max_characters)
    characters = (std::min)(characters, max_characters);
  const DWORD bytes = static_cast<DWORD>(characters * sizeof(wchar_t));
  return Write(&bytes, sizeof(bytes)) && Write(value.GetString(), bytes);
}

BOOL File::WriteSize32(std::size_t size) {
  if (size > MAXDWORD)
    return Fail(Error::kWrite, ERROR_ARITHMETIC_OVERFLOW);
  const DWORD encoded = static_cast<DWORD>(size);
  return Write(&encoded, sizeof(encoded));
}

BOOL File::ReadSize32(std::size_t& size) {
  DWORD encoded = 0;
  if (!Read(&encoded, sizeof(encoded)))
    return FALSE;
  size = encoded;
  return TRUE;
}

BOOL File::Seek(std::int64_t distance, SeekOrigin origin) {
  if (static_cast<DWORD>(origin) > FILE_END || (origin == SeekOrigin::kBegin && distance < 0))
    return Fail(Error::kSeek, ERROR_INVALID_PARAMETER);
  LARGE_INTEGER offset;
  offset.QuadPart = distance;
  return SetFilePointerEx(handle_, offset, nullptr, static_cast<DWORD>(origin)) ? TRUE : Fail(Error::kSeek, GetLastError());
}

std::uint64_t File::GetPosition() {
  LARGE_INTEGER zero{}, position;
  if (!SetFilePointerEx(handle_, zero, &position, FILE_CURRENT)) {
    Fail(Error::kSeek, GetLastError());
    return 0;
  }
  return static_cast<std::uint64_t>(position.QuadPart);
}

std::uint64_t File::GetLength() {
  LARGE_INTEGER size;
  if (!GetFileSizeEx(handle_, &size)) {
    Fail(Error::kGetSize, GetLastError());
    return 0;
  }
  return static_cast<std::uint64_t>(size.QuadPart);
}

BOOL File::Flush() {
  return FlushFileBuffers(handle_) ? TRUE : Fail(Error::kFlush, GetLastError());
}

BOOL File::Close() {
  if (handle_ == INVALID_HANDLE_VALUE)
    return TRUE;
  const HANDLE handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
  return CloseHandle(handle) ? TRUE : Fail(Error::kClose, GetLastError());
}

BOOL File::Exists(LPCWSTR path) {
  return path && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
