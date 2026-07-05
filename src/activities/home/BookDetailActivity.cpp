#include "BookDetailActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "ReadingState.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ACTION_COUNT = 2;  // Continue Reading, Delete
constexpr int COVER_W = 100;
constexpr int COVER_H = 150;

// Basename without directory or extension, matching AllBooksActivity's fallback.
std::string basenameTitle(const std::string& path) {
  const auto slash = path.rfind('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const auto dot = name.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    name = name.substr(0, dot);
  }
  return name;
}
}  // namespace

void BookDetailActivity::onEnter() {
  Activity::onEnter();

  // Prefer rich metadata from the recents store; a never-opened book only has
  // its filename, so fall back to that (no author, no cover).
  const RecentBook data = RECENT_BOOKS.getDataFromBook(bookPath);
  title = data.title.empty() ? basenameTitle(bookPath) : data.title;
  author = data.author;
  coverBmpPath = data.coverBmpPath;
  selectedAction = 0;
  requestUpdate();
}

void BookDetailActivity::onExit() { Activity::onExit(); }

void BookDetailActivity::loop() {
  buttonNavigator.onNext([this] {
    selectedAction = ButtonNavigator::nextIndex(selectedAction, ACTION_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedAction = ButtonNavigator::previousIndex(selectedAction, ACTION_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedAction == 0) {
      onSelectBook(bookPath);  // Continue Reading -> reader
    } else {
      promptDelete();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void BookDetailActivity::promptDelete() {
  auto handler = [this](const ActivityResult& res) {
    if (res.isCancelled) {
      return;
    }
    // Match FileBrowser's file delete (removes the file; the .crosspoint cache
    // is left, same as deleting from the file browser), then drop it from the
    // recents list so it doesn't linger there.
    Storage.remove(bookPath.c_str());
    if (RECENT_BOOKS.removeByPath(bookPath)) {
      RECENT_BOOKS.saveToFile();
    }
    finish();  // back to AllBooks, whose result handler re-scans the library
  };
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_BOOK_CONFIRM), title),
      std::move(handler));
}

void BookDetailActivity::drawCover(int x, int y, int w, int h) {
  bool drawn = false;
  if (!coverBmpPath.empty()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const std::string thumbPath = UITheme::getCoverThumbPath(coverBmpPath, metrics.homeCoverHeight);
    HalFile file;  // destructor closes on every exit path
    if (Storage.openFileForRead("DETAIL", thumbPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        const float scale = std::min({static_cast<float>(w) / static_cast<float>(bitmap.getWidth()),
                                      static_cast<float>(h) / static_cast<float>(bitmap.getHeight()), 1.0f});
        const int dw = static_cast<int>(static_cast<float>(bitmap.getWidth()) * scale);
        const int dh = static_cast<int>(static_cast<float>(bitmap.getHeight()) * scale);
        if (dw > 0 && dh > 0) {
          const int cx = x + (w - dw) / 2;
          const int cy = y + (h - dh) / 2;
          renderer.drawBitmap(bitmap, cx, cy, dw, dh);
          renderer.drawRect(cx - 1, cy - 1, dw + 2, dh + 2, true);
          drawn = true;
        }
      }
      file.close();
    }
  }
  if (!drawn) {
    // No cached cover (never opened on Home): a plain bordered book box.
    renderer.drawRect(x, y, w, h, true);
  }
}

void BookDetailActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BOOK_DETAILS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int coverX = metrics.contentSidePadding;
  drawCover(coverX, contentTop, COVER_W, COVER_H);

  // Title / author / reading state to the right of the cover.
  const int textX = coverX + COVER_W + metrics.contentSidePadding;
  const int textW = pageWidth - textX - metrics.contentSidePadding;
  int y = contentTop + 6;

  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(UI_12_FONT_ID, textX, y, renderer.truncatedText(UI_12_FONT_ID, title.c_str(), textW).c_str());
  y += titleLineH + 6;
  if (!author.empty()) {
    renderer.drawText(UI_10_FONT_ID, textX, y, renderer.truncatedText(UI_10_FONT_ID, author.c_str(), textW).c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID) + 6;
  }
  const UIIcon state = ReadingState::glyphForBook(bookPath);
  // tr() token-pastes StrId:: onto its argument, so it can't take an expression;
  // resolve to a StrId first and go through I18N.get().
  const StrId stateStr = (state == UIIcon::BookReading) ? StrId::STR_STATE_READING : StrId::STR_STATE_NEW;
  renderer.drawText(UI_10_FONT_ID, textX, y, I18N.get(stateStr));

  // Action list below the cover block.
  const int listTop = contentTop + COVER_H + metrics.verticalSpacing * 2;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawButtonMenu(
      renderer, Rect{0, listTop, pageWidth, listHeight}, ACTION_COUNT, selectedAction,
      [](int index) { return std::string(I18N.get(index == 0 ? StrId::STR_CONTINUE_READING : StrId::STR_DELETE)); },
      nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
