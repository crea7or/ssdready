// clang-format off
#include "pch.h"
// clang-format on
#include "buffered_file.h"

#include <algorithm>
#include <cstring>

BufferedFile::BufferedFile(bool throw_on_error, bool log_errors) : File(throw_on_error, log_errors), buffer_(1024 * 1024) {
}

BufferedFile::~BufferedFile() noexcept {
  // Persistence callers explicitly Close to check the final buffer flush.
  const DWORD error = GetLastError();
  try {
    Close();
  } catch (CException* exception) {
    exception->Delete();
  } catch (...) {}
  SetLastError(error);
}

BOOL BufferedFile::BufferFailure(Error operation, DWORD error) {
  failure_ = error ? error : ERROR_GEN_FAILURE;
  return File::Fail(operation, failure_);
}

BOOL BufferedFile::Open(LPCWSTR path, DWORD flags) {
  if (handle_ != INVALID_HANDLE_VALUE && !Close())
    return FALSE;
  position_ = available_ = 0;
  failure_ = ERROR_SUCCESS;
  const DWORD mode = flags & 3;
  if (mode != kModeRead && mode != kModeWrite)
    return File::Fail(Error::kOpen, ERROR_INVALID_PARAMETER);
  writing_ = mode == kModeWrite;
  return File::Open(path, flags);
}

BOOL BufferedFile::Read(void* target, std::size_t count) {
  if (failure_) {
    SetLastError(failure_);
    return FALSE;
  }
  if (!count)
    return TRUE;
  if (!target)
    return BufferFailure(Error::kRead, ERROR_INVALID_PARAMETER);
  if (writing_ || handle_ == INVALID_HANDLE_VALUE)
    return BufferFailure(Error::kRead, ERROR_INVALID_HANDLE);
  auto* output = static_cast<char*>(target);
  while (count) {
    if (position_ == available_) {
      DWORD received = 0;
      if (!ReadBlock(buffer_.data(), static_cast<DWORD>(buffer_.size()), received))
        return BufferFailure(Error::kRead, GetLastError());
      if (!received)
        return BufferFailure(Error::kRead, ERROR_HANDLE_EOF);
      if (received > buffer_.size())
        return BufferFailure(Error::kRead, ERROR_INVALID_DATA);
      position_ = 0;
      available_ = received;
    }
    const auto bytes = (std::min)(count, available_ - position_);
    std::memcpy(output, buffer_.data() + position_, bytes);
    output += bytes;
    position_ += bytes;
    count -= bytes;
  }
  return TRUE;
}

BOOL BufferedFile::Drain() {
  if (failure_) {
    SetLastError(failure_);
    return FALSE;
  }
  std::size_t written = 0;
  while (written < position_) {
    const auto remaining = static_cast<DWORD>(position_ - written);
    DWORD sent = 0;
    if (!WriteBlock(buffer_.data() + written, remaining, sent))
      return BufferFailure(Error::kWrite, GetLastError());
    if (!sent)
      return BufferFailure(Error::kWrite, ERROR_WRITE_FAULT);
    if (sent > remaining)
      return BufferFailure(Error::kWrite, ERROR_INVALID_DATA);
    written += sent;
  }
  position_ = 0;
  return TRUE;
}

BOOL BufferedFile::Write(const void* source, std::size_t count) {
  if (failure_) {
    SetLastError(failure_);
    return FALSE;
  }
  if (!count)
    return TRUE;
  if (!source)
    return BufferFailure(Error::kWrite, ERROR_INVALID_PARAMETER);
  if (!writing_ || handle_ == INVALID_HANDLE_VALUE)
    return BufferFailure(Error::kWrite, ERROR_INVALID_HANDLE);
  const auto* input = static_cast<const char*>(source);
  while (count) {
    const auto bytes = (std::min)(count, buffer_.size() - position_);
    std::memcpy(buffer_.data() + position_, input, bytes);
    position_ += bytes;
    input += bytes;
    count -= bytes;
    if (position_ == buffer_.size() && !Drain())
      return FALSE;
  }
  return TRUE;
}

BOOL BufferedFile::Flush() {
  if (failure_) {
    SetLastError(failure_);
    return FALSE;
  }
  if (writing_ && !Drain())
    return FALSE;
  return File::Flush();
}

BOOL BufferedFile::Close() {
  BOOL ok = failure_ == ERROR_SUCCESS;
  try {
    if (handle_ != INVALID_HANDLE_VALUE && writing_ && !Drain())
      ok = FALSE;
  } catch (...) {
    File::Close();
    throw;
  }
  if (!File::Close())
    ok = FALSE;
  position_ = available_ = 0;
  if (failure_)
    SetLastError(failure_);
  return ok;
}

BOOL BufferedFile::Seek(std::int64_t, SeekOrigin) {
  // Reject random access even through File*, without abandoning buffered data.
  return File::Fail(Error::kSeek, ERROR_NOT_SUPPORTED);
}

std::uint64_t BufferedFile::GetPosition() {
  File::Fail(Error::kSeek, ERROR_NOT_SUPPORTED);
  return 0;
}
