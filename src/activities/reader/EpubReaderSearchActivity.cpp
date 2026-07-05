#include "EpubReaderSearchActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Print.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_HITS = 60;   // cap results so a common word can't blow memory
constexpr size_t SCAN_CHUNK = 1024;

std::string toLowerAscii(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

// A Print sink that streaming-searches chapter HTML for a (lowercased) query as
// bytes arrive: strips tags, folds ASCII case, and keeps only a small rolling
// window so a whole chapter never sits in RAM. Matches spanning a chunk
// boundary survive because the window carries the last (query-1) chars.
class HtmlSearchSink : public Print {
  const std::string query;
  std::string window;
  bool inTag = false;
  bool foundFlag = false;

 public:
  explicit HtmlSearchSink(std::string lowerQuery) : query(std::move(lowerQuery)) {}

  bool found() {
    if (!foundFlag && !query.empty() && window.find(query) != std::string::npos) {
      foundFlag = true;
    }
    return foundFlag;
  }

  size_t write(uint8_t b) override {
    if (foundFlag || query.empty()) return 1;
    char c = static_cast<char>(b);
    if (c == '<') {
      inTag = true;
    } else if (c == '>') {
      inTag = false;
      window += ' ';  // a tag is a word boundary
    } else if (!inTag) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      window += c;
    }
    if (window.size() >= query.size() * 2) {
      if (window.find(query) != std::string::npos) {
        foundFlag = true;
        return 1;
      }
      window.erase(0, window.size() - (query.size() - 1));
    }
    return 1;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    for (size_t i = 0; i < size && !foundFlag; i++) {
      write(buffer[i]);
    }
    return size;
  }
};
}  // namespace

void EpubReaderSearchActivity::onEnter() {
  Activity::onEnter();
  if (!scanned) {
    runScan();
    scanned = true;
  }
  selectorIndex = 0;
  requestUpdate(true);
}

void EpubReaderSearchActivity::onExit() {
  Activity::onExit();
  hits.clear();
}

void EpubReaderSearchActivity::runScan() {
  hits.clear();
  if (!epub || query.empty()) return;
  hits.reserve(MAX_HITS);

  const std::string lowerQuery = toLowerAscii(query);
  const int count = epub->getSpineItemsCount();
  const Rect popup = GUI.drawPopup(renderer, tr(STR_SEARCHING));

  for (int i = 0; i < count && hits.size() < MAX_HITS; i++) {
    GUI.fillPopupProgress(renderer, popup, count > 0 ? (i * 100 / count) : 100);

    HtmlSearchSink sink(lowerQuery);
    const auto href = epub->getSpineItem(i).href;
    if (!href.empty()) {
      epub->readItemContentsToStream(href, sink, SCAN_CHUNK);
    }
    if (sink.found()) {
      const int tocIdx = epub->getTocIndexForSpineIndex(i);
      std::string title = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : std::string();
      if (title.empty()) {
        title = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(i + 1);
      }
      hits.push_back({i, std::move(title)});
    }
  }
}

void EpubReaderSearchActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  const int total = static_cast<int>(hits.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!hits.empty() && selectorIndex < total) {
      setResult(ChapterResult{hits[selectorIndex].spineIndex, std::string()});
      finish();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (total == 0) return;

  buttonNavigator.onNextRelease([this, total] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, total);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, total] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, total);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, total, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, total, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, total, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, total, pageItems);
    requestUpdate();
  });
}

void EpubReaderSearchActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SEARCH_RESULTS));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  if (hits.empty()) {
    renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, contentTop + 20, tr(STR_NO_MATCHES));
  } else {
    GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, static_cast<int>(hits.size()),
                 selectorIndex, [this](int index) { return hits[index].title; });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
