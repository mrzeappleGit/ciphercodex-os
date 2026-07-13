#pragma once

#include <cstring>
#include <string>

#include "CcxMerge.h"
#include "JsonParser/StreamingJsonParser.h"

// SAX fold of a device snapshot into the merge accumulator. Tracks which
// top-level array it is inside (books/progress/bookmarks — everything else,
// including rM2 ink arrays, is skipped structurally). Depth-aware so nested
// objects in future/unknown sections can't confuse section tracking.
//
// depth_ convention verified against StreamingJsonParserTest.cpp: the root
// object's OBJECT_START fires before its first KEY event (see SimpleObject),
// so depth_ is already 1 by the time top-level keys are seen. That matches
// this file's thresholds as-is: 2 for section-array ARRAY_START, 3 for a
// row's OBJECT_START/OBJECT_END. No adjustment needed.
class CcxSnapshotParse {
 public:
  explicit CcxSnapshotParse(ccxsync::Accumulator& acc) : acc_(acc), parser_(makeCallbacks()) {}

  void feed(const char* data, size_t len) { parser_.feed(data, len); }
  bool failed() const { return parser_.hasError(); }

 private:
  enum class Section : uint8_t { NONE, BOOKS, PROGRESS, BOOKMARKS, OTHER };

  JsonCallbacks makeCallbacks() {
    JsonCallbacks cb = {};
    cb.ctx = this;
    cb.onKey = [](void* c, const char* k, size_t n) { static_cast<CcxSnapshotParse*>(c)->onKey(k, n); };
    cb.onString = [](void* c, const char* v, size_t n) { static_cast<CcxSnapshotParse*>(c)->onValue(v, n, true); };
    cb.onNumber = [](void* c, const char* v, size_t n) { static_cast<CcxSnapshotParse*>(c)->onValue(v, n, false); };
    cb.onBool = [](void* c, bool) { static_cast<CcxSnapshotParse*>(c)->key_.clear(); };
    cb.onNull = [](void* c) { static_cast<CcxSnapshotParse*>(c)->key_.clear(); };
    cb.onObjectStart = [](void* c) { static_cast<CcxSnapshotParse*>(c)->onObjectStart(); };
    cb.onObjectEnd = [](void* c) { static_cast<CcxSnapshotParse*>(c)->onObjectEnd(); };
    cb.onArrayStart = [](void* c) { static_cast<CcxSnapshotParse*>(c)->onArrayStart(); };
    cb.onArrayEnd = [](void* c) { static_cast<CcxSnapshotParse*>(c)->onArrayEnd(); };
    return cb;
  }

  void onKey(const char* k, size_t n) {
    key_.assign(k, n);
    if (depth_ == 1) {  // top-level key names the upcoming section
      if (key_ == "books") pendingSection_ = Section::BOOKS;
      else if (key_ == "progress") pendingSection_ = Section::PROGRESS;
      else if (key_ == "bookmarks") pendingSection_ = Section::BOOKMARKS;
      else pendingSection_ = Section::OTHER;
    }
  }

  void onArrayStart() {
    depth_++;
    if (depth_ == 2) { section_ = pendingSection_; pendingSection_ = Section::OTHER; }
  }
  void onArrayEnd() {
    depth_--;
    if (depth_ == 1) section_ = Section::NONE;
  }
  void onObjectStart() {
    depth_++;
    if (depth_ == 3 && section_ != Section::NONE && section_ != Section::OTHER) resetRow();
  }
  void onObjectEnd() {
    if (depth_ == 3) emitRow();
    depth_--;
  }

  void onValue(const char* v, size_t n, bool isString) {
    if (depth_ != 3 || section_ == Section::NONE || section_ == Section::OTHER) { key_.clear(); return; }
    std::string val(v, n);
    if (isString) setStr(key_, val); else setNum(key_, val);
    key_.clear();
  }

  void resetRow() { book_ = {}; prog_ = {}; bm_ = {}; }

  void setStr(const std::string& k, const std::string& v) {
    switch (section_) {
      case Section::BOOKS:
        if (k == "digest") book_.digest = v;
        else if (k == "guid") book_.guid = v;
        else if (k == "title") book_.title = v.substr(0, 64);
        break;
      case Section::PROGRESS:
        if (k == "bookDigest") prog_.bookDigest = v;
        break;
      case Section::BOOKMARKS:
        if (k == "guid") bm_.guid = v;
        else if (k == "bookDigest") bm_.bookDigest = v;
        else if (k == "label") bm_.label = v.substr(0, 96);
        break;
      default: break;
    }
  }

  void setNum(const std::string& k, const std::string& v) {
    long long ll = std::strtoll(v.c_str(), nullptr, 10);
    float f = std::strtof(v.c_str(), nullptr);
    switch (section_) {
      case Section::BOOKS:
        if (k == "updatedAt") book_.updatedAt = ll;
        else if (k == "deleted") book_.deleted = static_cast<int>(ll);
        else if (k == "format") book_.format = static_cast<int>(ll);
        break;
      case Section::PROGRESS:
        if (k == "spineIndex") prog_.spineIndex = static_cast<int>(ll);
        else if (k == "percentage") prog_.percentage = f;
        else if (k == "updatedAt") prog_.updatedAt = ll;
        else if (k == "deleted") prog_.deleted = static_cast<int>(ll);
        break;
      case Section::BOOKMARKS:
        if (k == "spineIndex") bm_.spineIndex = static_cast<int>(ll);
        else if (k == "percentage") bm_.percentage = f;
        else if (k == "createdAt") bm_.createdAt = ll;
        else if (k == "updatedAt") bm_.updatedAt = ll;
        else if (k == "deleted") bm_.deleted = static_cast<int>(ll);
        break;
      default: break;
    }
  }

  void emitRow() {
    switch (section_) {
      case Section::BOOKS:     if (!book_.digest.empty()) acc_.foldBook(book_); break;
      case Section::PROGRESS:  if (!prog_.bookDigest.empty()) acc_.foldProgress(prog_); break;
      case Section::BOOKMARKS: if (!bm_.guid.empty()) acc_.foldBookmark(bm_); break;
      default: break;
    }
  }

  ccxsync::Accumulator& acc_;
  StreamingJsonParser parser_;
  Section section_ = Section::NONE, pendingSection_ = Section::OTHER;
  int depth_ = 0;
  std::string key_;
  ccxsync::MergedBook book_;
  ccxsync::MergedProgress prog_;
  ccxsync::MergedBookmark bm_;
};
