#pragma once

#include <string>
#include <vector>

// Pure LWW merge decisions for the CipherCodex WebDAV sync (frozen phase3b
// contract). Header-only and dependency-free so the host GoogleTest suite
// covers it (CipherCodexProgress.h pattern). Rows are folded one at a time by
// the streaming snapshot parser; whole snapshots are never held in memory.
namespace ccxsync {

struct MergedBook     { std::string digest, guid, title; long long updatedAt = 0; int deleted = 0; int format = 1; };
struct MergedProgress { std::string bookDigest; int spineIndex = 0; float percentage = 0.f; long long updatedAt = 0; int deleted = 0; };
struct MergedBookmark { std::string guid, bookDigest, label; int spineIndex = 0; float percentage = 0.f; long long createdAt = 0, updatedAt = 0; int deleted = 0; };

// True when the remote row should replace a local row carrying
// (localUpdatedAt, localDeleted). Tombstone wins updatedAt ties.
inline bool wins(long long remoteUpdatedAt, int remoteDeleted, long long localUpdatedAt, int localDeleted) {
  return remoteUpdatedAt > localUpdatedAt ||
         (remoteUpdatedAt == localUpdatedAt && remoteDeleted == 1 && localDeleted == 0);
}

class Accumulator {
 public:
  static constexpr size_t BOOKS_CAP = 400;      // heap bound: ~150B/row worst case
  static constexpr size_t BOOKMARKS_CAP = 256;  // across all local books

  void setLocalDigestFilter(std::vector<std::string> digests) { filter_ = std::move(digests); hasFilter_ = true; }

  void foldBook(const MergedBook& b) {
    if (b.format != 1) { dropped_++; return; }  // epub-only on X4 (1 = epub on the wire)
    for (auto& cur : books_) {
      if (cur.digest == b.digest) {
        if (wins(b.updatedAt, b.deleted, cur.updatedAt, cur.deleted)) cur = b;
        return;
      }
    }
    if (books_.size() >= BOOKS_CAP) { dropped_++; return; }
    books_.push_back(b);
  }

  void foldProgress(const MergedProgress& p) {
    if (!inFilter(p.bookDigest)) { dropped_++; return; }
    for (auto& cur : progress_) {
      if (cur.bookDigest == p.bookDigest) {
        if (wins(p.updatedAt, p.deleted, cur.updatedAt, cur.deleted)) cur = p;
        return;
      }
    }
    progress_.push_back(p);  // bounded by filter size (local book count)
  }

  void foldBookmark(const MergedBookmark& m) {
    if (!inFilter(m.bookDigest)) { dropped_++; return; }
    for (auto& cur : bookmarks_) {
      if (cur.guid == m.guid) {
        if (wins(m.updatedAt, m.deleted, cur.updatedAt, cur.deleted)) cur = m;
        return;
      }
    }
    if (bookmarks_.size() >= BOOKMARKS_CAP) { dropped_++; return; }
    bookmarks_.push_back(m);
  }

  const std::vector<MergedBook>& books() const { return books_; }
  const std::vector<MergedProgress>& progress() const { return progress_; }
  const std::vector<MergedBookmark>& bookmarks() const { return bookmarks_; }
  size_t dropped() const { return dropped_; }

 private:
  bool inFilter(const std::string& digest) const {
    if (!hasFilter_) return true;
    for (const auto& d : filter_) if (d == digest) return true;
    return false;
  }
  std::vector<MergedBook> books_;
  std::vector<MergedProgress> progress_;
  std::vector<MergedBookmark> bookmarks_;
  std::vector<std::string> filter_;
  bool hasFilter_ = false;
  size_t dropped_ = 0;
};

}  // namespace ccxsync
