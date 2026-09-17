// SquachWatch-CYD — the CROWD page. See include/ui_crowd.h.
#if SQUACH_MESH
#include "ui_crowd.h"
#include "theme.h"
#include "settings.h"
#include <Arduino.h>
#include <stdio.h>

namespace {

const int TOP_MARGIN = 16;

void geom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH) {
    top = TOP_MARGIN + Theme::LIST_HEADING_H;
    bodyBottom = screenH - Theme::PINNED_BACK_H - 2;
    t.setTextSize(2);
    // The same row height every other list on this board uses. Two rows,
    // so there is no scrolling and no reason to shave it.
    rowH = t.fontHeight() + 10;
}

void drawRow(TFT_eSPI& t, int w, int y, int hgt, const char* label,
             const char* value, uint16_t valueCol) {
    Theme::drawListRowPanel(t, w, y, hgt);
    t.setTextSize(2);
    t.setTextWrap(false);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);
    t.setTextColor(valueCol, Theme::BG);
    t.setCursor(w - 18 - t.textWidth(value), y + (hgt - t.fontHeight()) / 2);
    t.print(value);
}

void note(TFT_eSPI& t, int& y, int bodyBottom, const char* s) {
    t.setTextSize(1);
    if (y + t.fontHeight() > bodyBottom) return;
    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    t.setCursor(8, y);
    t.print(s);
    y += t.fontHeight() + 2;
}

}  // namespace

void uiCrowdInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiCrowdTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();
    int top, bodyBottom, rowH;
    geom(t, h, top, bodyBottom, rowH);

    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    Theme::drawActiveBackground(t, now, 0, bodyBottom, eng);
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> CROWD <<");
    Theme::drawListHeading(t, "CROWD", Theme::CYAN);

    const bool many = Settings::meshCrowd() > 1;
    drawRow(t, w, top, rowH, "HOW MANY", Settings::meshCrowdLabel(),
            many ? Theme::GREEN : Theme::W95_SHADOW);

    int y = top + rowH + 6;
    if (many) note(t, y, bodyBottom, "They roam, and shrink to fit.");
    else      note(t, y, bodyBottom, "ONE is the ordinary visit: one guest,");
    if (!many) note(t, y, bodyBottom, "both on the ground, with the set pieces.");
    y += 4;
    // The desk has its own, since it got a page of its own.
    note(t, y, bodyBottom, "The desk has its own: Settings, DESK MODE.");

    Theme::drawPinnedBack(t, "[ BACK ]");
}

CrowdRow uiCrowdHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, bodyBottom, rowH;
    geom(t, screenH, top, bodyBottom, rowH);
    for (uint8_t i = 0; i < (uint8_t)CrowdRow::COUNT; i++) {
        const int ry = top + i * rowH;
        if (ry + rowH > bodyBottom) break;
        if (y >= ry && y < ry + rowH) return (CrowdRow)i;
    }
    return CrowdRow::NONE;
}
#endif
