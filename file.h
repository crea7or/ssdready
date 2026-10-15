#pragma once

#include <afxstr.h>

#include <cstddef>
#include <cstdint>

// Single-owner synchronous file. Callers serialize concurrent access.
class File {
 public:

  enum OpenFlags : DWORD {
    kModeRead = 0,
    kModeWrite = 1,
    kModeReadWrite = 2,
    kShareExclusive = 0x10,
    kShareDenyWrite = 0x20,
    kShareDenyRead = 0x30,
    kShareDenyNone = 0x40,
    kCreate = 0x1000,
    kNoTruncate = 0x2000,
  };
  enum class SeekOrigin : DWORD { kBegin = FILE_BEGIN, kCurrent = FILE_CURRENT, kEnd = FILE_END };
  enum class Error { kRead = 1, kWrite, kOpen, kClose, kSeek, kFlush, kGetSize };

  explicit File(bool throw_on_error = false, bool log_errors = true);
  virtual ~File() noexcept;
  File(const File&) = delete;
  File& operator=(const File&) = delete;

  // Handles are not inherited by child processes.
  virtual BOOL Open(LPCWSTR path, DWORD flags);
  virtual BOOL Read(void* target, std::size_t count);
  virtual BOOL Write(const void* source, std::size_t count);
  virtual BOOL Close();
  virtual BOOL Flush();
  virtual BOOL Seek(std::int64_t distance, SeekOrigin origin);
  virtual std::uint64_t GetPosition();
  virtual std::uint64_t GetLength();

  // Preserve version 1's 32-bit counts and byte-prefixed UTF-16 strings.
  BOOL WriteSize32(std::size_t size);
  BOOL ReadSize32(std::size_t& size);
  BOOL ReadString(CString& value, std::size_t max_characters = 0);
  BOOL WriteString(const CString& value, std::size_t max_characters = 0);
  static BOOL Exists(LPCWSTR path);

 protected:

  // Shared native boundaries allow short-I/O regression tests.
  virtual BOOL ReadBlock(void* target, DWORD count, DWORD& received);
  virtual BOOL WriteBlock(const void* source, DWORD count, DWORD& sent);
  BOOL Fail(Error operation, DWORD error);
  HANDLE handle_ = INVALID_HANDLE_VALUE;

 private:

  CString file_name_;
  bool throw_on_error_;
  bool log_errors_;
};
