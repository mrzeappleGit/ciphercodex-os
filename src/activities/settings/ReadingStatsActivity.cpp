#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReadingStats.h"
#include "util/StatsFormat.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Day-of-month labels for the bar strip: locale-neutral, no i18n keys needed.
std::string dayOfMonthLabel(int daysBack) {
  time_t t = time(nullptr) - static_cast<time_t>(daysBack) * 86400;
  struct tm local{};
  localtime_r(&t, &local);
  char buf[4];
  snprintf(buf, sizeof(buf), "%02d", local.tm_mday);
  return buf;
}

}  // namespace

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int marginX = 20;
  const bool clockValid = READING_STATS.clockValid();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const int rowHeight = 42;

  const auto drawRow = [&](StrId label, const std::string& value) {
    renderer.drawText(UI_12_FONT_ID, marginX, y, I18N.get(label));
    const int valueWidth = renderer.getTextWidth(UI_12_FONT_ID, value.c_str());
    renderer.drawText(UI_12_FONT_ID, pageWidth - marginX - valueWidth, y, value.c_str());
    y += rowHeight;
  };

  if (clockValid) {
    drawRow(StrId::STR_STATS_TODAY, StatsFormat::duration(READING_STATS.todaySeconds()));
    drawRow(StrId::STR_STATS_THIS_WEEK, StatsFormat::duration(READING_STATS.weekSeconds()));
  }
  drawRow(StrId::STR_STATS_ALL_TIME, StatsFormat::duration(READING_STATS.allTimeSeconds()));
  if (clockValid) {
    char streak[16];
    snprintf(streak, sizeof(streak), "%u", READING_STATS.currentStreakDays());
    drawRow(StrId::STR_STATS_STREAK, std::string(streak) + " " + tr(STR_STATS_DAYS));
  }
  {
    char pages[16];
    snprintf(pages, sizeof(pages), "%u", READING_STATS.allTimePagesTurned());
    drawRow(StrId::STR_STATS_PAGES_TURNED, pages);
  }
  if (!APP_STATE.openEpubPath.empty()) {
    drawRow(StrId::STR_STATS_THIS_BOOK, StatsFormat::duration(READING_STATS.bookSeconds(APP_STATE.openEpubPath)));
  }

  if (!clockValid) {
    y += metrics.verticalSpacing;
    renderer.drawText(SMALL_FONT_ID, marginX, y, tr(STR_STATS_NEED_CLOCK));
  } else {
    // Seven-day bar strip: solid black bars, 1px baseline, day-of-month labels.
    uint32_t week[7];
    READING_STATS.lastSevenDaysSeconds(week);
    const uint32_t maxDay = std::max<uint32_t>(1, *std::max_element(week, week + 7));

    y += metrics.verticalSpacing * 2;
    renderer.drawText(SMALL_FONT_ID, marginX, y, tr(STR_STATS_LAST_7_DAYS));
    y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;

    const int chartHeight = 110;
    const int labelBand = renderer.getLineHeight(SMALL_FONT_ID) + 4;
    const int chartWidth = pageWidth - marginX * 2;
    const int slot = chartWidth / 7;
    const int barWidth = slot - 6;
    const int baseline = y + chartHeight;

    for (int i = 0; i < 7; i++) {
      const int x = marginX + i * slot + 3;
      const int h = static_cast<int>(static_cast<uint64_t>(week[i]) * chartHeight / maxDay);
      if (h > 0) {
        renderer.fillRect(x, baseline - h, barWidth, h, true);
      }
      // Selective label: the peak day's value above its bar.
      if (week[i] == maxDay && week[i] > 0) {
        const std::string label = StatsFormat::duration(week[i]);
        const int lw = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
        int lx = x + barWidth / 2 - lw / 2;
        lx = std::max(marginX, std::min(lx, pageWidth - marginX - lw));
        renderer.drawText(SMALL_FONT_ID, lx, baseline - h - labelBand, label.c_str());
      }
      const std::string dayLabel = dayOfMonthLabel(6 - i);
      const int dw = renderer.getTextWidth(SMALL_FONT_ID, dayLabel.c_str());
      renderer.drawText(SMALL_FONT_ID, x + barWidth / 2 - dw / 2, baseline + 4, dayLabel.c_str());
    }
    renderer.drawLine(marginX, baseline, pageWidth - marginX, baseline, true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
