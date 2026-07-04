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
  // Cipher daemon diamond emblem in place of the old logo bitmap (OS design comp).
  CipherEmblem::drawEmblem(renderer, pageWidth / 2, pageHeight / 2 - 10, 62);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 82, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 107, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
