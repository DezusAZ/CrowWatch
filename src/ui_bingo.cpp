// SquachWatch-CYD — the bingo card screen. See ui_bingo.h.
#include "ui_bingo.h"
#include "bingo.h"
#include "theme.h"
#include "detection_info.h"
#include "clock.h"
#include <Arduino.h>

namespace {

const int TOP = 16;
bool    s_stats    = false;
int8_t  s_openCell = -1;      // the square whose paragraph is up, or -1

// The card fills the space between the title bar and the button bar. Four
// squares across either way round: in portrait they are narrow and tall, in
// landscape wide and short, and the icon is sized from whichever is smaller.
void geom(TFT_eSPI& t, int w, int h, int& gx, int& gy, int& cw, int& ch) {
    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int top    = TOP + 14;
    const int bottom = bar.y - 6;
    const int pad    = 6, gap = 4;
    cw = (w - 2 * pad - 3 * gap) / 4;
    ch = (bottom - top - 3 * gap) / 4;
    gx = (w - (4 * cw + 3 * gap)) / 2;
    gy = top;
    (void)t;
}

// The counters' short names, not detectionTypeName()'s: "SAMSUNG_TAG" and
// "EVIL TWIN" do not fit a quarter of a 240px screen, and the squares are
// the same four-letter shorthand the main screen's counters use.
const char* shortName(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "FLOCK";
        case DetectionType::AXON:        return "AXON";
        case DetectionType::META:        return "GLASS";
        case DetectionType::SKIMMER:     return "SKIM";
        case DetectionType::RAVEN:       return "RAV";
        case DetectionType::AIRTAG:      return "TRACKER";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAM";
        case DetectionType::SAMSUNG_TAG: return "STAG";
        case DetectionType::GOOGLE_TAG:  return "GTAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "TWIN";
        case DetectionType::IBEACON:     return "BEACON";
        case DetectionType::HACKER:      return "HACK";
        default:                         return "?";
    }
}

const char* dayName(uint8_t d) {
    static const char* NAMES[8] = { "", "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    return d < 8 ? NAMES[d] : "";
}

void drawCard(TFT_eSPI& t, int w, int h) {
    int gx, gy, cw, ch;
    geom(t, w, h, gx, gy, cw, ch);

    // The count, where a card screen wants it: beside the heading rather
    // than under the grid, which is where the squares are.
    char head[32];
    const uint8_t lines = Bingo::linesCalled();
    snprintf(head, sizeof head, "%u OF 16   %u LINE%s",
             (unsigned)Bingo::markedCount(), (unsigned)lines, lines == 1 ? "" : "S");
    Theme::drawListHeading(t, head, Bingo::markedCount() == Bingo::CELLS ? Theme::AMBER : Theme::CYAN);

    t.setTextSize(1);
    for (uint8_t i = 0; i < Bingo::CELLS; i++) {
        const int x = gx + (i % 4) * (cw + 4);
        const int y = gy + (i / 4) * (ch + 4);
        const DetectionType type = Bingo::typeAt(i);
        const bool got  = Bingo::marked(i);
        const bool line = got && Bingo::inCalledLine(i);

        const uint16_t edge = line ? Theme::AMBER : (got ? Theme::GREEN : Theme::PURPLE);
        t.fillRect(x, y, cw, ch, Theme::BG);
        t.drawRect(x, y, cw, ch, edge);

        // The type's own icon, the one the alert and the log draw. An
        // uncaught square wears it faded rather than empty: the card is a
        // list of things to go and find, so it has to show what they look
        // like before you have found one.
        const int s = (cw < ch ? cw : ch) / 2 - 5;
        if (s >= 4) {
            const int icy = y + ch / 2 - 4;
            Theme::drawTypeIcon(t, type, x + cw / 2, icy, s);
            // Not yet caught: the icon behind a scanline veil. The icons draw
            // from their own palette, so dimming the theme's does nothing to
            // them -- every other row of the square goes back to background
            // instead -- every third row, which reads as faded while leaving the
            // shape of the thing you are looking for.
            if (!got)
                for (int ly = y + 2; ly < y + ch - 2; ly += 3)
                    t.drawFastHLine(x + 1, ly, cw - 2, Theme::BG);
        }

        const char* name = shortName(type);
        t.setTextColor(line ? Theme::AMBER : (got ? Theme::GREEN : Theme::W95_LIGHT), Theme::BG);
        int tw = t.textWidth(name);
        if (tw > cw - 4) tw = cw - 4;
        t.setCursor(x + (cw - tw) / 2, y + ch - t.fontHeight() - 2);
        t.print(name);

        if (got) {
            const char* d = dayName(Bingo::markDay(i));
            t.setTextColor(Theme::W95_SHADOW, Theme::BG);
            t.setCursor(x + cw - t.textWidth(d) - 2, y + 2);
            t.print(d);
        }
    }
}

void drawStats(TFT_eSPI& t, int w, int h) {
    Theme::drawListHeading(t, "BINGO STATS", Theme::VAPOR_PINK);
    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    t.setTextSize(1);
    int y = TOP + 18;
    const int lineH = t.fontHeight() + 6;

    struct Row { const char* k; char v[24]; } rows[6];
    uint8_t n = 0;
    snprintf(rows[n].v, sizeof rows[n].v, "%u", (unsigned)Bingo::cardsFilled()); rows[n++].k = "CARDS FILLED";
    snprintf(rows[n].v, sizeof rows[n].v, "%u of 16", (unsigned)Bingo::bestFilled()); rows[n++].k = "BEST CARD";
    snprintf(rows[n].v, sizeof rows[n].v, "%u weeks", (unsigned)Bingo::streak()); rows[n++].k = "STREAK";
    snprintf(rows[n].v, sizeof rows[n].v, "%u weeks", (unsigned)Bingo::bestStreak()); rows[n++].k = "BEST STREAK";
    snprintf(rows[n].v, sizeof rows[n].v, "%u", (unsigned)Bingo::linesEver()); rows[n++].k = "LINES CALLED";
    snprintf(rows[n].v, sizeof rows[n].v, "%u of 16", (unsigned)Bingo::markedCount()); rows[n++].k = "THIS CARD";

    for (uint8_t i = 0; i < n && y + lineH < bar.y - 20; i++) {
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(10, y);
        t.print(rows[i].k);
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(w / 2 + 10, y);
        t.print(rows[i].v);
        y += lineH;
    }

    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    t.setCursor(10, y + 4);
    t.print(Bingo::weekNumber() ? "A fresh card every week." : "No clock yet: this card stands.");
}

}  // namespace

