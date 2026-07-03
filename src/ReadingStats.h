#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Reading-time statistics: session tracking + per-day / per-book totals,
// persisted to /.crosspoint/stats.json. Durations use millis() (monotonic —
// an NTP jump mid-session must not warp a session); calendar bucketing uses
// the wall clock and is skipped entirely while the clock is unset.
class ReadingStats {
 private:
  ReadingStats() = default;
  static ReadingStats instance;

  static constexpr uint32_t MIN_SESSION_SECONDS = 15;
  static constexpr uint32_t IDLE_LIMIT_SECONDS = 10 * 60;
  static constexpr uint32_t IDLE_TAIL_SECONDS = 60;
  static constexpr size_t MAX_DAYS = 70;
  static constexpr size_t MAX_BOOKS = 50;

  // Persisted aggregates (small, capped).
  uint32_t allTime = 0;
  uint32_t pages = 0;
  std::vector<std::pair<std::string, uint32_t>> days;   // "YYYY-MM-DD" -> seconds, newest kept
  std::vector<std::pair<std::string, uint32_t>> books;  // path-hash key -> seconds

  // Live session (not persisted).
  bool sessionActive = false;
  bool firstShow = true;
  std::string sessionBookKey;
  unsigned long sessionStartMs = 0;
  unsigned long lastActivityMs = 0;
  uint32_t sessionPages = 0;
  bool dirty = false;

  void addSeconds(const std::string& bookKey, uint32_t seconds);
  static bool todayString(std::string& out, int daysBack = 0);

 public:
  ReadingStats(const ReadingStats&) = delete;
  ReadingStats& operator=(const ReadingStats&) = delete;
  static ReadingStats& getInstance() { return instance; }

  static std::string bookKeyFor(const std::string& path);

  void onReaderEnter(const std::string& bookPath);
  void onPageTurn();
  void onReaderExit();  // finalize the session and persist

  uint32_t todaySeconds() const;
  uint32_t weekSeconds() const;  // last 7 calendar days including today
  uint32_t allTimeSeconds() const { return allTime; }
  uint32_t allTimePagesTurned() const { return pages; }
  // Consecutive days with >= 60s read, counting back from today (from
  // yesterday when today has nothing yet).
  uint32_t currentStreakDays() const;
  uint32_t bookSeconds(const std::string& bookPath) const;
  void lastSevenDaysSeconds(uint32_t out[7]) const;  // out[0] = 6 days ago .. out[6] = today
  bool clockValid() const;

  void loadFromFile();
  void saveToFile();  // no-op unless dirty
};

#define READING_STATS ReadingStats::getInstance()
