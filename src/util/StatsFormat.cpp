#include "StatsFormat.h"

#include <I18n.h>

#include <cstdio>

#include "ReadingStats.h"

std::string StatsFormat::duration(uint32_t seconds) {
  char buf[16];
  const uint32_t h = seconds / 3600;
  const uint32_t m = (seconds % 3600) / 60;
  if (h > 0) {
    snprintf(buf, sizeof(buf), "%uH %02uM", h, m);
  } else {
    snprintf(buf, sizeof(buf), "%uM", m);
  }
  return buf;
}

std::string StatsFormat::summaryLine() {
  if (READING_STATS.allTimeSeconds() == 0) {
    return "";
  }
  if (!READING_STATS.clockValid()) {
    return std::string(tr(STR_STATS_ALL_TIME)) + " " + duration(READING_STATS.allTimeSeconds());
  }
  const uint32_t today = READING_STATS.todaySeconds();
  const uint32_t streak = READING_STATS.currentStreakDays();
  if (today == 0 && streak == 0) {
    return std::string(tr(STR_STATS_ALL_TIME)) + " " + duration(READING_STATS.allTimeSeconds());
  }
  std::string line = std::string(tr(STR_STATS_TODAY)) + " " + duration(today);
  if (streak > 0) {
    char streakBuf[16];
    snprintf(streakBuf, sizeof(streakBuf), " | %s %uD", tr(STR_STATS_STREAK), streak);
    line += streakBuf;
  }
  return line;
}
