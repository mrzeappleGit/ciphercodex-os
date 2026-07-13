#include "CcxSyncEngine.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <vector>

#include "CcxMerge.h"
#include "CcxSnapshotParse.h"
#include "CcxSnapshotWrite.h"
#include "CcxSyncState.h"
#include "CcxWebDav.h"

// src/ is not on lib/'s include path (no lib currently reaches into it; this
// is the first). Relative paths instead of a project-wide -Isrc build-flag
// change, to keep this a self-contained, minimal-diff dependency.
#include "../../src/BookmarkEntry.h"
#include "../../src/JsonSettingsIO.h"
#include "../../src/util/BookmarkUtil.h"

// Phase implementations for CcxSyncEngine::run(). See CcxSyncEngine.h for the
// phase sequence and task-6-brief.md for the wire-contract rules each step
// below follows (APPLY in particular).
namespace {

constexpr int MAX_DIRS = 400;             // AllBooksActivity::loadBooks bounds, epub-only here
constexpr size_t MAX_QUEUED_DIRS = 512;
constexpr size_t MAX_BOOKS = 800;
constexpr size_t NAME_BUFFER_SIZE = 256;
constexpr long long MIN_VALID_CLOCK_S = 1577836800LL;  // 2020-01-01, backstop against an unset RTC

struct LocalBook {
  std::string path;
  std::string cacheDir;
  CcxBookState state;
};

void report(CcxSyncEngine::PhaseFn onPhase, void* ctx, CcxSyncEngine::Phase phase, int done, int total) {
  if (onPhase) onPhase(ctx, phase, done, total);
}

bool isTombstoned(const std::string& digest) {
  for (const auto& t : CCXSYNC_STATE.tombstones) {
    if (t.guid.empty() && t.digest == digest) return true;
  }
  return false;
}

// Reuses Epub's existing hash-of-fullpath cache-key formula (Epub.h ctor)
// instead of re-deriving it here; no filesystem/parse cost, just a string hash.
std::string cacheDirFor(const std::string& epubPath) { return Epub(epubPath, "/.crosspoint").getCachePath(); }

// Display title: basename without directory or extension (AllBooksActivity::bookTitle
// pattern; not reused directly since that one is a private anonymous-namespace
// function in a different translation unit).
std::string bookTitleFromPath(const std::string& path) {
  const auto slash = path.rfind('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const auto dot = name.rfind('.');
  if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
  return name;
}

std::string sanitizeTitle(const std::string& raw, const std::string& digest) {
  std::string s;
  s.reserve(raw.size());
  for (char c : raw) {
    if (std::strchr("\\/:*?\"<>|", c) == nullptr) s += c;
  }
  const size_t b = s.find_first_not_of(" \t\n\r");
  const size_t e = s.find_last_not_of(" \t\n\r");
  s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
  if (s.empty()) s = "book-" + digest.substr(0, 8);
  return s;
}

// progress.bin is 6 bytes: spineIndex, pageNumber, pageCount, each little-endian
// uint16 (EpubReaderUtils::saveProgress). memcpy only -- RISC-V faults on an
// unaligned struct-cast read (see .skills/SKILL.md RISC-V Alignment).
bool readProgressBin(const std::string& cacheDir, uint16_t& spineIndex, uint16_t& pageNumber, uint16_t& pageCount) {
  HalFile f;
  if (!Storage.openFileForRead("CCXENG", cacheDir + "/progress.bin", f)) return false;
  uint8_t data[6];
  if (f.read(data, sizeof(data)) != sizeof(data)) return false;
  memcpy(&spineIndex, data, 2);
  memcpy(&pageNumber, data + 2, 2);
  memcpy(&pageCount, data + 4, 2);
  return true;
}

// CcxWebDav::DataFn sink that feeds one HTTP response chunk into the streaming
// snapshot parser.
bool feedSnapshotChunk(void* ctx, const char* data, size_t len) {
  static_cast<CcxSnapshotParse*>(ctx)->feed(data, len);
  return true;
}

// Iterative SD walk mirroring AllBooksActivity::loadBooks bounds exactly, but
// epub-only (books/xtc/txt aren't part of this sync feature). Returns false
// only on cancellation.
bool scanEpubs(std::vector<std::string>& out, bool* cancelFlag) {
  out.clear();
  out.reserve(128);

  auto nameBuf = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!nameBuf) {
    LOG_ERR("CCXENG", "OOM: name buffer");
    return true;  // not a cancellation -- scan just yields nothing this run
  }

  std::vector<std::string> dirStack;
  dirStack.reserve(16);
  dirStack.emplace_back("/");

  int dirsScanned = 0;
  while (!dirStack.empty() && out.size() < MAX_BOOKS && dirsScanned < MAX_DIRS) {
    if (cancelFlag && *cancelFlag) return false;
    const std::string dirPath = std::move(dirStack.back());
    dirStack.pop_back();
    dirsScanned++;

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) continue;
    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(nameBuf.get(), NAME_BUFFER_SIZE);
      const bool isDir = entry.isDirectory();
      entry.close();

      if (nameBuf[0] == '\0' || nameBuf[0] == '.' || strcmp(nameBuf.get(), "System Volume Information") == 0) {
        continue;
      }

      std::string full = dirPath;
      if (full.empty() || full.back() != '/') full += '/';
      full += nameBuf.get();

      if (isDir) {
        if (dirStack.size() < MAX_QUEUED_DIRS) dirStack.push_back(std::move(full));
      } else if (FsHelpers::hasEpubExtension(full)) {
        if (out.size() < MAX_BOOKS) {
          out.push_back(std::move(full));
        } else {
          LOG_INF("CCXENG", "scan: capped at %zu books", MAX_BOOKS);
        }
      }
    }
    dir.close();
  }
  if (dirsScanned >= MAX_DIRS) LOG_INF("CCXENG", "scan: capped at %d dirs", MAX_DIRS);
  return true;
}

