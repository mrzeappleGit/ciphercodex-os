#include "ReadingStats.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <ctime>
#include <functional>

namespace {
constexpr const char* STATS_FILE = "/.crosspoint/stats.json";
}

ReadingStats ReadingStats::instance;

std::string ReadingStats::bookKeyFor(const std::string& path) {
  // Same key the /.crosspoint/epub_<hash> cache dirs use, so a book's stats
  // and cache share an identity.
  return std::to_string(std::hash<std::string>{}(path));
}

bool ReadingStats::todayString(std::string& out, int daysBack) {
  time_t now = time(nullptr);
  now -= static_cast<time_t>(daysBack) * 86400;
  struct tm local{};
  localtime_r(&now, &local);
  if (local.tm_year + 1900 < 2024) {
    return false;
  }
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  out = buf;
  return true;
}

bool ReadingStats::clockValid() const {
  std::string unused;
  return todayString(unused);
}

void ReadingStats::onReaderEnter(const std::string& bookPath) {
  sessionActive = true;
  firstShow = true;
  sessionBookKey = bookKeyFor(bookPath);
  sessionStartMs = millis();
  lastActivityMs = sessionStartMs;
  sessionPages = 0;
}

void ReadingStats::onPageTurn() {
  if (!sessionActive) {
    return;
  }
  // The first render after entering is the restored position, not a turn.
  if (firstShow) {
    firstShow = false;
    lastActivityMs = millis();
    return;
  }
  sessionPages++;
  lastActivityMs = millis();
}

void ReadingStats::onReaderExit() {
  if (!sessionActive) {
    return;
  }
  sessionActive = false;

  const unsigned long now = millis();
  uint32_t seconds = (now - sessionStartMs) / 1000;
  const uint32_t sinceActivity = (now - lastActivityMs) / 1000;
  // Screen-on idling is not reading: trim ends far past the last page turn.
  if (sinceActivity > IDLE_LIMIT_SECONDS) {
    seconds = (lastActivityMs - sessionStartMs) / 1000 + IDLE_TAIL_SECONDS;
  }
  if (seconds < MIN_SESSION_SECONDS && sessionPages == 0) {
    return;
  }

  allTime += seconds;
  pages += sessionPages;
  addSeconds(sessionBookKey, seconds);

  std::string today;
  if (todayString(today)) {
    // Sessions crossing midnight land whole on the exit day — they are capped
    // by the idle guard, so the error is bounded and not worth splitting.
    for (auto& d : days) {
      if (d.first == today) {
        d.second += seconds;
        dirty = true;
        saveToFile();
        return;
      }
    }
    days.emplace_back(today, seconds);
    if (days.size() > MAX_DAYS) {
      // Entries are appended chronologically; drop from the front.
      days.erase(days.begin(), days.begin() + (days.size() - MAX_DAYS));
    }
  }
  dirty = true;
  saveToFile();
}

void ReadingStats::addSeconds(const std::string& bookKey, uint32_t seconds) {
  for (auto& b : books) {
    if (b.first == bookKey) {
      b.second += seconds;
      return;
    }
  }
  books.emplace_back(bookKey, seconds);
  if (books.size() > MAX_BOOKS) {
    // Evict the smallest total, not the oldest — keep the books that matter.
    auto smallest = std::min_element(books.begin(), books.end(),
                                     [](const auto& a, const auto& b) { return a.second < b.second; });
    books.erase(smallest);
  }
}

uint32_t ReadingStats::todaySeconds() const {
  std::string today;
  if (!todayString(today)) {
    return 0;
  }
  for (const auto& d : days) {
    if (d.first == today) {
      return d.second;
    }
  }
  return 0;
}

uint32_t ReadingStats::weekSeconds() const {
  uint32_t out[7];
  lastSevenDaysSeconds(out);
  uint32_t total = 0;
  for (uint32_t v : out) {
    total += v;
  }
  return total;
}

void ReadingStats::lastSevenDaysSeconds(uint32_t out[7]) const {
  for (int i = 0; i < 7; i++) {
    out[i] = 0;
    std::string date;
    if (!todayString(date, 6 - i)) {
      continue;
    }
    for (const auto& d : days) {
      if (d.first == date) {
        out[i] = d.second;
        break;
      }
    }
  }
}

uint32_t ReadingStats::currentStreakDays() const {
  auto secondsOn = [this](int daysBack) -> uint32_t {
    std::string date;
    if (!todayString(date, daysBack)) {
      return 0;
    }
    for (const auto& d : days) {
      if (d.first == date) {
        return d.second;
      }
    }
    return 0;
  };
  if (!clockValid()) {
    return 0;
  }
  uint32_t streak = 0;
  int back = secondsOn(0) >= 60 ? 0 : 1;
  while (secondsOn(back) >= 60) {
    streak++;
    back++;
    if (static_cast<size_t>(back) > MAX_DAYS) {
      break;
    }
  }
  return streak;
}

uint32_t ReadingStats::bookSeconds(const std::string& bookPath) const {
  const std::string key = bookKeyFor(bookPath);
  for (const auto& b : books) {
    if (b.first == key) {
      return b.second;
    }
  }
  return 0;
}

void ReadingStats::loadFromFile() {
  if (!Storage.exists(STATS_FILE)) {
    return;
  }
  String json = Storage.readFile(STATS_FILE);
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("STAT", "JSON parse error: %s", error.c_str());
    return;
  }
  allTime = doc["allTimeSeconds"] | 0U;
  pages = doc["pagesTurned"] | 0U;
  days.clear();
  for (JsonPair kv : doc["days"].as<JsonObject>()) {
    if (days.size() >= MAX_DAYS) break;
    days.emplace_back(kv.key().c_str(), kv.value().as<uint32_t>());
  }
  std::sort(days.begin(), days.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  books.clear();
  for (JsonPair kv : doc["books"].as<JsonObject>()) {
    if (books.size() >= MAX_BOOKS) break;
    books.emplace_back(kv.key().c_str(), kv.value().as<uint32_t>());
  }
  LOG_DBG("STAT", "Loaded stats: %u s all-time, %zu days, %zu books", allTime, days.size(), books.size());
}

void ReadingStats::saveToFile() {
  if (!dirty) {
    return;
  }
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  doc["allTimeSeconds"] = allTime;
  doc["pagesTurned"] = pages;
  JsonObject dayObj = doc["days"].to<JsonObject>();
  for (const auto& d : days) {
    dayObj[d.first] = d.second;
  }
  JsonObject bookObj = doc["books"].to<JsonObject>();
  for (const auto& b : books) {
    bookObj[b.first] = b.second;
  }
  String json;
  serializeJson(doc, json);
  if (Storage.writeFile(STATS_FILE, json)) {
    dirty = false;
  } else {
    LOG_ERR("STAT", "Failed to write %s", STATS_FILE);
  }
}
