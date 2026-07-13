#pragma once

#include <string>
#include <vector>

// Sync state for the CipherCodex WebDAV sync feature: global config/creds/
// tombstones live in /.crosspoint/ccxsync.json (singleton, mirrors
// KOReaderCredentialStore); per-book state is a small JSON file living
// alongside progress.bin in the book's cache dir, addressed by cacheDir per
// call (no singleton -- there can be many open books' worth of state).
struct CcxBookState {
  std::string path;                       // SD path of the epub (for orphan detection)
  long long fileSize = 0, fileMtime = 0;  // digest cache validity key
  std::string digest, guid;               // 32-hex partialMD5; UUIDv4-hex row guid
  long long firstSeenAt = 0;              // book row updatedAt on the wire (constant after mint)
  long long localStamp = 0;               // updatedAt of the last locally-authored progress
  int pendSpine = -1;
  float pendPct = 0;
  long long pendAt = 0;              // remote-won position awaiting reader apply
  std::vector<std::string> bmGuids;  // bookmark guids as of last sync (deletion detection), cap 32
  bool dirtyProgress = false, dirtyBookmarks = false;
};

class CcxSyncState {
 public:
  CcxSyncState(const CcxSyncState&) = delete;
  CcxSyncState& operator=(const CcxSyncState&) = delete;

  static CcxSyncState& getInstance() { return instance; }

  // Global config: /.crosspoint/ccxsync.json
  bool loadGlobal();
  bool saveGlobal();  // mints deviceId (if still empty) before writing

  std::string url, user, pass, deviceId;
  long long lastSyncAt = 0;

  struct Tombstone {
    std::string guid;  // empty = book tombstone, non-empty = bookmark tombstone
    std::string digest;
    long long at = 0;
  };
  static constexpr size_t TOMBSTONES_CAP = 256;
  std::vector<Tombstone> tombstones;  // oldest dropped past cap (enforced on save)

  bool hasConfig() const { return !url.empty(); }

  // Per-book state: <cacheDir>/ccxsync.json
  static bool loadBook(const std::string& cacheDir, CcxBookState& out);   // false = no state yet
  static bool saveBook(const std::string& cacheDir, const CcxBookState& s);
  static void markProgressDirty(const std::string& cacheDir);   // load->set->save (reader-exit hook)
  static void markBookmarksDirty(const std::string& cacheDir);  // bookmark-save hook

  // 16 bytes from esp_random(), rendered lowercase hex (32 chars). Shared by
  // deviceId minting here and row-guid minting (Task 6).
  static std::string newGuid();

 private:
  CcxSyncState() = default;
  static CcxSyncState instance;
};

#define CCXSYNC_STATE CcxSyncState::getInstance()
