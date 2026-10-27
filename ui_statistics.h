#pragma once

#include <afxstr.h>

#include <cstdint>
#include <limits>

namespace ui_statistics {
inline std::uint64_t DailyBytes(std::uint64_t bytes, std::uint64_t seconds) {
  if (seconds == 0)
    return 0;
  constexpr std::uint64_t kSecondsPerDay = 86400;
  constexpr auto kMaximum = (std::numeric_limits<std::uint64_t>::max)();
  const auto whole = bytes / seconds;
  if (whole > kMaximum / kSecondsPerDay)
    return kMaximum;
  const auto fraction = static_cast<std::uint64_t>(static_cast<long double>(bytes % seconds) * kSecondsPerDay / seconds);
  const auto total = whole * kSecondsPerDay;
  return total > kMaximum - fraction ? kMaximum : total + fraction;
}

inline CString LifetimeText(std::uint64_t endurance_bytes, std::uint64_t rounded_bytes, std::uint64_t seconds) {
  const auto daily = DailyBytes(rounded_bytes, seconds);
  if (seconds <= 300 || daily == 0)
    return L"Calculating";
  const double days = static_cast<double>(endurance_bytes) / daily;
  CString result;
  if (days < 30)
    result.Format(L"%.0f days", days);
  else if (days < 365)
    result.Format(L"%.2f months", days / 30.4375);
  else
    result.Format(L"%.2f years", days / 365);
  return result;
}
}  // namespace ui_statistics