// Loads (or mints) this book's sync state. The expensive digest computation
// only runs when the state file is missing or the file size changed -- the
// cache CcxBookState exists for.
//
// ponytail: fileMtime is part of the change-detection key per the state
// schema, but HalFile/HalStorage expose no mtime accessor (grepped; nothing
// in this codebase reads SdFat's getModifyDateTime through the HAL). Keyed
// on fileSize alone here; add mtime once HalFile exposes it, if a same-size
// content edit needs to be caught.
bool buildLocalBook(const std::string& path, long long now, LocalBook& out) {
  out.path = path;
  out.cacheDir = cacheDirFor(path);
  CcxBookState state;
  const bool had = CcxSyncState::loadBook(out.cacheDir, state);

  long long curSize = 0;
  {
    HalFile f;
    if (Storage.openFileForRead("CCXENG", path, f)) curSize = static_cast<long long>(f.fileSize());
  }

  if (!had || state.fileSize != curSize) {
    const std::string digest = KOReaderDocumentId::calculate(path);
    if (digest.empty()) {
      LOG_ERR("CCXENG", "digest failed for %s", path.c_str());
      return false;
    }
    state.path = path;
    state.fileSize = curSize;
    state.fileMtime = 0;
    state.digest = digest;
    if (state.guid.empty()) state.guid = CcxSyncState::newGuid();
    if (state.firstSeenAt == 0) state.firstSeenAt = now;
    CcxSyncState::saveBook(out.cacheDir, state);
  }
  out.state = state;
  return true;
}

const ccxsync::MergedProgress* findProgress(const ccxsync::Accumulator& acc, const std::string& digest) {
  for (const auto& p : acc.progress()) {
    if (p.bookDigest == digest) return &p;
  }
  return nullptr;
}

}  // namespace

