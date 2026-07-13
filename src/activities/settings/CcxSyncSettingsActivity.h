#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Settings screen for the CipherCodex WebDAV sync feature. Mirrors
 * KOReaderSettingsActivity's shape: a row list (server URL, username,
 * password, test connection, sync now) each editing CCXSYNC_STATE directly.
 *
 * TEST CONNECTION runs an embedded state machine shaped like
 * KOReaderAuthActivity (WiFi -> NTP -> WebDAV PROPFIND) instead of a separate
 * activity, since the task's file list only calls for these two files.
 */
class CcxSyncSettingsActivity final : public Activity {
 public:
  explicit CcxSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CcxSyncSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return testState == TEST_TESTING; }

 private:
  enum TestState { TEST_IDLE, TEST_WIFI, TEST_TESTING, TEST_SUCCESS, TEST_FAILED };

  ButtonNavigator buttonNavigator;
  size_t selectedIndex = 0;

  TestState testState = TEST_IDLE;
  std::string testResultMessage;

  void handleSelection();
  void startTest();
  void onTestWifiComplete(bool success);
  void performTest();
};
