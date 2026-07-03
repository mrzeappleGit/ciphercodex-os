#pragma once

#include "activities/Activity.h"

/**
 * Read-only reading statistics: time totals, streak, pages turned, and a
 * seven-day bar strip. Data comes entirely from READING_STATS.
 */
class ReadingStatsActivity final : public Activity {
 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStats", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
