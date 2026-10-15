#pragma once

#include "system_error.h"

class FileError : public SystemError {
 public:

  explicit FileError(LPCWSTR file_name = nullptr, DWORD error = GetLastError());

 private:

  static CString Context(LPCWSTR file_name);
};
