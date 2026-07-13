#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>

#include "CcxMerge.h"

// Streams the X4's snapshot to an SD file, one row at a time. Never holds a
// whole-snapshot doc in memory: each row is serialized through a small
// stack-buffer ArduinoJson doc and written straight to the HalFile. Dumb by
// design -- tombstones are the engine's job (Task 6); this class just emits
// whatever rows it's handed, in order, wrapped in the wire envelope.
class CcxSnapshotWrite {
 public:
  bool begin(const std::string& sdPath, const std::string& deviceId, long long generatedAt);
  void addBook(const ccxsync::MergedBook& b, long long addedAt);
  void addProgress(const ccxsync::MergedProgress& p);
  void addBookmark(const ccxsync::MergedBookmark& m);
  bool finish();  // closes arrays/object + file; false on any write error

 private:
  enum class Section : uint8_t { NONE, BOOKS, PROGRESS, BOOKMARKS };

  bool writeRaw(const char* s, size_t len);
  bool writeStr(const std::string& s) { return writeRaw(s.data(), s.size()); }
  void enterSection(Section s);

  HalFile file_;
  Section section_ = Section::NONE;
  bool firstInSection_ = true;
  bool ok_ = false;
};
