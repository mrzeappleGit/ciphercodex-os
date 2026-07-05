#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Chapter-level in-book search: scans each spine item's raw HTML for the query
// (streaming, tag-stripped) and lists the chapters that contain it. Selecting a
// result returns a ChapterResult so the reader jumps to that chapter's start —
// deliberately viewport-independent (no page-cache math), so it can never
// mis-align a jump.
class EpubReaderSearchActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::string query;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool scanned = false;

  struct Hit {
    int spineIndex;
    std::string title;
  };
  std::vector<Hit> hits;

  void runScan();

 public:
  EpubReaderSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::shared_ptr<Epub>& epub,
                           std::string query)
      : Activity("EpubReaderSearch", renderer, mappedInput), epub(epub), query(std::move(query)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
