#include "AllBooksActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include "BookDetailActivity.h"
#include "MappedInputManager.h"
#include "ReadingState.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;  // hold-to-open-detail threshold
constexpr size_t NAME_BUFFER_SIZE = 256;
constexpr size_t MAX_BOOKS = 800;        // hard cap so a huge card can't exhaust RAM
constexpr int MAX_DIRS = 400;            // bound directories *processed*
constexpr size_t MAX_QUEUED_DIRS = 512;  // bound directories *queued* (heap safety)

// Display title: basename without directory or extension.
std::string bookTitle(const std::string& path) {
  const auto slash = path.rfind('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const auto dot = name.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    name = name.substr(0, dot);
  }
  return name;
}
}  // namespace

void AllBooksActivity::loadBooks() {
  books.clear();
  books.reserve(128);

  auto nameBuf = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!nameBuf) {
    LOG_ERR("AllBooks", "OOM: name buffer");
    return;
  }

  // Iterative directory walk (no recursion): pop a directory, list its entries,
  // push child directories. Only one SD directory handle is open at a time.
  std::vector<std::string> dirStack;
  dirStack.reserve(16);
  dirStack.emplace_back("/");

  int dirsScanned = 0;
  while (!dirStack.empty() && books.size() < MAX_BOOKS && dirsScanned < MAX_DIRS) {
    const std::string dirPath = std::move(dirStack.back());
    dirStack.pop_back();
    dirsScanned++;

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      continue;
    }
    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(nameBuf.get(), NAME_BUFFER_SIZE);
      const bool isDir = entry.isDirectory();
      entry.close();

      // Skip empty names (SdFat returns "" when a long name overflows nameBuf or
      // on an LFN read error; an empty name would collapse `full` back to the
      // parent path and re-scan it in a loop), hidden entries (incl.
      // /.crosspoint), and the FAT system folder.
      if (nameBuf[0] == '\0' || nameBuf[0] == '.' || strcmp(nameBuf.get(), "System Volume Information") == 0) {
        continue;
      }

      std::string full = dirPath;
      if (full.empty() || full.back() != '/') {
        full += '/';
      }
      full += nameBuf.get();

      if (isDir) {
        // MAX_DIRS caps directories processed; also bound the queue so a very
        // wide tree can't push unbounded heap strings (new throws -> abort under
        // -fno-exceptions).
        if (dirStack.size() < MAX_QUEUED_DIRS) {
          dirStack.push_back(std::move(full));
        }
      } else if (FsHelpers::hasEpubExtension(full) || FsHelpers::hasXtcExtension(full) ||
                 FsHelpers::hasTxtExtension(full) || FsHelpers::hasMarkdownExtension(full)) {
        if (books.size() < MAX_BOOKS) {
          books.push_back(std::move(full));
        }
      }
    }
    dir.close();
  }

  FsHelpers::sortFileList(books);
}

void AllBooksActivity::onEnter() {
  Activity::onEnter();

  // The scan can touch many directories; show a transient status while it runs.
  GUI.drawPopup(renderer, tr(STR_SCANNING));
  renderer.displayBuffer();

  loadBooks();
  selectorIndex = 0;
  requestUpdate(true);
}

void AllBooksActivity::onExit() {
  Activity::onExit();
  books.clear();
}

void AllBooksActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

  // After a long-press has fired, swallow input until Confirm is physically
  // released, so the release doesn't also open the book.
  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      longPressFired = false;
    }
    return;
  }

  // Long-press Confirm opens the book's detail screen; a normal Confirm-release
  // (below) still opens the book directly, preserving quick resume.
  if (!books.empty() && selectorIndex < static_cast<int>(books.size()) &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    startActivityForResult(std::make_unique<BookDetailActivity>(renderer, mappedInput, books[selectorIndex]),
                           [this](const ActivityResult&) {
                             // A delete inside the detail screen changes the library; re-scan.
                             loadBooks();
                             if (books.empty()) {
                               selectorIndex = 0;
                             } else if (selectorIndex >= static_cast<int>(books.size())) {
                               selectorIndex = static_cast<int>(books.size()) - 1;
                             }
                             requestUpdate(true);
                           });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!books.empty() && selectorIndex < static_cast<int>(books.size())) {
      onSelectBook(books[selectorIndex]);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int listSize = static_cast<int>(books.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, listSize);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, listSize, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, listSize, pageItems);
    requestUpdate();
  });
}

void AllBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_ALL_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_BOOKS));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(books.size()), selectorIndex,
        [this](int index) { return bookTitle(books[index]); }, nullptr,
        [this](int index) { return ReadingState::glyphForBook(books[index]); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
