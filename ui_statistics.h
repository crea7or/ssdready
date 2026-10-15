#pragma once

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
}  // namespace ui_statistics
