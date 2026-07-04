#include "CipherTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Internal constants
namespace {
// Cipher bar glyph: five horizontal bars of varying width, scaled 0.75x from
// the brand SVG (widths 32/24/30/18/26 at 4px tall, 3px gaps).
constexpr int kGlyphBarWidths[] = {24, 18, 22, 13, 19};
constexpr int kGlyphBarCount = 5;
constexpr int kGlyphBarHeight = 3;
constexpr int kGlyphBarGap = 2;
constexpr int kGlyphWidth = 24;
constexpr int kGlyphHeight = kGlyphBarCount * kGlyphBarHeight + (kGlyphBarCount - 1) * kGlyphBarGap;

constexpr int kHeaderTextGap = 12;       // Gap between glyph/chamfer and header text
constexpr int kSubHeaderAccentWidth = 56;
constexpr int kMaxHeaderSubtitleWidth = 200;
constexpr int kMinValueGap = 10;
constexpr int kSubtitleLineGap = 2;

void drawCipherGlyph(const GfxRenderer& renderer, int x, int y) {
  for (int i = 0; i < kGlyphBarCount; ++i) {
    renderer.fillRect(x, y + i * (kGlyphBarHeight + kGlyphBarGap), kGlyphBarWidths[i], kGlyphBarHeight, false);
  }
}

// Home tile layout (hero cover + up to two compact secondary recents)
constexpr int kHomeSlotInset = 10;       // Cover inset within its slot; keeps the selection frame off the art
constexpr int kHomeColumnGap = 14;       // Gap between hero slot, info column, and secondary rows
constexpr int kHomeTextGap = 8;
constexpr int kHomeFrameGap = 4;         // Outer 2px frame to inner 1px frame offset
constexpr int kHomeLabelPadX = 6;
constexpr int kHomeHeroSlotPercent = 48; // Hero slot share of the tile's inner width
constexpr int kPlaceholderBandPadY = 10;

// Angular stand-in cover: bordered book-shaped box with the brand glyph on a
// solid black band, so a missing or unreadable thumb never leaves a blank slot.
void drawCoverPlaceholder(const GfxRenderer& renderer, int x, int y, int w, int h) {
  renderer.drawRect(x, y, w, h, true);
  const int bandH = kGlyphHeight + 2 * kPlaceholderBandPadY;
  const int bandY = y + (h - bandH) / 2;
  renderer.fillRect(x, bandY, w, bandH, true);
  drawCipherGlyph(renderer, x + (w - kGlyphWidth) / 2, bandY + kPlaceholderBandPadY);
}

// Streams the cover thumb from SD, downscale-fits and centers it in the slot;
// falls back to the placeholder when there is no usable thumb. First-render
// only: the result gets snapshotted into the cover buffer.
void drawCoverInSlot(const GfxRenderer& renderer, const std::string& coverBmpPath, int slotX, int slotY, int slotW,
                     int slotH) {
  const int maxW = slotW - 2 * kHomeSlotInset;
  const int maxH = slotH - 2 * kHomeSlotInset;
  if (maxW <= 0 || maxH <= 0) {
    return;
  }

  bool coverDrawn = false;
  if (!coverBmpPath.empty()) {
    const std::string thumbPath = UITheme::getCoverThumbPath(coverBmpPath, CipherMetrics::values.homeCoverHeight);
    HalFile file;  // destructor closes the handle on every exit path
    if (Storage.openFileForRead("HOME", thumbPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        // drawBitmap downscales to fit but anchors top-left; compute the scaled
        // size here so the cover can be centered within the slot.
        const float scale = std::min({static_cast<float>(maxW) / static_cast<float>(bitmap.getWidth()),
                                      static_cast<float>(maxH) / static_cast<float>(bitmap.getHeight()), 1.0f});
        const int drawW = static_cast<int>(static_cast<float>(bitmap.getWidth()) * scale);
        const int drawH = static_cast<int>(static_cast<float>(bitmap.getHeight()) * scale);
        if (drawW > 0 && drawH > 0) {
          const int coverX = slotX + (slotW - drawW) / 2;
          const int coverY = slotY + (slotH - drawH) / 2;
          renderer.drawBitmap(bitmap, coverX, coverY, drawW, drawH);
          renderer.drawRect(coverX - 1, coverY - 1, drawW + 2, drawH + 2, true);
          coverDrawn = true;
        }
      }
    }
  }

  if (!coverDrawn) {
    const int boxH = maxH;
    const int boxW = std::min(maxW, boxH * 2 / 3);  // book-shaped
    drawCoverPlaceholder(renderer, slotX + (slotW - boxW) / 2, slotY + (slotH - boxH) / 2, boxW, boxH);
  }
}

// Cipher selection: double square frame (2px outer, 1px inner, clear gap).
void drawSelectionFrame(const GfxRenderer& renderer, int x, int y, int w, int h) {
  renderer.drawRect(x, y, w, h, 2, true);
  renderer.drawRect(x + kHomeFrameGap, y + kHomeFrameGap, w - 2 * kHomeFrameGap, h - 2 * kHomeFrameGap, 1, true);
}

// Empty state: square-bordered panel with an inverted title band, echoing the
// header's black band + glyph motif.
void drawEmptyRecentsPanel(const GfxRenderer& renderer, Rect rect) {
  const int pad = CipherMetrics::values.contentSidePadding;
  const int panelX = rect.x + pad;
  const int panelW = rect.width - 2 * pad;
  const int panelH = rect.height * 2 / 5;
  const int panelY = rect.y + (rect.height - panelH) / 2;

  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int bandH = titleLineH + 16;
  renderer.drawRect(panelX, panelY, panelW, panelH, 2, true);
  renderer.fillRect(panelX, panelY, panelW, bandH, true);

  const int glyphX = panelX + pad;
  drawCipherGlyph(renderer, glyphX, panelY + (bandH - kGlyphHeight) / 2);
  const int textX = glyphX + kGlyphWidth + kHeaderTextGap;
  const auto title =
      renderer.truncatedText(UI_12_FONT_ID, tr(STR_NO_OPEN_BOOK), panelX + panelW - pad - textX, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, panelY + (bandH - titleLineH) / 2, title.c_str(), false, EpdFontFamily::BOLD);

  const int bodyLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const auto body = renderer.truncatedText(UI_10_FONT_ID, tr(STR_START_READING), panelW - 2 * pad);
  const int bodyW = renderer.getTextWidth(UI_10_FONT_ID, body.c_str());
  renderer.drawText(UI_10_FONT_ID, panelX + (panelW - bodyW) / 2, panelY + bandH + (panelH - bandH - bodyLineH) / 2,
                    body.c_str(), true);
}
}  // namespace

void CipherTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  const auto& metrics = CipherMetrics::values;

  // Solid black brand band; repainting it fully also clears the previous
  // battery draw, so no explicit clear rect is needed.
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);

  // White notch at the right end of the band holds the standard black-on-white
  // battery cluster; a 45-degree chamfer between band and notch echoes the
  // octagonal frame motif.
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - metrics.batteryWidth;
  int notchX = batteryX - 8;
  if (showBatteryPercentage) {
    notchX -= renderer.getTextWidth(SMALL_FONT_ID, "100%") + batteryPercentSpacing;
  }
  renderer.fillRect(notchX, rect.y, rect.x + rect.width - notchX, rect.height, false);
  const int chamfer = rect.height;
  const int xPoints[3] = {notchX - chamfer, notchX, notchX};
  const int yPoints[3] = {rect.y, rect.y, rect.y + rect.height - 1};
  renderer.fillPolygon(xPoints, yPoints, 3, false);

  // drawBatteryRight places the icon at y+6 (icon height = batteryHeight), so
  // offset by -6 to center the icon within the band.
  const int batteryY = rect.y + (rect.height - metrics.batteryHeight) / 2 - 6;
  drawBatteryRight(renderer, Rect{batteryX, batteryY, metrics.batteryWidth, metrics.batteryHeight},
                   showBatteryPercentage);

  const int glyphX = rect.x + metrics.contentSidePadding;
  drawCipherGlyph(renderer, glyphX, rect.y + (rect.height - kGlyphHeight) / 2);

  const int textLeft = glyphX + kGlyphWidth + kHeaderTextGap;
  const int textRight = notchX - chamfer - kHeaderTextGap;

  int subtitleWidth = 0;
  std::string truncatedSubtitle;
  if (subtitle) {
    truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, kMaxHeaderSubtitleWidth, EpdFontFamily::REGULAR);
    subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
  }

  if (title) {
    const int maxTitleWidth = textRight - textLeft - (subtitleWidth > 0 ? subtitleWidth + kMinValueGap : 0);
    if (maxTitleWidth > 0) {
      auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, textLeft, rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                        truncatedTitle.c_str(), false, EpdFontFamily::BOLD);
    }
  }

  if (subtitleWidth > 0) {
    renderer.drawText(SMALL_FONT_ID, textRight - subtitleWidth,
                      rect.y + (rect.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2, truncatedSubtitle.c_str(),
                      false);
  }
}

void CipherTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                                const char* rightLabel) const {
  BaseTheme::drawSubHeader(renderer, rect, label, rightLabel);

  // Thin rule with a short heavy segment at the left: the brand's gradient
  // rule rendered as weight contrast.
  const int sidePadding = CipherMetrics::values.contentSidePadding;
  const int ruleY = rect.y + rect.height - 3;
  renderer.drawLine(rect.x + sidePadding, ruleY, rect.x + rect.width - sidePadding - 1, ruleY, true);
  renderer.fillRect(rect.x + sidePadding, ruleY - 1, kSubHeaderAccentWidth, 3, true);
}

int CipherTheme::getListPageItems(int contentHeight, bool hasSubtitle) const {
  const int rowHeight =
      hasSubtitle ? CipherMetrics::values.listWithSubtitleRowHeight : CipherMetrics::values.listRowHeight;
  return contentHeight / rowHeight;
}

void CipherTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                           const std::function<std::string(int index)>& rowTitle,
                           const std::function<std::string(int index)>& rowSubtitle,
                           const std::function<UIIcon(int index)>& rowIcon,
                           const std::function<std::string(int index)>& rowValue, bool highlightValue,
                           const std::function<bool(int index)>& rowDimmed) const {
  (void)rowIcon;
  (void)highlightValue;
  const auto& metrics = CipherMetrics::values;
  const int rowHeight = (rowSubtitle != nullptr) ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  const int pageItems = std::max(1, rect.height / rowHeight);

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    // Angular page indicators: solid triangles at the right edge.
    constexpr int indicatorWidth = 20;
    constexpr int arrowSize = 6;
    constexpr int margin = 15;

    const int centerX = rect.x + rect.width - indicatorWidth / 2 - margin;
    const int indicatorTop = rect.y;
    const int indicatorBottom = rect.y + rect.height - arrowSize;

    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + i * 2;
      const int startX = centerX - i;
      renderer.drawLine(startX, indicatorTop + i, startX + lineWidth - 1, indicatorTop + i);
    }

    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
      const int startX = centerX - (arrowSize - 1 - i);
      renderer.drawLine(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                        indicatorBottom - arrowSize + 1 + i);
    }
  }

  // Selection is full inversion: solid black row, white text.
  const int contentWidth = rect.width - 5;
  if (selectedIndex >= 0) {
    renderer.fillRect(rect.x, rect.y + selectedIndex % pageItems * rowHeight, rect.width, rowHeight);
  }

  const int titleLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int subtitleLineHeight = renderer.getLineHeight(SMALL_FONT_ID);

  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    const bool isSelected = i == selectedIndex;

    int rowTextWidth = contentWidth - metrics.contentSidePadding * 2;
    std::string valueText;
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      if (!valueText.empty()) {
        const int maxValW = std::max(0, rowTextWidth - 40 - kMinValueGap);
        valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxValW);
        rowTextWidth -= renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + kMinValueGap;
      }
    }

    const int titleY = (rowSubtitle != nullptr)
                           ? itemY + (rowHeight - titleLineHeight - kSubtitleLineGap - subtitleLineHeight) / 2
                           : itemY + (rowHeight - titleLineHeight) / 2;

    auto item = renderer.truncatedText(UI_10_FONT_ID, rowTitle(i).c_str(), rowTextWidth);
    renderer.drawText(UI_10_FONT_ID, rect.x + metrics.contentSidePadding, titleY, item.c_str(), !isSelected);

    // Checkerboard dither for dimmed rows (existing dither gray).
    if (rowDimmed && rowDimmed(i) && !isSelected) {
      const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, item.c_str());
      const int tx = rect.x + metrics.contentSidePadding;
      for (int py = titleY; py < titleY + titleLineHeight; py++)
        for (int px = tx; px < tx + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowSubtitle != nullptr) {
      const std::string subtitleText = rowSubtitle(i);
      if (!subtitleText.empty()) {
        auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, rect.x + metrics.contentSidePadding,
                          titleY + titleLineHeight + kSubtitleLineGap, subtitle.c_str(), !isSelected);
      }
    }

    if (!valueText.empty()) {
      const int valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - metrics.contentSidePadding - valueTextWidth, titleY,
                        valueText.c_str(), !isSelected);
    }
  }
}

void CipherTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                  const char* btn4) const {
  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = CipherMetrics::values.buttonHintsHeight;
  constexpr int buttonY = CipherMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;
  // X3 has wider screen in portrait (528 vs 480), use more spacing
  constexpr int x4ButtonPositions[] = {25, 130, 245, 350};
  constexpr int x3ButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = gpio.deviceIsX3() ? x3ButtonPositions : x4ButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      // Inverted chip: solid black, white label, square corners.
      renderer.fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, true);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i], false);
    }
  }

  renderer.setOrientation(origOrientation);
}

void CipherTheme::drawOptionPopup(const GfxRenderer& renderer, const char* title,
                                  const std::vector<std::string>& options, int selectedIndex) const {
  const auto& metrics = CipherMetrics::values;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int optionFontId = metrics.optionPopupUseSmallFont ? UI_10_FONT_ID : UI_12_FONT_ID;
  const EpdFontFamily::Style optionStyle =
      metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

  const int itemSpacing = metrics.optionPopupItemSpacing;
  const int innerPadding = metrics.optionPopupInnerPadding;
  const int selectionHPadding = metrics.optionPopupSelectionHPadding;
  const int selectionVPadding = metrics.optionPopupSelectionVPadding;

  const int optionLineHeight = renderer.getLineHeight(optionFontId);
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int rowHeight = optionLineHeight + selectionVPadding * 2;

  int maxTextWidth = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
  for (const auto& opt : options) {
    const int w = renderer.getTextWidth(optionFontId, opt.c_str(), optionStyle);
    if (w > maxTextWidth) maxTextWidth = w;
  }

  const int optionCount = static_cast<int>(options.size());
  const int listHeight = rowHeight * optionCount + itemSpacing * (optionCount - 1);
  const int dialogW = std::min((maxTextWidth + innerPadding * 2 + selectionHPadding * 2) * 12 / 10,
                               pageWidth - metrics.optionPopupDialogSideMargin * 2);
  const int bandHeight = titleLineHeight + innerPadding;
  const int dialogH = bandHeight + metrics.optionPopupTitleGap + listHeight + innerPadding;
  const int dialogX = (pageWidth - dialogW) / 2;
  const int dialogY = (pageHeight - dialogH) / 2;
  const int frameThickness = metrics.popupFrameThickness;

  // Square dialog: black border, white interior, inverted title band.
  renderer.fillRect(dialogX - frameThickness, dialogY - frameThickness, dialogW + frameThickness * 2,
                    dialogH + frameThickness * 2, true);
  renderer.fillRect(dialogX, dialogY, dialogW, dialogH, false);
  renderer.fillRect(dialogX, dialogY, dialogW, bandHeight, true);

  renderer.drawCenteredText(UI_12_FONT_ID, dialogY + (bandHeight - titleLineHeight) / 2, title, false,
                            EpdFontFamily::BOLD);

  const int listTop = dialogY + bandHeight + metrics.optionPopupTitleGap;
  const int itemRectX = dialogX + innerPadding;
  const int itemRectW = dialogW - innerPadding * 2;

  for (int i = 0; i < optionCount; i++) {
    const int itemY = listTop + i * (rowHeight + itemSpacing);
    const bool selected = (i == selectedIndex);
    const char* labelText = options[i].c_str();

    if (selected) {
      renderer.fillRect(itemRectX, itemY, itemRectW, rowHeight, true);
    }

    const int textW = renderer.getTextWidth(optionFontId, labelText, optionStyle);
    const int textX = itemRectX + (itemRectW - textW) / 2;
    const int textY = itemY + (rowHeight - optionLineHeight) / 2;
    renderer.drawText(optionFontId, textX, textY, labelText, !selected, optionStyle);
  }
}

void CipherTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                      const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                      bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    // Pure vector drawing, cheap to redraw each pass: no snapshot needed.
    drawEmptyRecentsPanel(renderer, rect);
    return;
  }

  const auto& metrics = CipherMetrics::values;
  const int innerX = rect.x + metrics.contentSidePadding;
  const int innerW = rect.width - 2 * metrics.contentSidePadding;
  const int bookCount = std::min(static_cast<int>(recentBooks.size()), metrics.homeRecentBooksCount);
  const bool hasSecondary = bookCount > 1;

  // Hero slot on the left, sized book-shaped (2:3) so the selection frame hugs
  // a typical cover; the right column holds the hero info block on top and up
  // to two compact secondary entries below it.
  const int heroSlotW = innerW * kHomeHeroSlotPercent / 100;
  const int heroSlotH = std::min(rect.height, (heroSlotW - 2 * kHomeSlotInset) * 3 / 2 + 2 * kHomeSlotInset);
  const int heroSlotY = rect.y + (rect.height - heroSlotH) / 2;
  const int colX = innerX + heroSlotW + kHomeColumnGap;
  const int colW = innerX + innerW - colX;

  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int bodyLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int labelBandH = smallLineH + 10;
  const int titleMaxLines = hasSecondary ? 2 : 3;

  const int infoTop = rect.y + kHomeSlotInset;
  const int infoH = hasSecondary ? labelBandH + kHomeTextGap + 2 * titleLineH + kHomeTextGap + bodyLineH
                                 : rect.height - 2 * kHomeSlotInset;
  const int secTop = infoTop + infoH + kHomeColumnGap;
  const int secRowH = (rect.y + rect.height - secTop) / 2;
  const int secCoverSlotW = (secRowH - 2 * kHomeSlotInset) * 2 / 3 + 2 * kHomeSlotInset;

  // Covers stream from SD only until a snapshot of them is stored; afterwards
  // the restored buffer supplies them and only text/selection is redrawn.
  if (!coverRendered || !bufferRestored) {
    drawCoverInSlot(renderer, recentBooks[0].coverBmpPath, innerX, heroSlotY, heroSlotW, heroSlotH);
    for (int i = 1; i < bookCount; i++) {
      drawCoverInSlot(renderer, recentBooks[i].coverBmpPath, colX, secTop + (i - 1) * secRowH, secCoverSlotW, secRowH);
    }
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // Text and selection are redrawn on every call over the restored snapshot;
  // any selectorIndex >= bookCount (a menu row) draws no selection here.

  const bool heroSelected = selectorIndex == 0;
  const auto titleLines =
      renderer.wrappedText(UI_12_FONT_ID, recentBooks[0].title.c_str(), colW, titleMaxLines, EpdFontFamily::BOLD);
  const auto author = renderer.truncatedText(UI_10_FONT_ID, recentBooks[0].author.c_str(), colW);
  const int titleBlockH = static_cast<int>(titleLines.size()) * titleLineH;
  const int authorBlockH = author.empty() ? 0 : kHomeTextGap + bodyLineH;
  const int blockH = labelBandH + kHomeTextGap + titleBlockH + authorBlockH;
  int textY = infoTop + std::max(0, (infoH - blockH) / 2);

  const auto continueLabel =
      renderer.truncatedText(SMALL_FONT_ID, tr(STR_CONTINUE_READING), colW - 2 * kHomeLabelPadX);
  if (heroSelected) {
    // Inverted label band carries the selection cue into the info column.
    renderer.fillRect(colX, textY, colW, labelBandH, true);
    renderer.drawText(SMALL_FONT_ID, colX + kHomeLabelPadX, textY + (labelBandH - smallLineH) / 2,
                      continueLabel.c_str(), false);
  } else {
    renderer.drawText(SMALL_FONT_ID, colX + kHomeLabelPadX, textY + (labelBandH - smallLineH) / 2,
                      continueLabel.c_str(), true);
    // Sub-header motif: short heavy accent segment under the label.
    renderer.fillRect(colX, textY + labelBandH - 3, kSubHeaderAccentWidth, 3, true);
  }
  textY += labelBandH + kHomeTextGap;

  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, colX, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += titleLineH;
  }
  if (!author.empty()) {
    textY += kHomeTextGap;
    renderer.drawText(UI_10_FONT_ID, colX, textY, author.c_str(), true);
  }

  if (heroSelected) {
    drawSelectionFrame(renderer, innerX, heroSlotY, heroSlotW, heroSlotH);
  }

  for (int i = 1; i < bookCount; i++) {
    const int rowY = secTop + (i - 1) * secRowH;
    const bool rowSelected = selectorIndex == i;
    const int rowTextX = colX + secCoverSlotW + kHomeTextGap;
    const int rowTextW = colX + colW - rowTextX - kHomeSlotInset;

    const auto rowTitle = renderer.wrappedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), rowTextW, 2);
    const int rowTitleH = static_cast<int>(rowTitle.size()) * smallLineH;
    int lineY = rowY + (secRowH - rowTitleH) / 2;

    if (rowSelected) {
      renderer.fillRect(rowTextX - kHomeLabelPadX, lineY - kHomeLabelPadX, rowTextW + 2 * kHomeLabelPadX,
                        rowTitleH + 2 * kHomeLabelPadX, true);
      drawSelectionFrame(renderer, colX, rowY, colW, secRowH);
    }
    for (const auto& line : rowTitle) {
      renderer.drawText(SMALL_FONT_ID, rowTextX, lineY, line.c_str(), !rowSelected);
      lineY += smallLineH;
    }
  }
}