CcxSyncEngine::Summary CcxSyncEngine::run(PhaseFn onPhase, void* ctx, bool* cancelFlag) {
  Summary sum;

  CCXSYNC_STATE.loadGlobal();
  if (!CCXSYNC_STATE.hasConfig()) {
    sum.error = "not configured";
    return sum;
  }
  if (time(nullptr) < MIN_VALID_CLOCK_S) {
    sum.error = "clock not set";
    return sum;
  }
  const long long now = static_cast<long long>(time(nullptr)) * 1000LL;

  CcxWebDav dav(CCXSYNC_STATE.url, CCXSYNC_STATE.user, CCXSYNC_STATE.pass);

  // ---- SCAN ----
  report(onPhase, ctx, Phase::SCAN, 0, 0);
  std::vector<std::string> epubPaths;
  if (!scanEpubs(epubPaths, cancelFlag)) {
    sum.error = "cancelled";
    return sum;
  }

  std::vector<LocalBook> localBooks;
  localBooks.reserve(epubPaths.size());
  for (size_t i = 0; i < epubPaths.size(); i++) {
    if (cancelFlag && *cancelFlag) {
      sum.error = "cancelled";
      return sum;
    }
    LocalBook lb;
    if (buildLocalBook(epubPaths[i], now, lb)) localBooks.push_back(std::move(lb));
    report(onPhase, ctx, Phase::SCAN, static_cast<int>(i + 1), static_cast<int>(epubPaths.size()));
  }

  // ---- UPLOAD ----
  report(onPhase, ctx, Phase::UPLOAD, 0, 0);
  if (!dav.mkcol("books/") || !dav.mkcol("state/")) {
    sum.error = "mkcol failed";
    return sum;
  }
  std::vector<std::string> remoteBooks;
  dav.list("books/", remoteBooks, 1000);

  int upTotal = 0;
  for (const auto& b : localBooks) {
    if (!isTombstoned(b.state.digest)) upTotal++;
  }
  int upDone = 0;
  for (const auto& b : localBooks) {
    if (cancelFlag && *cancelFlag) {
      sum.error = "cancelled";
      return sum;
    }
    if (isTombstoned(b.state.digest)) continue;
    const std::string remoteName = b.state.digest + ".epub";
    const bool present = std::find(remoteBooks.begin(), remoteBooks.end(), remoteName) != remoteBooks.end();
    if (!present) {
      if (dav.putFile("books/" + remoteName, b.path, "application/epub+zip")) {
        sum.booksUp++;
      } else {
        LOG_ERR("CCXENG", "upload failed: %s", b.path.c_str());
      }
    }
    upDone++;
    report(onPhase, ctx, Phase::UPLOAD, upDone, upTotal);
  }

  // ---- PULL ----
  report(onPhase, ctx, Phase::PULL, 0, 0);
  std::vector<std::string> stateNames;
  dav.list("state/", stateNames, 32);

  ccxsync::Accumulator acc;
  {
    std::vector<std::string> localDigests;
    localDigests.reserve(localBooks.size());
    for (const auto& b : localBooks) localDigests.push_back(b.state.digest);
    acc.setLocalDigestFilter(std::move(localDigests));
  }

  for (size_t i = 0; i < stateNames.size(); i++) {
    if (cancelFlag && *cancelFlag) {
      sum.error = "cancelled";
      return sum;
    }
    const std::string& name = stateNames[i];
    if (name.size() < 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;  // skip stray *.json.tmp

    CcxSnapshotParse parse(acc);
    const bool httpOk = dav.getStreaming("state/" + name, &feedSnapshotChunk, &parse);
    if (!httpOk || parse.failed()) {
      sum.error = "bad snapshot: " + name;
      return sum;  // better no sync than a partial merge
    }
    report(onPhase, ctx, Phase::PULL, static_cast<int>(i + 1), static_cast<int>(stateNames.size()));
  }

  // ---- APPLY ----
  report(onPhase, ctx, Phase::APPLY, 0, static_cast<int>(localBooks.size()));

  for (const auto& mb : acc.books()) {
    if (mb.deleted != 1 || isTombstoned(mb.digest)) continue;
    bool localHas = false;
    for (const auto& b : localBooks) {
      if (b.state.digest == mb.digest) {
        localHas = true;
        break;
      }
    }
    if (!localHas) continue;
    CcxSyncState::Tombstone t;
    t.guid.clear();
    t.digest = mb.digest;
    t.at = mb.updatedAt;
    CCXSYNC_STATE.tombstones.push_back(t);
    sum.tombstones++;
  }

  for (size_t i = 0; i < localBooks.size(); i++) {
    if (cancelFlag && *cancelFlag) {
      sum.error = "cancelled";
      return sum;
    }
    LocalBook& b = localBooks[i];
    if (!isTombstoned(b.state.digest)) {
      bool stateChanged = false;

      if (const auto* mp = findProgress(acc, b.state.digest)) {
        if (mp->updatedAt > b.state.localStamp) {
          b.state.pendSpine = mp->spineIndex;
          b.state.pendPct = mp->percentage;
          b.state.pendAt = mp->updatedAt;
          stateChanged = true;
          sum.entities++;
        }
      }

      std::vector<BookmarkEntry> entries;
      const std::string bmPath = BookmarkUtil::getBookmarkPath(b.path);
      if (Storage.exists(bmPath.c_str())) {
        String json = Storage.readFile(bmPath.c_str());
        if (!json.isEmpty()) JsonSettingsIO::loadBookmarks(entries, json.c_str());
      }
      bool bmChanged = false;

      for (const auto& mm : acc.bookmarks()) {
        if (mm.bookDigest != b.state.digest) continue;
        auto it = std::find_if(entries.begin(), entries.end(),
                               [&](const BookmarkEntry& e) { return e.guid == mm.guid; });
        if (mm.deleted == 1) {
          if (it != entries.end()) {
            entries.erase(it);
            bmChanged = true;
          }
          continue;
        }
        const bool shouldWrite = (it == entries.end()) || ccxsync::wins(mm.updatedAt, 0, it->updatedAt, 0);
        if (!shouldWrite) continue;
        BookmarkEntry entry;
        entry.xpath.clear();  // empty xpath: ProgressMapper::toCrossPoint falls back to the
                              // percentage/byte-position path cleanly (verified -- no crash/no-op)
        entry.summary = mm.label;
        entry.percentage = mm.percentage;
        entry.computedSpineIndex = static_cast<uint16_t>(mm.spineIndex);
        entry.guid = mm.guid;
        entry.updatedAt = mm.updatedAt;
        if (it != entries.end()) {
          *it = entry;
        } else {
          entries.push_back(entry);
        }
        bmChanged = true;
      }

      // Local bookmark deletions since last sync: a guid we tracked in bmGuids
      // that no longer has a matching entry was removed locally (not by a
      // remote tombstone, which would already be reflected above).
      for (const auto& g : b.state.bmGuids) {
        const bool stillThere =
            std::any_of(entries.begin(), entries.end(), [&](const BookmarkEntry& e) { return e.guid == g; });
        if (stillThere) continue;
        const bool alreadyTombstoned = std::any_of(
            CCXSYNC_STATE.tombstones.begin(), CCXSYNC_STATE.tombstones.end(),
            [&](const CcxSyncState::Tombstone& t) { return t.guid == g; });
        if (alreadyTombstoned) continue;
        CcxSyncState::Tombstone t;
        t.guid = g;
        t.digest = b.state.digest;
        t.at = now;
        CCXSYNC_STATE.tombstones.push_back(t);
      }

      if (b.state.dirtyProgress) {
        b.state.localStamp = now;
        b.state.dirtyProgress = false;
        stateChanged = true;
      }
      if (b.state.dirtyBookmarks) {
        // v1 approximation: X4 can't tell which entry changed, so every entry
        // in a dirty file is re-stamped; entries missing a guid (pre-Task-6
        // bookmarks) mint one now.
        for (auto& e : entries) {
          if (e.guid.empty()) e.guid = CcxSyncState::newGuid();
          e.updatedAt = now;
        }
        bmChanged = true;
        b.state.dirtyBookmarks = false;
        stateChanged = true;
      }

      if (bmChanged) {
        Storage.mkdir(BookmarkUtil::getBookmarksDir().c_str());
        if (!JsonSettingsIO::saveBookmarks(entries, bmPath.c_str())) {
          LOG_ERR("CCXENG", "failed to save bookmarks for %s", b.path.c_str());
        }
        b.state.bmGuids.clear();
        b.state.bmGuids.reserve(entries.size());
        for (const auto& e : entries) {
          if (!e.guid.empty()) b.state.bmGuids.push_back(e.guid);
        }
        stateChanged = true;
      }

      if (stateChanged) CcxSyncState::saveBook(b.cacheDir, b.state);
    }
    report(onPhase, ctx, Phase::APPLY, static_cast<int>(i + 1), static_cast<int>(localBooks.size()));
  }

  // ---- DOWNLOAD ----
  const int dlTotal = static_cast<int>(acc.books().size());
  report(onPhase, ctx, Phase::DOWNLOAD, 0, dlTotal);
  int dlIdx = 0;
  for (const auto& mb : acc.books()) {
    if (cancelFlag && *cancelFlag) {
      sum.error = "cancelled";
      return sum;
    }
    dlIdx++;
    if (mb.deleted == 1 || isTombstoned(mb.digest)) {
      report(onPhase, ctx, Phase::DOWNLOAD, dlIdx, dlTotal);
      continue;
    }
    bool isLocal = false;
    for (const auto& b : localBooks) {
      if (b.state.digest == mb.digest) {
        isLocal = true;
        break;
      }
    }
    if (isLocal) {
      report(onPhase, ctx, Phase::DOWNLOAD, dlIdx, dlTotal);
      continue;
    }

    Storage.mkdir("/Books");
    std::string dst = "/Books/" + sanitizeTitle(mb.title, mb.digest) + ".epub";
    if (Storage.exists(dst.c_str())) {
      dst = "/Books/" + sanitizeTitle(mb.title, mb.digest) + "-" + mb.digest.substr(0, 8) + ".epub";
    }
    if (!dav.getToFile("books/" + mb.digest + ".epub", dst)) {
      LOG_ERR("CCXENG", "download failed: %s", mb.digest.c_str());
      report(onPhase, ctx, Phase::DOWNLOAD, dlIdx, dlTotal);
      continue;
    }

    CcxBookState state;
    state.path = dst;
    state.digest = mb.digest;
    state.guid = mb.guid;
    state.firstSeenAt = mb.updatedAt;
    {
      HalFile f;
      if (Storage.openFileForRead("CCXENG", dst, f)) state.fileSize = static_cast<long long>(f.fileSize());
    }
    CcxSyncState::saveBook(cacheDirFor(dst), state);
    sum.booksDown++;
    report(onPhase, ctx, Phase::DOWNLOAD, dlIdx, dlTotal);
  }

  // ---- PUSH ----
  report(onPhase, ctx, Phase::PUSH, 0, 0);
  if (cancelFlag && *cancelFlag) {
    sum.error = "cancelled";
    return sum;
  }
  constexpr char SNAPSHOT_TMP[] = "/.crosspoint/ccxsync-out.tmp";
  CcxSnapshotWrite writer;
  if (!writer.begin(SNAPSHOT_TMP, CCXSYNC_STATE.deviceId, now)) {
    sum.error = "snapshot write failed";
    return sum;
  }

  for (const auto& b : localBooks) {
    if (isTombstoned(b.state.digest)) continue;
    ccxsync::MergedBook mb;
    mb.digest = b.state.digest;
    mb.guid = b.state.guid;
    mb.title = bookTitleFromPath(b.path);
    mb.deleted = 0;
    mb.format = 1;
    writer.addBook(mb, b.state.firstSeenAt);
  }
  for (const auto& t : CCXSYNC_STATE.tombstones) {
    if (!t.guid.empty()) continue;  // book tombstones only (bookmark tombstones carry a guid)
    ccxsync::MergedBook mb;
    mb.digest = t.digest;
    mb.deleted = 1;
    mb.format = 1;
    writer.addBook(mb, t.at);
  }

  // Book-level percentage isn't derivable from one spine's progress.bin (that
  // only has this-chapter page counts); the codebase has no cached whole-book
  // percentage to reuse (grepped RecentBooksStore/ReadingStats/home-screen --
  // none store one; the only source is Epub::calculateProgress(), which needs
  // a loaded Epub's cumulative spine sizes and isn't cheap to compute for
  // every local book on every sync). Push spineIndex only; a remote device
  // still lands on the right chapter (spine-start apply, Step 3).
  bool loggedNoPercentSource = false;
  for (const auto& b : localBooks) {
    if (isTombstoned(b.state.digest) || b.state.localStamp <= 0) continue;
    uint16_t spineIndex = 0, pageNumber = 0, pageCount = 0;
    if (!readProgressBin(b.cacheDir, spineIndex, pageNumber, pageCount)) continue;
    (void)pageNumber;
    (void)pageCount;  // no book-level percentage source (see comment above); position rides on spineIndex alone
    if (!loggedNoPercentSource) {
      LOG_INF("CCXENG", "no cached book-level percentage; pushing percentage=0, spineIndex carries position");
      loggedNoPercentSource = true;
    }
    ccxsync::MergedProgress mp;
    mp.bookDigest = b.state.digest;
    mp.spineIndex = spineIndex;
    mp.percentage = 0.0f;
    mp.updatedAt = b.state.localStamp;
    mp.deleted = 0;
    writer.addProgress(mp);
  }

  for (const auto& b : localBooks) {
    if (isTombstoned(b.state.digest)) continue;
    const std::string bmPath = BookmarkUtil::getBookmarkPath(b.path);
    if (!Storage.exists(bmPath.c_str())) continue;
    String json = Storage.readFile(bmPath.c_str());
    if (json.isEmpty()) continue;
    std::vector<BookmarkEntry> entries;
    JsonSettingsIO::loadBookmarks(entries, json.c_str());
    for (const auto& e : entries) {
      if (e.guid.empty()) continue;  // never synced (pre-guid, never resaved this run) -- skip
      ccxsync::MergedBookmark mm;
      mm.guid = e.guid;
      mm.bookDigest = b.state.digest;
      mm.label = e.summary;
      mm.spineIndex = e.computedSpineIndex;
      mm.percentage = e.percentage;
      mm.createdAt = e.updatedAt;  // X4 doesn't track creation time separately from last-touch
      mm.updatedAt = e.updatedAt;
      mm.deleted = 0;
      writer.addBookmark(mm);
    }
  }
  for (const auto& t : CCXSYNC_STATE.tombstones) {
    if (t.guid.empty()) continue;  // bookmark tombstones only
    ccxsync::MergedBookmark mm;
    mm.guid = t.guid;
    mm.bookDigest = t.digest;
    mm.deleted = 1;
    mm.updatedAt = t.at;
    writer.addBookmark(mm);
  }

  if (!writer.finish()) {
    sum.error = "snapshot write failed";
    Storage.remove(SNAPSHOT_TMP);
    return sum;
  }

  const std::string tmpName = "state/" + CCXSYNC_STATE.deviceId + ".json.tmp";
  const std::string finalName = "state/" + CCXSYNC_STATE.deviceId + ".json";
  if (!dav.putFile(tmpName, SNAPSHOT_TMP, "application/json") || !dav.move(tmpName, finalName)) {
    sum.error = "push failed";
    Storage.remove(SNAPSHOT_TMP);
    return sum;
  }
  Storage.remove(SNAPSHOT_TMP);

  // ---- DONE ----
  CCXSYNC_STATE.lastSyncAt = now;
  CCXSYNC_STATE.saveGlobal();
  report(onPhase, ctx, Phase::DONE, 1, 1);
  return sum;
}
