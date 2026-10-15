// clang-format off
#include "pch.h"
// clang-format on
#include "string_utils.h"

namespace string_utils {
void Split(const CString& text, wchar_t separator, CStringArray& components) {
  int begin = 0;
  const auto length = text.GetLength();
  for (int index = 0; index < length; ++index) {
    if (text[index] == separator) {
      components.Add(CString(text.GetString() + begin, index - begin));
      begin = index + 1;
    }
  }
  components.Add(CString(text.GetString() + begin, length - begin));
}

void EnsureTrailingSlash(CString& path) {
  if (!path.IsEmpty() && path[path.GetLength() - 1] != L'\\' && path[path.GetLength() - 1] != L'/')
    path += L'\\';
}

CString FormatBytes(std::uint64_t bytes) {
  static constexpr LPCWSTR units[] = {L"B", L"KiB", L"MiB", L"GiB", L"TiB", L"PiB", L"EiB"};
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= 1024 && unit + 1 < _countof(units)) {
    value /= 1024;
    ++unit;
  }
  CString result;
  result.Format(L"%.2f %s", value, units[unit]);
  return result;
}
}  // namespace string_utils
