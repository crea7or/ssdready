// clang-format off
#include "pch.h"
// clang-format on
#include "file_error.h"

CString FileError::Context(LPCWSTR file_name) {
  CString context;
  context.Format(L"Operation with file '%s' failed", file_name && *file_name ? file_name : L"Unnamed");
  return context;
}

FileError::FileError(LPCWSTR file_name, DWORD error) : SystemError(Context(file_name), error) {
}