void uiBingoInit(TFT_eSPI& t) {
    s_stats    = false;
    s_openCell = -1;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiBingoTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();
    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);

    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> BINGO <<");
    if (s_stats) drawStats(t, w, h);
    else         drawCard(t, w, h);

    Theme::drawButton(t, bar.x[0], bar.y, bar.w[0], bar.h, s_stats ? "[ CARD ]" : "[ STATS ]", false);
    Theme::drawButton(t, bar.x[1], bar.y, bar.w[1], bar.h, "[ NEW ]", false);
    Theme::drawButton(t, bar.x[2], bar.y, bar.w[2], bar.h, "[ BACK ]", false);

    // Over everything else, and last: the square's own paragraph.
    if (s_openCell >= 0) {
        const DetectionType type = Bingo::typeAt((uint8_t)s_openCell);
        Theme::drawInfoPanel(t, w, h, now, detectionTypeName(type), DetectionInfo::explain(type));
        // The panel covers the card, so the veil above it does not matter here.
    }
}

BingoTap uiBingoHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    if (s_openCell >= 0) {
        // Anywhere outside it closes it, the same as every other panel.
        s_openCell = -1;
        (void)Theme::infoPanelHitDismiss(x, y, screenW, screenH);
        return BingoTap::HANDLED;
    }

    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(screenW, screenH);
    if (y >= bar.y && y < bar.y + bar.h) {
        if (x >= bar.x[2]) return BingoTap::BACK;
        if (x >= bar.x[1]) { Bingo::newCard(); return BingoTap::HANDLED; }
        s_stats = !s_stats;
        return BingoTap::HANDLED;
    }
    if (s_stats) return BingoTap::NONE;

    int gx, gy, cw, ch;
    geom(t, screenW, screenH, gx, gy, cw, ch);
    for (uint8_t i = 0; i < Bingo::CELLS; i++) {
        const int cx = gx + (i % 4) * (cw + 4);
        const int cy = gy + (i / 4) * (ch + 4);
        if (x >= cx && x < cx + cw && y >= cy && y < cy + ch) {
            s_openCell = (int8_t)i;
            return BingoTap::HANDLED;
        }
    }
    return BingoTap::NONE;
}
