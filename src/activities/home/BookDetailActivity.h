#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Optional per-book detail screen reached by long-pressing a row in
// AllBooksActivity (Confirm still opens the book directly, preserving quick
// resume). Shows the cover, title/author, reading state, and a small action
// list: Continue Reading (-> reader) or Delete (-> confirm -> remove file).
class BookDetailActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  const std::string bookPath;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  int selectedAction = 0;  // 0 = resume, 1 = delete

  void drawCover(int x, int y, int w, int h);
  void promptDelete();

 public:
  BookDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
      : Activity("BookDetail", renderer, mappedInput), bookPath(std::move(path)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
