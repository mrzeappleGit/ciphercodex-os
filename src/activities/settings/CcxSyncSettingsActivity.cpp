#include "CcxSyncSettingsActivity.h"

#include <CcxSyncState.h>
#include <CcxWebDav.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "CcxSyncActivity.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/NtpTime.h"

namespace {
constexpr int MENU_ITEMS = 5;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_CCXSYNC_SERVER_URL, StrId::STR_USERNAME, StrId::STR_PASSWORD,
                                     StrId::STR_TEST_CONNECTION, StrId::STR_SYNC_NOW};
}  // namespace

void CcxSyncSettingsActivity::onEnter() {
  Activity::onEnter();

  // Singleton fields are blank until the first load; other stores follow the
  // same on-demand-reload pattern (see OpdsServerListActivity::onEnter).
  CCXSYNC_STATE.loadGlobal();

  selectedIndex = 0;
  testState = TEST_IDLE;
  requestUpdate();
}

void CcxSyncSettingsActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void CcxSyncSettingsActivity::loop() {
  if (testState == TEST_SUCCESS || testState == TEST_FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      testState = TEST_IDLE;
      requestUpdate();
    }
    return;
  }
  if (testState == TEST_WIFI || testState == TEST_TESTING) {
    // WifiSelectionActivity (pushed on top) owns input while TEST_WIFI is
    // active; TEST_TESTING is a transient render-only state around the
    // blocking performTest() call below.
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void CcxSyncSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // Server URL - prefill with https:// if empty to save typing
    const std::string prefillUrl = CCXSYNC_STATE.url.empty() ? "https://" : CCXSYNC_STATE.url;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CCXSYNC_SERVER_URL),
                                                                    prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               CCXSYNC_STATE.url = (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               CCXSYNC_STATE.saveGlobal();
                             }
                           });
  } else if (selectedIndex == 1) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CCXSYNC_USERNAME),
                                                                    CCXSYNC_STATE.user, 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               CCXSYNC_STATE.user = std::get<KeyboardResult>(result.data).text;
                               CCXSYNC_STATE.saveGlobal();
                             }
                           });
  } else if (selectedIndex == 2) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CCXSYNC_PASSWORD),
                                                                    CCXSYNC_STATE.pass, 64, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               CCXSYNC_STATE.pass = std::get<KeyboardResult>(result.data).text;
                               CCXSYNC_STATE.saveGlobal();
                             }
                           });
  } else if (selectedIndex == 3) {
    // Test Connection
    if (!CCXSYNC_STATE.hasConfig()) return;
    startTest();
  } else if (selectedIndex == 4) {
    // Sync Now
    if (!CCXSYNC_STATE.hasConfig()) return;
    startActivityForResult(std::make_unique<CcxSyncActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void CcxSyncSettingsActivity::startTest() {
  // Shape mirrors KOReaderAuthActivity::onEnter: skip WiFi selection if
  // already connected.
  if (WiFi.status() == WL_CONNECTED) {
    {
      RenderLock lock(*this);
      testState = TEST_TESTING;
    }
    requestUpdateAndWait();
    performTest();
    return;
  }

  testState = TEST_WIFI;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onTestWifiComplete(!result.isCancelled); });
}

void CcxSyncSettingsActivity::onTestWifiComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock(*this);
      testState = TEST_FAILED;
      testResultMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    testState = TEST_TESTING;
  }
  requestUpdateAndWait();

  performTest();
}

void CcxSyncSettingsActivity::performTest() {
  NtpTime::syncOnce();

  CcxWebDav dav(CCXSYNC_STATE.url, CCXSYNC_STATE.user, CCXSYNC_STATE.pass);
  const bool ok = dav.test();

  {
    RenderLock lock(*this);
    if (ok) {
      testState = TEST_SUCCESS;
      testResultMessage = tr(STR_CONNECTION_OK);
    } else {
      testState = TEST_FAILED;
      testResultMessage = "HTTP " + std::to_string(dav.lastStatus());
    }
  }
  requestUpdate();
}

void CcxSyncSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CCXSYNC_TITLE));

  if (testState != TEST_IDLE) {
    const auto height = renderer.getLineHeight(UI_10_FONT_ID);
    const auto top = (pageHeight - height) / 2;

    if (testState == TEST_SUCCESS) {
      renderer.drawCenteredText(UI_10_FONT_ID, top, testResultMessage.c_str(), true, EpdFontFamily::BOLD);
    } else if (testState == TEST_FAILED) {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CONNECTION_FAILED), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, testResultMessage.c_str());
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_TESTING_CONNECTION));
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, static_cast<int>(selectedIndex),
      [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr, nullptr,
      [this](int index) {
        if (index == 0) return CCXSYNC_STATE.url.empty() ? std::string(tr(STR_NOT_SET)) : CCXSYNC_STATE.url;
        if (index == 1) return CCXSYNC_STATE.user.empty() ? std::string(tr(STR_NOT_SET)) : CCXSYNC_STATE.user;
        if (index == 2) return CCXSYNC_STATE.pass.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        if (index == 3 || index == 4) {
          return CCXSYNC_STATE.hasConfig() ? std::string() : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
        }
        return std::string(tr(STR_NOT_SET));
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
