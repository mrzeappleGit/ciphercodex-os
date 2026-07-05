#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Flat, books-only library: recursively scans the SD card for readable books
// and lists them (with reading-state glyphs), unlike FileBrowserActivity which
// shows the raw filesystem (folders + every file type in one directory).
class AllBooksActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::vector<std::string> books;  // full SD paths, sorted
  int selectorIndex = 0;
  bool longPressFired = false;  // guards the release after a long-press-to-detail

  void loadBooks();

 public:
  explicit AllBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AllBooks", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
