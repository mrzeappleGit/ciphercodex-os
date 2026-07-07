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
#include "components/themes/cipher/CipherEmblem.h"
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

constexpr int kHeaderTextGap = 12;  // Gap between glyph/chamfer and header text
constexpr int kSubHeaderAccentWidth = 56;
constexpr int kMaxHeaderSubtitleWidth = 200;
constexpr int kMinValueGap = 10;
constexpr int kSubtitleLineGap = 2;

void drawCipherGlyph(const GfxRenderer& renderer, int x, int y) {
  for (int i = 0; i < kGlyphBarCount; ++i) {
    renderer.fillRect(x, y + i * (kGlyphBarHeight + kGlyphBarGap), kGlyphBarWidths[i], kGlyphBarHeight, false);
  }
}

// Reading-state leading glyph for library rows: a hollow diamond (never opened)
// or a left-half-filled diamond (in progress), echoing the OS diamond identity.
// `black` follows the row's text colour so it inverts on the selected row.
constexpr int kBookGlyphRad = 6;
constexpr int kBookGlyphCol = 2 * kBookGlyphRad + 12;  // reserved leading column width

void drawReadingGlyph(const GfxRenderer& renderer, int cx, int cy, UIIcon state, bool black) {
  if (state == UIIcon::BookReading) {
    const int lx[3] = {cx, cx, cx - kBookGlyphRad};
    const int ly[3] = {cy - kBookGlyphRad, cy + kBookGlyphRad, cy};
    renderer.fillPolygon(lx, ly, 3, black);
  }
  CipherEmblem::strokeDiamond(renderer, cx, cy, kBookGlyphRad, 1, black);
}

// Home cover-grid layout (Kindle-style 3-column, up-to-2-row grid of recents)
constexpr int kHomeSlotInset = 10;  // Cover inset within its slot; keeps the selection frame off the art
constexpr int kHomeTextGap = 8;
constexpr int kHomeFrameGap = 4;  // Outer 2px frame to inner 1px frame offset
constexpr int kHomeLabelPadX = 6;
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

  // Daemon-OS wordmark diamond (the "◆"): a small white token on the black
  // band, tying every header to the boot/sleep emblem. Occupies a kGlyphWidth
  // slot so the title offset is unchanged.
  const int glyphX = rect.x + metrics.contentSidePadding;
  CipherEmblem::drawGlyph(renderer, glyphX + kGlyphWidth / 2, rect.y + rect.height / 2, kGlyphWidth / 2 - 2, false);

  const int textLeft = glyphX + kGlyphWidth + kHeaderTextGap;
  const int textRight = notchX - chamfer - kHeaderTextGap;

  int subtitleWidth = 0;
  std::string truncatedSubtitle;
  if (subtitle) {
    truncatedSubtitle =
        renderer.truncatedText(SMALL_FONT_ID, subtitle, kMaxHeaderSubtitleWidth, EpdFontFamily::REGULAR);
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

  // Cache the visible rows' icons (rowIcon may stat the SD card) and reserve a
  // leading glyph column when any visible row carries a reading-state glyph, so
  // book rows and plain rows stay aligned. Lists that pass no book-state icon
  // leave glyphCol at 0, so their layout is unchanged.
  constexpr int kMaxVisibleRows = 24;
  UIIcon visibleIcons[kMaxVisibleRows];
  bool hasBookGlyphs = false;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int slot = i - pageStartIndex;
    const UIIcon ic = (rowIcon && slot < kMaxVisibleRows) ? rowIcon(i) : UIIcon::None;
    if (slot < kMaxVisibleRows) visibleIcons[slot] = ic;
    if (ic == UIIcon::BookNew || ic == UIIcon::BookReading) hasBookGlyphs = true;
  }
  const int glyphCol = hasBookGlyphs ? kBookGlyphCol : 0;

  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    const bool isSelected = i == selectedIndex;
    const int textX = rect.x + metrics.contentSidePadding + glyphCol;

    const int slot = i - pageStartIndex;
    const UIIcon rowGlyph = slot < kMaxVisibleRows ? visibleIcons[slot] : UIIcon::None;
    if (glyphCol > 0 && (rowGlyph == UIIcon::BookNew || rowGlyph == UIIcon::BookReading)) {
      drawReadingGlyph(renderer, rect.x + metrics.contentSidePadding + kBookGlyphRad, itemY + rowHeight / 2, rowGlyph,
                       !isSelected);
    }

    int rowTextWidth = contentWidth - metrics.contentSidePadding * 2 - glyphCol;
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
    renderer.drawText(UI_10_FONT_ID, textX, titleY, item.c_str(), !isSelected);

    // Checkerboard dither for dimmed rows (existing dither gray).
    if (rowDimmed && rowDimmed(i) && !isSelected) {
      const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, item.c_str());
      for (int py = titleY; py < titleY + titleLineHeight; py++)
        for (int px = textX; px < textX + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowSubtitle != nullptr) {
      const std::string subtitleText = rowSubtitle(i);
      if (!subtitleText.empty()) {
        auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, textX, titleY + titleLineHeight + kSubtitleLineGap, subtitle.c_str(),
                          !isSelected);
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

void CipherTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                             bool selected) const {
  // Active category as a solid black chip (echoes the header band and button
  // hints); when the tab row itself has focus, the chip gains the double-frame
  // selection motif used on home covers.
  constexpr int kChipPadX = 8;
  constexpr int kChipPadY = 4;
  constexpr int kFocusGap = 3;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int chipH = lineHeight + 2 * kChipPadY;
  const int chipY = rect.y + (rect.height - chipH) / 2;

  int currentX = rect.x + CipherMetrics::values.contentSidePadding;
  for (const auto& tab : tabs) {
    const auto style = tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, tab.label, style);
    const int chipW = textWidth + 2 * kChipPadX;

    if (tab.selected) {
      renderer.fillRect(currentX, chipY, chipW, chipH, true);
      if (selected) {
        renderer.drawRect(currentX - kFocusGap, chipY - kFocusGap, chipW + 2 * kFocusGap, chipH + 2 * kFocusGap, 1,
                          true);
      }
    }
    renderer.drawText(UI_12_FONT_ID, currentX + kChipPadX, chipY + kChipPadY, tab.label, !tab.selected, style);
    currentX += chipW + CipherMetrics::values.tabSpacing;
  }
}

void CipherTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                 const std::function<std::string(int index)>& buttonLabel,
                                 const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rowIcon;  // Cipher menu rows lead with the brand diamond instead of per-item icons
  const auto& metrics = CipherMetrics::values;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int glyphCx = rect.x + metrics.contentSidePadding + kBookGlyphRad;
  const int textX = rect.x + metrics.contentSidePadding + kBookGlyphCol;

  for (int i = 0; i < buttonCount; ++i) {
    const int rowY = rect.y + metrics.verticalSpacing + i * (metrics.menuRowHeight + metrics.menuSpacing);
    const bool isSelected = selectedIndex == i;
    const int rowCy = rowY + metrics.menuRowHeight / 2;

    // Selection is full-row inversion (same language as drawList); unselected
    // rows are quiet left-aligned entries with a hollow diamond bullet.
    if (isSelected) {
      renderer.fillRect(rect.x + metrics.contentSidePadding, rowY, rect.width - 2 * metrics.contentSidePadding,
                        metrics.menuRowHeight, true);
      CipherEmblem::drawGlyph(renderer, glyphCx, rowCy, kBookGlyphRad - 1, false);
    } else {
      CipherEmblem::strokeDiamond(renderer, glyphCx, rowCy, kBookGlyphRad - 1, 1, true);
    }

    const std::string labelStr = buttonLabel(i);
    const auto label =
        renderer.truncatedText(UI_10_FONT_ID, labelStr.c_str(), rect.width - textX - metrics.contentSidePadding);
    renderer.drawText(UI_10_FONT_ID, textX, rowY + (metrics.menuRowHeight - lineHeight) / 2, label.c_str(),
                      !isSelected);
  }
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

  // Kindle-style cover grid: 3 columns, up to 2 rows, covers dominant with a
  // wrapped title beneath each. Reading order (left-to-right, top-to-bottom)
  // matches the linear NavNext/NavPrevious selector, so navigation is unchanged
  // from the single-hero layout: the cursor just steps cover to cover, then on
  // into the menu rows below (selectorIndex >= bookCount frames nothing here).
  constexpr int kGridCols = 3;
  const int gridRows = (bookCount + kGridCols - 1) / kGridCols;  // 1 when <=3 recents, else 2
  const int tileW = innerW / kGridCols;
  const int rowH = rect.height / std::max(1, gridRows);

  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int titleAreaH = 2 * smallLineH + kHomeTextGap;  // room for up to two wrapped title lines
  const int coverSlotH = std::max(0, rowH - titleAreaH);

  // Covers stream from SD only on the first render; the snapshot then supplies
  // them so cursor moves redraw just the titles and the selection frame.
  if (!coverRendered || !bufferRestored) {
    for (int i = 0; i < bookCount; i++) {
      const int tileX = innerX + tileW * (i % kGridCols);
      const int tileY = rect.y + rowH * (i / kGridCols);
      drawCoverInSlot(renderer, recentBooks[i].coverBmpPath, tileX, tileY, tileW, coverSlotH);
    }
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // Titles + selection frame over the restored covers, every pass.
  for (int i = 0; i < bookCount; i++) {
    const int tileX = innerX + tileW * (i % kGridCols);
    const int tileY = rect.y + rowH * (i / kGridCols);

    const int titleMaxW = tileW - 2 * kHomeLabelPadX;
    const auto titleLines = renderer.wrappedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), titleMaxW, 2);
    int textY = tileY + coverSlotH + kHomeTextGap;
    for (const auto& line : titleLines) {
      const int lineW = renderer.getTextWidth(SMALL_FONT_ID, line.c_str());
      renderer.drawText(SMALL_FONT_ID, tileX + (tileW - lineW) / 2, textY, line.c_str(), true);
      textY += smallLineH;
    }

    if (selectorIndex == i) {
      drawSelectionFrame(renderer, tileX + kHomeFrameGap, tileY, tileW - 2 * kHomeFrameGap, coverSlotH);
    }
  }
}
