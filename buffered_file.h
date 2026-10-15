#pragma once

#include <vector>

#include "file.h"

// Sequential persistence only. Read/write mixing and seeking are unsupported.
class BufferedFile : public File {
 public:

  explicit BufferedFile(bool throw_on_error = false, bool log_errors = true);
  ~BufferedFile() noexcept override;
  BufferedFile(const BufferedFile&) = delete;
  BufferedFile& operator=(const BufferedFile&) = delete;

  BOOL Open(LPCWSTR path, DWORD flags) override;
  BOOL Read(void* target, std::size_t count) override;
  BOOL Write(const void* source, std::size_t count) override;
  BOOL Close() override;
  BOOL Flush() override;

 private:

  BOOL Seek(std::int64_t, SeekOrigin) override;
  std::uint64_t GetPosition() override;
  BOOL Drain();
  BOOL BufferFailure(Error operation, DWORD error);
  std::vector<char> buffer_;
  std::size_t position_ = 0;
  std::size_t available_ = 0;
  bool writing_ = false;
  DWORD failure_ = ERROR_SUCCESS;
};
