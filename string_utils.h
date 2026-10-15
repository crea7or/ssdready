#pragma once

#include <afxcoll.h>

#include <cstdint>

namespace string_utils {
// Append all components, preserving empty components and the input string.
void Split(const CString& text, wchar_t separator, CStringArray& components);
void EnsureTrailingSlash(CString& path);
CString FormatBytes(std::uint64_t bytes);
}  // namespace string_utils
