#pragma once

#include <cstdint>
#include <string>

// End-to-end WebDAV sync run for the CipherCodex feature: SD scan -> upload
// new local books -> pull every device's snapshot -> LWW merge+apply
// (tombstones, pending progress, bookmarks) -> download new remote books ->
// push this device's snapshot. Blocking; the caller (a background task
// spawned from a settings activity) drives it and renders `onPhase` progress.
class CcxSyncEngine {
 public:
  struct Summary {
    int booksUp = 0, booksDown = 0, entities = 0, tombstones = 0;
    std::string error;
    bool ok() const { return error.empty(); }
  };
  enum class Phase : uint8_t { SCAN, UPLOAD, PULL, APPLY, DOWNLOAD, PUSH, DONE };
  using PhaseFn = void (*)(void* ctx, Phase phase, int itemsDone, int itemsTotal);

  // Blocking; call from an activity's background task, not the render loop.
  // `cancelFlag`, if non-null, is polled between files and between phases.
  static Summary run(PhaseFn onPhase, void* ctx, bool* cancelFlag);
};
