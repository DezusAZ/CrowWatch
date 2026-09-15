// SquachWatch-CYD — the SquachMesh menu. See include/ui_meshmenu.h.
#include "ui_meshmenu.h"

#if SQUACH_MESH

#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include "detection.h"
#include "meshtalk.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace {

const int TOP_MARGIN = Theme::LIST_TOP + Theme::LIST_HEADING_H;   // under the heading
const uint8_t ROW_N = 7;     // BACK is pinned to the bottom edge now, not a row

// Row height from live font metrics, shared by drawing and hit-testing so
// the two cannot drift -- the same reason every other row list in this
// codebase computes it rather than hard-coding it.
void geom(TFT_eSPI& t, int& top, int& rowH) {
    top = TOP_MARGIN;
    t.setTextSize(2);
    rowH = t.fontHeight() + 10;
}

void row(TFT_eSPI& t, int w, int y, int hgt, const char* label,
         const char* value, uint16_t valueCol) {
    // A solid panel under the row, as the settings screen has. Without it
    // the labels sat straight on the dimmed backdrop, and the synthwave sun
    // came through the gaps in every word.
    Theme::drawListRowPanel(t, w, y, hgt);
    t.setTextSize(2);
    t.setTextWrap(false);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);
    if (!value) return;
    t.setTextColor(valueCol, Theme::BG);
    const int vw = t.textWidth(value);
    t.setCursor(w - 8 - vw, y + (hgt - t.fontHeight()) / 2);
    t.print(value);
}

} // namespace

void uiMeshMenuInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshMenuTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();

    // Same knocked-back backdrop the other sub-screens use, so this reads as
    // part of the device rather than as a dialog bolted on.
    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 128);

    Theme::drawListHeading(t, "SQUACHMESH", Theme::VAPOR_PINK);

    int top, rowH;
    geom(t, top, rowH);

    const char* nm = Squachy::customName();
    if (!nm) nm = Squachy::nickname();

    // Transmit is coloured by state rather than just labelled, because it is
    // the row with a consequence: green reads as "you are visible to anybody
    // scanning", which is worth being able to see at a glance rather than
    // read.
    row(t, w, top + 0 * rowH, rowH, "DETECT",   Settings::meshDetectLabel(),
        Settings::meshDetect() ? Theme::GREEN : Theme::W95_SHADOW);
    row(t, w, top + 1 * rowH, rowH, "TRANSMIT", Settings::meshTransmitLabel(),
        Settings::meshTransmit() ? Theme::AMBER : Theme::W95_SHADOW);
    // MESSAGES says ERR rather than OFF when the crypto self-test failed at
    // boot: a switch that cannot be turned on should not look like one that
    // merely isn't.
    const bool st = MeshTalk::selfTestOk();
    row(t, w, top + 2 * rowH, rowH, "MESSAGES",
        !st ? "ERR" : (Settings::messagesOn() ? "ON" : "OFF"),
        !st ? Theme::RED : (Settings::messagesOn() ? Theme::GREEN : Theme::W95_SHADOW));
    // How many of them may be on screen at once, and whether they roam.
    row(t, w, top + 3 * rowH, rowH, "CROWD", Settings::meshCrowdLabel(),
        Settings::meshCrowd() > 1 ? Theme::GREEN : Theme::W95_SHADOW);
    // Everybody who has ever held the phrase, here or not.
    char sq[12];
    const uint8_t members = MeshTalk::rosterCount();
    if (members) snprintf(sq, sizeof sq, "%u >", (unsigned)members);
    else         snprintf(sq, sizeof sq, "NONE >");
    row(t, w, top + 4 * rowH, rowH, "SQUAD", sq, members ? Theme::GREEN : Theme::W95_SHADOW);
    // Never the phrase itself. This screen is looked at over shoulders.
    row(t, w, top + 5 * rowH, rowH, "PHRASE",
        MeshTalk::havePhrase() ? "SET >" : "NONE >",
        MeshTalk::havePhrase() ? Theme::VAPOR_YELLOW : Theme::W95_SHADOW);
    row(t, w, top + 6 * rowH, rowH, "NAME",     nm, Theme::VAPOR_YELLOW);

    // One line saying what the two switches actually mean together, because
    // "DETECT off, TRANSMIT on" is not self-evidently "they can see you but
    // you cannot see them".
    const bool d = Settings::meshDetect(), x = Settings::meshTransmit();
    const char* note = d && x ? "You see them, they see you."
                     : d      ? "You see them. They cannot see you."
                     : x      ? "They see you. You cannot see them."
                              : "Off. Nothing sent, nothing shown.";
    t.setTextSize(1);
    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    // Seven rows leave no room under them in landscape, so there the one
    // line that matters most sits beside the heading instead: a warning
    // about messages if there is one, else what the two switches mean.
    const bool below = top + ROW_N * rowH + 6 + 2 * t.fontHeight() + 3 <= h - Theme::PINNED_BACK_H - 2;
    if (below) {
        t.setCursor(8, top + ROW_N * rowH + 6);
        t.print(note);
    }

    // And one for messages, which need both halves: DETECT to hear one,
    // TRANSMIT to answer.
    const char* mnote =
        !MeshTalk::selfTestOk()      ? "Messages off: crypto self-test failed."
      : !Settings::messagesOn()      ? nullptr
      : !MeshTalk::havePhrase()      ? "Messages need a phrase to share first."
      : !Settings::meshDetect()      ? "Messages need DETECT on to be heard."
      : !Settings::meshTransmit()    ? "Messages: you can read, not reply."
                                     : "Messages: reading and replying.";
    if (below && mnote) {
        t.setCursor(8, top + ROW_N * rowH + 6 + t.fontHeight() + 3);
        t.print(mnote);
    }
    if (!below) {
        const bool warn = mnote && strcmp(mnote, "Messages: reading and replying.") != 0;
        const char* one = warn ? mnote : note;
        t.setTextColor(warn ? Theme::AMBER : Theme::W95_LIGHT, Theme::BG);
        t.setCursor(w - 8 - t.textWidth(one), Theme::LIST_TOP + (Theme::LIST_HEADING_H - t.fontHeight()) / 2);
        t.print(one);
    }
    Theme::drawPinnedBack(t, "[ BACK ]");
}

MeshMenuRow uiMeshMenuHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)screenW; (void)screenH;
    int top, rowH;
    geom(t, top, rowH);
    if (x < 0 || y < top) return MeshMenuRow::NONE;
    const int idx = (y - top) / rowH;
    if (idx < 0 || idx >= (int)ROW_N) return MeshMenuRow::NONE;
    return (MeshMenuRow)idx;
}

#endif // SQUACH_MESH
