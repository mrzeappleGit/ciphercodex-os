#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/themes/cipher/CipherEmblem.h"
#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Registration frame + corner ticks around the emblem (OS design comp's
  // boot/lock screen): a 1px inner frame inset 22px with 2px corner brackets.
  constexpr int kFrameInset = 22;
  constexpr int kTickInset = 16;
  constexpr int kTickLen = 14;
  renderer.drawRect(kFrameInset, kFrameInset, pageWidth - 2 * kFrameInset, pageHeight - 2 * kFrameInset, 1, true);
  const int r = pageWidth - kTickInset;   // right edge of tick square
  const int b = pageHeight - kTickInset;  // bottom edge of tick square
  renderer.fillRect(kTickInset, kTickInset, kTickLen, 2, true);
  renderer.fillRect(kTickInset, kTickInset, 2, kTickLen, true);
  renderer.fillRect(r - kTickLen, kTickInset, kTickLen, 2, true);
  renderer.fillRect(r - 2, kTickInset, 2, kTickLen, true);
  renderer.fillRect(kTickInset, b - 2, kTickLen, 2, true);
  renderer.fillRect(kTickInset, b - kTickLen, 2, kTickLen, true);
  renderer.fillRect(r - kTickLen, b - 2, kTickLen, 2, true);
  renderer.fillRect(r - 2, b - kTickLen, 2, kTickLen, true);

  // Cipher daemon diamond emblem in place of the old logo bitmap (OS design comp).
  CipherEmblem::drawEmblem(renderer, pageWidth / 2, pageHeight / 2 - 10, 62);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 82, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 107, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 48, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
