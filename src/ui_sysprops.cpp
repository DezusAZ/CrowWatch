// SquachWatch-CYD — SYSTEM PROPERTIES. See include/ui_sysprops.h.
#include "ui_sysprops.h"
#include "theme.h"
#include "settings.h"
#include "ota_core.h"
#include "detection.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

namespace {

// The one colour a Win95 window needs that the board's palette has no name
// for: the title bar's navy. Fixed like the silver in theme.h, and for the
// same reason -- the point of a system window is that it was borrowed from
// somewhere else.
const uint16_t NAVY = 0x0010;

const int TITLE_H = 15;
const int TAB_H   = 15;
const int BTN_H   = 20;
const int SLOP    = 5;        // a fingertip is wider than a tab
const int ROW_H   = 12;

enum Tab : uint8_t { TAB_UPDATE = 0, TAB_NOTES = 1, TAB_BOARD = 2, TAB_N = 3 };
const char* const TAB_NAME[TAB_N] = { "UPDATE", "NOTES", "BOARD" };

uint8_t s_tab = TAB_UPDATE;

struct Geom {
    int x, y, w, h;          // the window
    int tabY, tabW;          // the strip
    int px, py, pw, ph;      // the sunken panel
    int btnY;                // the button row
};

Geom geom(TFT_eSPI& t) {
    Geom g;
    const int sw = t.width(), sh = t.height();
    g.w = sw - 16 < 280 ? sw - 16 : 280;
    g.h = sh - 24 < 196 ? sh - 24 : 196;
    g.x = (sw - g.w) / 2;
    g.y = (sh - g.h) / 2;
    g.tabY = g.y + 3 + TITLE_H + 2;
    g.tabW = (g.w - 8) / TAB_N;
    g.btnY = g.y + g.h - 6 - BTN_H;
    g.px = g.x + 4;
    g.py = g.tabY + TAB_H;
    g.pw = g.w - 8;
    g.ph = g.btnY - 5 - g.py;
    return g;
}

bool in(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx - SLOP && x < bx + bw + SLOP && y >= by - SLOP && y < by + bh + SLOP;
}

// A raised Win95 surface: the window itself and the tabs are both this.
void raised(TFT_eSPI& t, int x, int y, int w, int h) {
    t.fillRect(x + 2, y + 2, w - 4, h - 4, Theme::W95_FACE);
    t.drawFastHLine(x, y, w, Theme::W95_HILITE);
    t.drawFastVLine(x, y, h, Theme::W95_HILITE);
    t.drawFastHLine(x, y + h - 1, w, Theme::W95_DKSHADOW);
    t.drawFastVLine(x + w - 1, y, h, Theme::W95_DKSHADOW);
    t.drawFastHLine(x + 1, y + 1, w - 2, Theme::W95_LIGHT);
    t.drawFastVLine(x + 1, y + 1, h - 2, Theme::W95_LIGHT);
    t.drawFastHLine(x + 1, y + h - 2, w - 2, Theme::W95_SHADOW);
    t.drawFastVLine(x + w - 2, y + 1, h - 2, Theme::W95_SHADOW);
}

// ...and a sunken one: the panel the tabs open onto.
void sunken(TFT_eSPI& t, int x, int y, int w, int h) {
    t.fillRect(x + 2, y + 2, w - 4, h - 4, Theme::W95_FACE);
    t.drawFastHLine(x, y, w, Theme::W95_SHADOW);
    t.drawFastVLine(x, y, h, Theme::W95_SHADOW);
    t.drawFastHLine(x, y + h - 1, w, Theme::W95_HILITE);
    t.drawFastVLine(x + w - 1, y, h, Theme::W95_HILITE);
}

void label(TFT_eSPI& t, int x, int y, const char* s, uint16_t c = Theme::W95_DKSHADOW) {
    t.setTextColor(c, Theme::W95_FACE);
    t.setCursor(x, y);
    t.print(s);
}

// A label in the left column and its value beside it. The column holds the
// longest of them ("Heard from", "Other slot") with a space after it, and
// the value gets the rest -- which in portrait is twenty-four characters.
const int COL = 68;
void row(TFT_eSPI& t, const Geom& g, int y, const char* name, const char* value, bool strong = false) {
    label(t, g.px + 6, y, name, Theme::W95_SHADOW);
    label(t, g.px + 6 + COL, y, value, strong ? NAVY : Theme::W95_DKSHADOW);
}

void checkbox(TFT_eSPI& t, int x, int y, bool on, const char* text) {
    t.fillRect(x, y - 1, 9, 9, Theme::W95_HILITE);
    t.drawFastHLine(x, y - 1, 9, Theme::W95_SHADOW);
    t.drawFastVLine(x, y - 1, 9, Theme::W95_SHADOW);
    if (on) {
        // A tick, drawn rather than typed: the board's font has no check.
        t.drawLine(x + 2, y + 3, x + 4, y + 5, Theme::W95_DKSHADOW);
        t.drawLine(x + 4, y + 5, x + 7, y + 1, Theme::W95_DKSHADOW);
        t.drawLine(x + 2, y + 4, x + 4, y + 6, Theme::W95_DKSHADOW);
        t.drawLine(x + 4, y + 6, x + 7, y + 2, Theme::W95_DKSHADOW);
    }
    label(t, x + 14, y, text);
}

void uptimeText(char* out, size_t n, uint32_t now) {
    const uint32_t s = now / 1000;
    if (s < 3600) snprintf(out, n, "%lum %02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
    else          snprintf(out, n, "%luh %02lum", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60));
}

// ---- the three panels -------------------------------------------------------

void drawUpdateTab(TFT_eSPI& t, const Geom& g) {
    int y = g.py + 7;
    char buf[40];

    // As the build stamped it: a release is "v1.10.1", a bench build carries
    // the commit and "-dirty" after it, and both are the truth about what is
    // running. Cut to what the panel holds rather than dressed up.
    snprintf(buf, sizeof buf, "%.24s", OtaCore::runningVersion());
    row(t, g, y, "Running", buf);
    y += ROW_H;

    const char* name = OtaCore::releaseName();
    if (name[0]) snprintf(buf, sizeof buf, "v%s  %.16s", OtaCore::availableVersion(), name);
    else         snprintf(buf, sizeof buf, "v%s", OtaCore::availableVersion());
    row(t, g, y, "Available", buf, true);
    y += ROW_H;

    const char* from = OtaCore::availableFrom();
    if (from[0]) snprintf(buf, sizeof buf, "%.12s's board", from);
    else         snprintf(buf, sizeof buf, "squachwatch.com");
    row(t, g, y, "Heard from", buf);
    y += ROW_H + 6;

    // Thirty-three characters a line: what the portrait window holds.
    label(t, g.px + 6, y, "Downloads over WiFi, then the");
    y += ROW_H - 2;
    label(t, g.px + 6, y, "board restarts into it. What");
    y += ROW_H - 2;
    label(t, g.px + 6, y, "you have now is kept in the");
    y += ROW_H - 2;
    label(t, g.px + 6, y, "other slot, to go back to.");

    checkbox(t, g.px + 6, g.py + g.ph - 14, Settings::updateCheck(), "Look for updates every boot");
}

void drawNotesTab(TFT_eSPI& t, const Geom& g) {
    int y = g.py + 7;
    char buf[40];
    const char* name = OtaCore::releaseName();
    if (name[0]) snprintf(buf, sizeof buf, "v%s  \"%.16s\"", OtaCore::availableVersion(), name);
    else         snprintf(buf, sizeof buf, "v%s", OtaCore::availableVersion());
    label(t, g.px + 6, y, buf, NAVY);
    y += ROW_H + 3;

    const uint8_t n = OtaCore::newsCount();
    if (!n) {
        // Nothing came with it, and saying so is better than an empty box.
        // A squad member's hello carries a version and nothing else, and a
        // release made before the site started sending its lines has none.
        label(t, g.px + 6, y, OtaCore::availableFrom()[0] ? "A squad member had this one,"
                                                          : "The site sent no lines with");
        y += ROW_H - 2;
        label(t, g.px + 6, y, OtaCore::availableFrom()[0] ? "and a hello carries no notes."
                                                          : "this one: it predates them.");
        y += ROW_H + 2;
        label(t, g.px + 6, y, "What changed is on the site.", Theme::W95_SHADOW);
        return;
    }
    for (uint8_t i = 0; i < n && y < g.py + g.ph - 10; i++) {
        label(t, g.px + 6, y, "-", Theme::W95_SHADOW);
        label(t, g.px + 14, y, OtaCore::newsAt(i));
        y += ROW_H;
    }
}

void drawBoardTab(TFT_eSPI& t, const Geom& g) {
    int y = g.py + 7;
    char buf[40];

    row(t, g, y, "Build", OtaCore::buildName());
    y += ROW_H;

    snprintf(buf, sizeof buf, "%s  %.17s", OtaCore::runningSlot(), OtaCore::runningVersion());
    row(t, g, y, "This slot", buf);
    y += ROW_H;

    const char* other = OtaCore::otherVersion();
    snprintf(buf, sizeof buf, "%.23s", other && other[0] ? other : "nothing to go back to");
    row(t, g, y, "Other slot", buf);
    y += ROW_H;

    uptimeText(buf, sizeof buf, millis());
    row(t, g, y, "Up", buf);
    y += ROW_H;

    snprintf(buf, sizeof buf, "%lu KB free", (unsigned long)(ESP.getFreeHeap() / 1024));
    row(t, g, y, "Memory", buf);
    y += ROW_H;

    snprintf(buf, sizeof buf, "%lu KB in one piece",
             (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    row(t, g, y, "", buf);
}

}  // namespace

void uiSysPropsInit(TFT_eSPI& t) {
    s_tab = TAB_UPDATE;
    OtaCore::refreshOther();     // the BOARD tab reads the other slot
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiSysPropsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();

    // The room carries on behind it, dimmed: the window is in front of the
    // board, not instead of it.
    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 130);

    const Geom g = geom(t);
    t.setTextWrap(false);
    t.setTextSize(1);

    raised(t, g.x, g.y, g.w, g.h);

    // Title bar, with the close box at its right end.
    t.fillRect(g.x + 3, g.y + 3, g.w - 6, TITLE_H, NAVY);
    t.setTextColor(Theme::WHITE, NAVY);
    t.setCursor(g.x + 7, g.y + 3 + (TITLE_H - 8) / 2);
    t.print("System Properties");
    const int cx = g.x + g.w - 6 - 12;
    Theme::drawWin95Button(t, cx, g.y + 4, 12, TITLE_H - 2, "", false);
    t.drawLine(cx + 4, g.y + 8, cx + 8, g.y + 12, Theme::W95_DKSHADOW);
    t.drawLine(cx + 8, g.y + 8, cx + 4, g.y + 12, Theme::W95_DKSHADOW);

    // The tabs. The open one is a row taller and joins the panel below it,
    // which is the whole trick of a tab strip.
    for (uint8_t i = 0; i < TAB_N; i++) {
        const int tx = g.x + 4 + i * g.tabW;
        const bool on = (i == s_tab);
        raised(t, tx, on ? g.tabY - 2 : g.tabY, g.tabW, on ? TAB_H + 4 : TAB_H);
        t.setTextColor(on ? NAVY : Theme::W95_DKSHADOW, Theme::W95_FACE);
        t.setCursor(tx + (g.tabW - t.textWidth(TAB_NAME[i])) / 2, g.tabY + (on ? 2 : 4));
        t.print(TAB_NAME[i]);
    }

    sunken(t, g.px, g.py, g.pw, g.ph);
    // The open tab's bottom edge is the panel's top edge: paint over the
    // seam so the two read as one surface.
    t.fillRect(g.x + 5 + s_tab * g.tabW, g.py, g.tabW - 2, 2, Theme::W95_FACE);

    if      (s_tab == TAB_UPDATE) drawUpdateTab(t, g);
    else if (s_tab == TAB_NOTES)  drawNotesTab(t, g);
    else                          drawBoardTab(t, g);

    // The buttons. UPDATE NOW only where it belongs -- a button that acts on
    // the other tabs' content would be a button that means different things
    // in different places.
    const int right = g.x + g.w - 6;
    if (s_tab == TAB_UPDATE) {
        Theme::drawWin95Button(t, right - 56, g.btnY, 56, BTN_H, "LATER", false);
        Theme::drawWin95Button(t, right - 56 - 4 - 88, g.btnY, 88, BTN_H, "UPDATE NOW", false);
        // The default button, the one a keyboard would have focused.
        t.drawRect(right - 56 - 4 - 88 - 2, g.btnY - 2, 92, BTN_H + 4, Theme::W95_DKSHADOW);
    } else {
        Theme::drawWin95Button(t, right - 56, g.btnY, 56, BTN_H, "CLOSE", false);
    }
}

SysPropsHit uiSysPropsTouch(TFT_eSPI& t, int x, int y) {
    const Geom g = geom(t);

    // Outside the window: the same as LATER. A modal you cannot walk away
    // from is a modal that traps a board whose touch is badly calibrated.
    if (x < g.x || x >= g.x + g.w || y < g.y || y >= g.y + g.h) return SysPropsHit::CLOSE;

    const int cx = g.x + g.w - 6 - 12;
    if (in(x, y, cx, g.y + 4, 12, TITLE_H - 2)) return SysPropsHit::CLOSE;

    for (uint8_t i = 0; i < TAB_N; i++)
        if (in(x, y, g.x + 4 + i * g.tabW, g.tabY - 2, g.tabW, TAB_H + 4)) { s_tab = i; return SysPropsHit::NONE; }

    const int right = g.x + g.w - 6;
    if (s_tab == TAB_UPDATE) {
        if (in(x, y, right - 56, g.btnY, 56, BTN_H)) return SysPropsHit::CLOSE;
        if (in(x, y, right - 148, g.btnY, 88, BTN_H)) return SysPropsHit::UPDATE_NOW;
        // The checkbox, and its words: a label you can tap is the difference
        // between a setting people find and one they do not.
        if (in(x, y, g.px + 6, g.py + g.ph - 15, 180, 11)) { Settings::toggleUpdateCheck(); return SysPropsHit::NONE; }
    } else if (in(x, y, right - 56, g.btnY, 56, BTN_H)) {
        return SysPropsHit::CLOSE;
    }
    return SysPropsHit::NONE;
}
