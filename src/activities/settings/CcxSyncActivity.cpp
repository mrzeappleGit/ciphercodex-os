#include "CcxSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/NtpTime.h"

namespace {
std::string phaseLabel(const CcxSyncEngine::Phase phase, const int itemsDone, const int itemsTotal) {
  using Phase = CcxSyncEngine::Phase;
  switch (phase) {
    case Phase::SCAN:
      return tr(STR_CCXSYNC_SCANNING);
    case Phase::UPLOAD: {
      std::string label = tr(STR_CCXSYNC_UPLOADING);
      if (itemsTotal > 0) label += " " + std::to_string(itemsDone) + "/" + std::to_string(itemsTotal);
      return label;
    }
    case Phase::PULL:
      return tr(STR_CCXSYNC_PULLING);
    case Phase::APPLY:
      return tr(STR_CCXSYNC_APPLYING);
    case Phase::DOWNLOAD: {
      std::string label = tr(STR_CCXSYNC_DOWNLOADING);
      if (itemsTotal > 0) label += " " + std::to_string(itemsDone) + "/" + std::to_string(itemsTotal);
      return label;
    }
    case Phase::PUSH:
    case Phase::DONE:
    default:
      return tr(STR_CCXSYNC_PUSHING);
  }
}
}  // namespace

void CcxSyncActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void CcxSyncActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void CcxSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state_ = SYNCING;
  }
  requestUpdateAndWait();

  NtpTime::syncOnce();
  runSync();
}

// CcxSyncEngine::run() blocks until the whole sync completes; called
// synchronously here the same way FontDownloadActivity::downloadFamily()
// blocks inside its WiFi-selection result handler, not from a spawned task.
void CcxSyncActivity::runSync() {
  cancelRequested_ = false;
  currentPhase_ = CcxSyncEngine::Phase::SCAN;
  itemsDone_ = 0;
  itemsTotal_ = 0;

  summary_ = CcxSyncEngine::run(&CcxSyncActivity::onPhase, this, &cancelRequested_);

  RenderLock lock(*this);
  state_ = summary_.ok() ? COMPLETE : ERROR;
  // No explicit requestUpdate() here: this whole call chain runs inside the
  // WifiSelectionActivity result handler, and ActivityManager::loop()
  // triggers a requestUpdate() automatically once that handler returns
  // (mirrors FontDownloadActivity::onWifiSelectionComplete).
}

// CcxSyncEngine::PhaseFn is a plain function pointer (not std::function), so
// progress reporting goes through this static trampoline. Mirrors the
// HttpDownloader progress lambda in FontDownloadActivity::downloadFamily:
// poll input, flag cancellation, force an immediate render.
void CcxSyncActivity::onPhase(void* ctx, const CcxSyncEngine::Phase phase, const int itemsDone,
                              const int itemsTotal) {
  auto* self = static_cast<CcxSyncActivity*>(ctx);
  self->currentPhase_ = phase;
  self->itemsDone_ = itemsDone;
  self->itemsTotal_ = itemsTotal;

  self->mappedInput.update();
  if (self->mappedInput.isPressed(MappedInputManager::Button::Back) ||
      self->mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    self->cancelRequested_ = true;
  }
  self->requestUpdate(true);
}

void CcxSyncActivity::loop() {
  if (state_ == COMPLETE || state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
  // WIFI_SELECTION: input owned by the pushed WifiSelectionActivity.
  // SYNCING: the blocking runSync() call above owns input polling via onPhase().
}

void CcxSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CCXSYNC_TITLE));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto centerY = (pageHeight - lineHeight) / 2;

  if (state_ == SYNCING) {
    const std::string statusText = phaseLabel(currentPhase_, itemsDone_, itemsTotal_);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, statusText.c_str());

    const int barY = centerY + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, barY, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<size_t>(itemsDone_), static_cast<size_t>(itemsTotal_ > 0 ? itemsTotal_ : 1));

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight * 2, tr(STR_SYNC_COMPLETE), true,
                              EpdFontFamily::BOLD);

    const std::string line1 = std::to_string(summary_.booksUp) + " " + tr(STR_CCXSYNC_BOOKS_UP);
    const std::string line2 = std::to_string(summary_.booksDown) + " " + tr(STR_CCXSYNC_BOOKS_DOWN);
    const std::string line3 = std::to_string(summary_.entities) + " " + tr(STR_CCXSYNC_ENTITIES) + ", " +
                              std::to_string(summary_.tombstones) + " " + tr(STR_CCXSYNC_TOMBSTONES);

    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight / 2, line1.c_str());
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + lineHeight / 2 + 10, line2.c_str());
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + lineHeight * 3 / 2 + 20, line3.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_SYNC_FAILED), true, EpdFontFamily::BOLD);
    if (!summary_.error.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + metrics.verticalSpacing, summary_.error.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  // WIFI_SELECTION: nothing to draw here -- WifiSelectionActivity covers the screen.

  renderer.displayBuffer();
}
