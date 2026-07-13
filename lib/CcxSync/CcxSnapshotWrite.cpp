#include "CcxSnapshotWrite.h"

#include <ArduinoJson.h>

// Wire envelope (frozen phase3b contract):
// {"deviceId":"...","generatedAt":N,"books":[...],"progress":[...],"bookmarks":[...]}
//
// begin() opens the file and writes everything up through the "books" array
// opener. Each addX() call writes exactly one row through a small per-row
// JsonDocument into a stack char[512] (never a whole-snapshot doc), preceded
// by a comma if it isn't the first row in its section. enterSection() closes
// the previous array and opens the next one the first time a later section
// is touched -- callers are expected to add books, then progress, then
// bookmarks (matching the wire order), skipping a section entirely is fine.

bool CcxSnapshotWrite::writeRaw(const char* s, size_t len) {
  if (!ok_) return false;
  if (file_.write(s, len) != len) { ok_ = false; return false; }
  return true;
}

void CcxSnapshotWrite::enterSection(Section target) {
  static constexpr char TO_PROGRESS[] = "],\"progress\":[";
  static constexpr char TO_BOOKMARKS[] = "],\"bookmarks\":[";
  while (ok_ && section_ != target) {
    switch (section_) {
      case Section::BOOKS:
        writeRaw(TO_PROGRESS, sizeof(TO_PROGRESS) - 1);
        section_ = Section::PROGRESS;
        break;
      case Section::PROGRESS:
        writeRaw(TO_BOOKMARKS, sizeof(TO_BOOKMARKS) - 1);
        section_ = Section::BOOKMARKS;
        break;
      default:
        return;  // NONE (not begun) or already at/past BOOKMARKS
    }
    firstInSection_ = true;
  }
}

bool CcxSnapshotWrite::begin(const std::string& sdPath, const std::string& deviceId, long long generatedAt) {
  if (!Storage.openFileForWrite("CCXSNAP", sdPath, file_)) { ok_ = false; return false; }
  // deviceId is always 32 lowercase hex chars (CcxSyncState::newGuid) -- no
  // JSON string escaping needed for plain concatenation here.
  std::string head =
      "{\"deviceId\":\"" + deviceId + "\",\"generatedAt\":" + std::to_string(generatedAt) + ",\"books\":[";
  section_ = Section::BOOKS;
  firstInSection_ = true;
  ok_ = writeStr(head);
  return ok_;
}

namespace {
// Serializes one row doc into `buf` (char[512]); returns bytes written, or 0
// on overflow/error (leaving `ok` untouched lets the caller decide).
size_t serializeRow(JsonDocument& doc, char* buf, size_t bufSize) {
  size_t n = serializeJson(doc, buf, bufSize);
  if (n == 0 || n >= bufSize) return 0;  // overflow or empty -- never expected for these row shapes
  return n;
}
}  // namespace

void CcxSnapshotWrite::addBook(const ccxsync::MergedBook& b, long long addedAt) {
  enterSection(Section::BOOKS);
  if (!ok_) return;
  if (!firstInSection_) writeRaw(",", 1);
  firstInSection_ = false;

  JsonDocument doc;
  doc["digest"] = b.digest;
  doc["guid"] = b.guid;
  doc["title"] = b.title;
  doc["format"] = 1;  // epub-only on the wire; Accumulator already dropped non-epub rows
  doc["addedAt"] = addedAt;
  doc["lastOpenedAt"] = 0;
  doc["deleted"] = b.deleted;
  doc["updatedAt"] = addedAt;  // book rows are immutable after mint: updatedAt == addedAt

  char buf[512];
  size_t n = serializeRow(doc, buf, sizeof(buf));
  if (n == 0) { ok_ = false; return; }
  writeRaw(buf, n);
}

void CcxSnapshotWrite::addProgress(const ccxsync::MergedProgress& p) {
  enterSection(Section::PROGRESS);
  if (!ok_) return;
  if (!firstInSection_) writeRaw(",", 1);
  firstInSection_ = false;

  JsonDocument doc;
  doc["bookDigest"] = p.bookDigest;
  doc["spineIndex"] = p.spineIndex;
  doc["charOffset"] = 0;  // X4 tracks spineIndex/percentage only; wire slot kept for cross-device compat
  doc["percentage"] = p.percentage;
  doc["deleted"] = p.deleted;
  doc["updatedAt"] = p.updatedAt;

  char buf[512];
  size_t n = serializeRow(doc, buf, sizeof(buf));
  if (n == 0) { ok_ = false; return; }
  writeRaw(buf, n);
}

void CcxSnapshotWrite::addBookmark(const ccxsync::MergedBookmark& m) {
  enterSection(Section::BOOKMARKS);
  if (!ok_) return;
  if (!firstInSection_) writeRaw(",", 1);
  firstInSection_ = false;

  JsonDocument doc;
  doc["guid"] = m.guid;
  doc["bookDigest"] = m.bookDigest;
  doc["spineIndex"] = m.spineIndex;
  doc["charOffset"] = 0;
  doc["percentage"] = m.percentage;
  doc["label"] = m.label;
  doc["createdAt"] = m.createdAt;
  doc["deleted"] = m.deleted;
  doc["updatedAt"] = m.updatedAt;

  char buf[512];
  size_t n = serializeRow(doc, buf, sizeof(buf));
  if (n == 0) { ok_ = false; return; }
  writeRaw(buf, n);
}

bool CcxSnapshotWrite::finish() {
  enterSection(Section::BOOKMARKS);  // close out any un-entered sections (e.g. zero progress/bookmarks)
  writeRaw("]}", 2);
  file_.flush();
  bool closed = file_.close();
  return ok_ && closed;
}
