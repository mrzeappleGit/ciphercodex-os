#pragma once

#include <CcxSyncEngine.h>

#include <string>

#include "activities/Activity.h"

/**
 * SYNC NOW runner for the CipherCodex WebDAV sync feature. Mirrors
 * FontDownloadActivity's state machine shape: WIFI_SELECTION -> SYNCING ->
 * COMPLETE/ERROR, with CcxSyncEngine::run() called synchronously (it blocks)
 * from the WiFi-selection result handler -- the same mechanism
 * FontDownloadActivity uses for its download, not a spawned task.
 */
class CcxSyncActivity final : public Activity {
 public:
  explicit CcxSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CcxSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == SYNCING; }
  bool skipLoopDelay() override { return true; }

 private:
  enum State { WIFI_SELECTION, SYNCING, COMPLETE, ERROR };

  State state_ = WIFI_SELECTION;
  CcxSyncEngine::Phase currentPhase_ = CcxSyncEngine::Phase::SCAN;
  int itemsDone_ = 0;
  int itemsTotal_ = 0;
  bool cancelRequested_ = false;
  CcxSyncEngine::Summary summary_;

  void onWifiSelectionComplete(bool success);
  void runSync();
  static void onPhase(void* ctx, CcxSyncEngine::Phase phase, int itemsDone, int itemsTotal);
};
