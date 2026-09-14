// SquachWatch-CYD — UPDATE SQUAD screen. See ui_squadupdate.h.
#if SQUACH_MESH
#include "ui_squadupdate.h"
#include "theme.h"
#include "settings.h"
#include "ota_core.h"
#include "ota_wifi.h"
#include "meshtalk.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {
const int BTN_H = 28;
const int SLOP  = 6;
const uint8_t TALLY_N = 8;

bool     s_share   = false;
bool     s_sent    = false;
uint32_t s_sentAt  = 0;
char     s_tally[TALLY_N][13];
uint8_t  s_tallyN  = 0;

struct Geom { int shareY, shareH, sendX, sendY, sendW; };

Geom geom(TFT_eSPI& t) {
    Geom g;
    t.setTextSize(2);
    g.shareH = t.fontHeight() + 10;
    t.setTextSize(1);
    g.shareY = Theme::LIST_TOP + Theme::LIST_HEADING_H + 4 + 12 * 3 + 6;
    g.sendW  = 150;
    g.sendX  = (t.width() - g.sendW) / 2;
    g.sendY  = t.height() - Theme::PINNED_BACK_H - BTN_H - 8;
    return g;
}

bool in(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx - SLOP && x <= bx + bw + SLOP && y >= by - SLOP && y <= by + bh + SLOP;
}

void line(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    t.setTextColor(c, Theme::BG);
    t.setCursor(8, y);
    t.print(s);
}
} // namespace

void uiSquadUpdateInit(TFT_eSPI& t) {
    s_sent   = false;
    s_tallyN = 0;
    // Sharing defaults to on when there is something to share: the point of
    // the row is the six boards on one desk, and they are on one network.
    s_share  = OtaWifi::hasSaved();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

bool uiSquadUpdateShareWifi() { return s_share && OtaWifi::hasSaved(); }
void uiSquadUpdateToggleShare() { if (OtaWifi::hasSaved()) s_share = !s_share; }

void uiSquadUpdateSent(bool ok, uint32_t now) {
    if (!ok) return;
    s_sent   = true;
    s_sentAt = now;
    s_tallyN = 0;
}

void uiSquadUpdateReported(const char* name) {
    if (!name || !name[0]) name = "SOMEONE";
    for (uint8_t i = 0; i < s_tallyN; i++)
        if (!strcmp(s_tally[i], name)) return;
    if (s_tallyN >= TALLY_N) return;
    snprintf(s_tally[s_tallyN], sizeof s_tally[0], "%s", name);
    s_tallyN++;
}

void uiSquadUpdateTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    (void)eng;
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawListHeading(t, "UPDATE SQUAD", Theme::VAPOR_PINK);
    const Geom g = geom(t);
    t.setTextSize(1);
    int y = Theme::LIST_TOP + Theme::LIST_HEADING_H + 4;
    char buf[48];

    if (!s_sent) {
        line(t, y, Theme::WHITE, "Tells every SquachWatch in range with");
        y += 12;
        snprintf(buf, sizeof buf, "your phrase to update to %s,", OtaCore::runningVersion());
        line(t, y, Theme::WHITE, buf);
        y += 12;
        line(t, y, Theme::WHITE, "the version this one runs.");

        // The SHARE WIFI row, a settings row in shape.
        Theme::drawListRowPanel(t, w, g.shareY, g.shareH);
        t.setTextSize(2);
        const bool can = OtaWifi::hasSaved();
        t.setTextColor(can ? Theme::VAPOR_PINK : Theme::blend(Theme::BG, Theme::VAPOR_PINK, 110), Theme::BG);
        t.setCursor(8, g.shareY + (g.shareH - t.fontHeight()) / 2);
        t.print("SHARE WIFI");
        const char* v = can ? (s_share ? "ON" : "OFF") : "--";
        t.setTextColor(can ? Theme::WHITE : Theme::blend(Theme::BG, Theme::WHITE, 110), Theme::BG);
        t.setCursor(w - 18 - t.textWidth(v), g.shareY + (g.shareH - t.fontHeight()) / 2);
        t.print(v);
        t.setTextSize(1);
        y = g.shareY + g.shareH + 4;
        if (can) snprintf(buf, sizeof buf, "%s, used once and forgotten.", OtaWifi::savedSsid());
        else     snprintf(buf, sizeof buf, "No network saved here to share.");
        line(t, y, Theme::W95_LIGHT, buf);
        y += 12;
        line(t, y, Theme::W95_LIGHT, "Boards with their own WiFi use that.");

        Theme::drawWin95Button(t, g.sendX, g.sendY, g.sendW, BTN_H, "SEND", false);
    } else {
        const uint32_t left = (now - s_sentAt < 60000u) ? (60000u - (now - s_sentAt)) / 1000u : 0;
        if (left) snprintf(buf, sizeof buf, "Sent. On the air for %lu s more.", (unsigned long)left);
        else      snprintf(buf, sizeof buf, "Sent. Boards report back here.");
        line(t, y, Theme::WHITE, buf);
        y += 12;
        line(t, y, Theme::W95_LIGHT, "Each one joins WiFi, installs, restarts,");
        y += 12;
        line(t, y, Theme::W95_LIGHT, "and says so. A minute or two each.");
        y += 18;
        if (s_tallyN == 0) {
            line(t, y, Theme::CYAN, "Nobody yet.");
        } else {
            t.setTextSize(2);
            const int colW = w / 2;
            for (uint8_t i = 0; i < s_tallyN; i++) {
                const int cx = 8 + (i % 2) * colW, cy = y + (i / 2) * (t.fontHeight() + 4);
                if (cy + t.fontHeight() > h - Theme::PINNED_BACK_H - 4) break;
                t.setTextColor(Theme::GREEN, Theme::BG);
                t.setCursor(cx, cy);
                t.print(s_tally[i]);
            }
            t.setTextSize(1);
        }
    }
    Theme::drawPinnedBack(t, "[ BACK ]");
}

SquadUpdateHit uiSquadUpdateHit(TFT_eSPI& t, int x, int y) {
    if (Theme::pinnedBackHit(x, y, t.width(), t.height())) return SquadUpdateHit::BACK;
    if (s_sent) return SquadUpdateHit::NONE;
    const Geom g = geom(t);
    if (in(x, y, 3, g.shareY, t.width() - 10, g.shareH)) return SquadUpdateHit::SHARE;
    if (in(x, y, g.sendX, g.sendY, g.sendW, BTN_H))     return SquadUpdateHit::SEND;
    return SquadUpdateHit::NONE;
}
#endif // SQUACH_MESH
