#pragma once
#include <cstdint>
#include <string>

namespace StatsFormat {
// "1H 02M" / "42M" — the duration convention used across stats UI.
std::string duration(uint32_t seconds);

// One-line summary for glanceable chrome (home header, sleep screen):
// clock set:   "Today 42M | Streak 3D"
// clock unset: "All Time 12H 05M"
// nothing read yet: "" — callers skip drawing.
std::string summaryLine();
}  // namespace StatsFormat
