// SquachWatch-CYD — the UPDATE FIRMWARE screen. See include/ui_update.h.
#include "ui_update.h"
#include "ota_core.h"
#include "ota_ble.h"
#include "ota_wifi.h"
#include "theme.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

bool s_askSwitch = false;

const int BTN_H   = 28;
const int SLOP    = 6;
const int ROW_Y0  = 36;
const int ROW_H   = 24;
const int ROW_GAP = 3;

// One place for every button position, shared by drawing and hit testing so
// the two cannot drift apart.
struct Geom {
    int x, w;                          // the wide buttons
    int wifiY, btY, squadY, switchY;
    int backX, backY, backW;
};

Geom geom(TFT_eSPI& t) {
    const int w = t.width(), h = t.height();
    Geom g;
    g.w = w - 32;
    if (g.w > 230) g.w = 230;
    g.x     = (w - g.w) / 2;
    g.backW = 110;
    g.backX = (w - g.backW) / 2;
    g.backY = h - BTN_H - 6;
    g.switchY = g.backY - BTN_H - 8;
    g.squadY  = OtaCore::otherVersion() ? g.switchY - BTN_H - 8 : g.switchY;
#if SQUACH_MESH
    g.btY     = g.squadY - BTN_H - 8;
#else
    g.btY     = g.squadY;
#endif
    g.wifiY   = g.btY - BTN_H - 8;
    return g;
}

// A row of equal buttons along the bottom edge.
struct Row { int n, y, w, x[3]; };

Row bottomRow(TFT_eSPI& t, int n) {
    Row r;
    r.n = n;
    const int gap = 8, total = t.width() - 16;
    r.w = (total - (n - 1) * gap) / n;
    if (r.w > 110) r.w = 110;
    const int span = n * r.w + (n - 1) * gap;
    for (int i = 0; i < n; i++) r.x[i] = (t.width() - span) / 2 + i * (r.w + gap);
    r.y = t.height() - BTN_H - 6;
    return r;
}

struct Panel { int x, y, w, h, yesX, noX, btnY, btnW; };

Panel panel(TFT_eSPI& t) {
    const int w = t.width(), h = t.height();
    Panel p;
    p.w = w - 24;
    if (p.w > 270) p.w = 270;
    p.h    = 112;
    p.x    = (w - p.w) / 2;
    p.y    = (h - p.h) / 2;
    p.btnW = 92;
    const int gap = 14;
    p.yesX = p.x + (p.w - (p.btnW * 2 + gap)) / 2;
    p.noX  = p.yesX + p.btnW + gap;
    p.btnY = p.y + p.h - BTN_H - 10;
    return p;
}

bool in(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx - SLOP && x <= bx + bw + SLOP && y >= by - SLOP && y <= by + bh + SLOP;
}

int lineH(TFT_eSPI& t) { return t.fontHeight() + 2; }

void label(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    t.setTextColor(c, Theme::BG);
    t.setCursor(8, y);
    t.print(s);
}

void centred(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    t.setTextColor(c, Theme::BG);
    t.setCursor((t.width() - t.textWidth(s)) / 2, y);
    t.print(s);
}

// Wrapped paragraph, left-aligned. Returns the y below it.
int para(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    const int charW = t.textWidth("M");
    int maxW = t.width() - 16;
    if (maxW > 47 * charW) maxW = 47 * charW;
    char lines[6][48];
    const uint8_t n = Theme::wrapText(t, s, maxW, lines, 6);
    t.setTextColor(c, Theme::BG);
    for (uint8_t i = 0; i < n; i++) {
        t.setCursor(8, y);
        t.print(lines[i]);
        y += lineH(t);
    }
    return y;
}

void pair(TFT_eSPI& t, int y, const char* k, const char* v) {
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(8, y);
    t.print(k);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(8 + t.textWidth("OTHER SLOT ") + 4, y);
    t.print(v);
}

void backButton(TFT_eSPI& t, const char* lab) {
    const Geom g = geom(t);
    Theme::drawWin95Button(t, g.backX, g.backY, g.backW, BTN_H, lab, false);
}

void drawBar(TFT_eSPI& t, int y, uint8_t pct) {
    const int x = 16, w = t.width() - 32, h = 16;
    t.drawRect(x, y, w, h, Theme::CYAN);
    const int fill = (w - 4) * pct / 100;
    t.fillRect(x + 2, y + 2, fill, h - 4, Theme::VAPOR_PINK);
    t.fillRect(x + 2 + fill, y + 2, (w - 4) - fill, h - 4, Theme::BG);
}

// ---- the menu -----------------------------------------------------------------

void drawMenu(TFT_eSPI& t) {
    const Geom g = geom(t);
    int y = 22;
    label(t, y, Theme::VAPOR_PINK, "UPDATE FIRMWARE");
    y += lineH(t) + 4;
    pair(t, y, "RUNNING", OtaCore::runningVersion());
    y += lineH(t);
    const char* other = OtaCore::otherVersion();
    pair(t, y, "OTHER SLOT", other ? other : "nothing to switch to");

    Theme::drawWin95Button(t, g.x, g.wifiY, g.w, BTN_H, "UPDATE OVER WIFI", false);
    Theme::drawWin95Button(t, g.x, g.btY,   g.w, BTN_H, "UPDATE OVER BLUETOOTH (BETA)", false);
#if SQUACH_MESH
    Theme::drawWin95Button(t, g.x, g.squadY, g.w, BTN_H, "UPDATE SQUAD", false);
#endif
    if (other) {
        char b[40];
        snprintf(b, sizeof b, "SWITCH TO %.20s", other);
        Theme::drawWin95Button(t, g.x, g.switchY, g.w, BTN_H, b, false);
    }
    backButton(t, "BACK");
}

void drawSwitchPanel(TFT_eSPI& t) {
    const Panel p = panel(t);
    Theme::dimRegion(t, 0, 0, t.width(), t.height(), 140);
    t.fillRect(p.x, p.y, p.w, p.h, Theme::BG);
    t.drawRect(p.x, p.y, p.w, p.h, Theme::CYAN);
    char head[40];
    snprintf(head, sizeof head, "SWITCH TO %.20s?", OtaCore::otherVersion() ? OtaCore::otherVersion() : "");
    int y = p.y + 10;
    t.setTextColor(Theme::AMBER, Theme::BG);
    t.setCursor(p.x + (p.w - t.textWidth(head)) / 2, y);
    t.print(head);
    y += lineH(t) + 4;
    const char* l1 = "Restarts into the other version.";
    const char* l2 = "Your settings stay.";
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(p.x + (p.w - t.textWidth(l1)) / 2, y); t.print(l1);
    y += lineH(t);
    t.setCursor(p.x + (p.w - t.textWidth(l2)) / 2, y); t.print(l2);
    Theme::drawWin95Button(t, p.yesX, p.btnY, p.btnW, BTN_H, "SWITCH", false);
    Theme::drawWin95Button(t, p.noX,  p.btnY, p.btnW, BTN_H, "CANCEL", false);
}

// ---- shared progress screens --------------------------------------------------

// The bar and the numbers under it, painted over whatever is there. The
// numbers are padded to a fixed width and drawn with a background colour, so
// a shorter string never leaves the tail of a longer one behind and nothing
// has to be cleared first.
int progressLive(TFT_eSPI& t, uint32_t got, uint32_t total, uint8_t pct) {
    int y = 24 + lineH(t) + 8;
    drawBar(t, y, pct);
    y += 24;
    char b[40];
    const int n = snprintf(b, sizeof b, "%lu of %lu KB   %u%%", (unsigned long)(got / 1024),
                           (unsigned long)(total / 1024), (unsigned)pct);
    const int FIELD = 30;
    char padded[FIELD + 1];
    const int left = n < FIELD ? (FIELD - n) / 2 : 0;
    snprintf(padded, sizeof padded, "%*s%s%*s", left, "", b, FIELD - n - left > 0 ? FIELD - n - left : 0, "");
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor((t.width() - FIELD * t.textWidth("M")) / 2, y);
    t.print(padded);
    return y;
}

void drawProgress(TFT_eSPI& t, const char* head, uint32_t got, uint32_t total, uint8_t pct, const char* note) {
    label(t, 24, Theme::AMBER, head);
    int y = progressLive(t, got, total, pct);
    y += lineH(t) + 8;
    para(t, y, Theme::WHITE, note);
    backButton(t, "CANCEL");
}

void drawFinishing(TFT_eSPI& t, const char* head, uint16_t c, bool restarting) {
    int y = 24;
    label(t, y, c, head);
    y += lineH(t) + 8;
    drawBar(t, y, 100);
    y += 26;
    if (restarting) {
        label(t, y, Theme::WHITE, "Restarting now.");
        y += lineH(t) + 4;
        para(t, y, Theme::AMBER,
             "When it comes back, leave it on for 30 seconds so it can confirm the new version.");
    }
}

// ---- Bluetooth ----------------------------------------------------------------

void drawBtWaiting(TFT_eSPI& t, bool connected) {
    const Geom g = geom(t);
    int y = 22;
    if (!connected)                  label(t, y, Theme::AMBER, "WAITING FOR YOUR BROWSER");
    else if (OtaBle::codeAccepted()) label(t, y, Theme::CYAN,  "CODE ACCEPTED - GETTING READY");
    else                             label(t, y, Theme::CYAN,  "CONNECTED - TYPE THE CODE");
    y += lineH(t) + 4;
    label(t, y, Theme::WHITE, "On a computer or Android phone,");
    y += lineH(t);
    label(t, y, Theme::WHITE, "open");
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.print(" squachwatch.com/update");
    y += lineH(t);
    label(t, y, Theme::WHITE, "press CONNECT and pick");
    y += lineH(t);
    label(t, y, Theme::CYAN, OtaBle::deviceName());
    y += lineH(t) + 4;
    label(t, y, Theme::WHITE, "Then type this code:");
    y += lineH(t) + 2;

    char code[16];
    const uint32_t c = OtaBle::pairingCode();
    snprintf(code, sizeof code, "%03lu %03lu", (unsigned long)(c / 1000), (unsigned long)(c % 1000));
    t.setTextSize(3);
    centred(t, y, Theme::VAPOR_PINK, code);
    y += t.fontHeight() + 6;
    t.setTextSize(1);
    if (y < g.backY - lineH(t) - 2) centred(t, y, Theme::W95_SHADOW, "Detection is paused on this screen.");
    backButton(t, "CANCEL");
}

void drawFailed(TFT_eSPI& t, const char* words, bool canRetry) {
    int y = 24;
    label(t, y, Theme::VAPOR_PINK, "UPDATE STOPPED");
    y += lineH(t) + 8;
    para(t, y, Theme::WHITE, words);
    if (canRetry) {
        const Row r = bottomRow(t, 2);
        Theme::drawWin95Button(t, r.x[0], r.y, r.w, BTN_H, "TRY AGAIN", false);
        Theme::drawWin95Button(t, r.x[1], r.y, r.w, BTN_H, "DONE", false);
    } else {
        backButton(t, "OK");
    }
}

void drawBt(TFT_eSPI& t) {
    switch (OtaBle::state()) {
        case OtaBle::State::WAITING:   drawBtWaiting(t, false); break;
        case OtaBle::State::CONNECTED: drawBtWaiting(t, true);  break;
        case OtaBle::State::RECEIVING:
            drawProgress(t, "INSTALLING UPDATE", OtaBle::bytesReceived(), OtaBle::bytesExpected(),
                         OtaBle::percent(), "Keep the board powered and close by, and leave the browser tab open.");
            break;
        case OtaBle::State::VERIFYING: drawFinishing(t, "CHECKING SIGNATURE...", Theme::AMBER, false); break;
        case OtaBle::State::DONE:      drawFinishing(t, "UPDATE INSTALLED", Theme::CYAN, true); break;
        case OtaBle::State::FAILED:    drawFailed(t, OtaBle::failureText(), false); break;
        default: break;
    }
}

// ---- WiFi ---------------------------------------------------------------------

int pickRowCount(TFT_eSPI& t) {
    const int bottom = bottomRow(t, 1).y - 6;
    int n = (bottom - ROW_Y0) / (ROW_H + ROW_GAP);
    if (n > OtaWifi::netCount()) n = OtaWifi::netCount();
    return n < 0 ? 0 : n;
}

void signalBars(TFT_eSPI& t, int x, int yBottom, int8_t rssi) {
    const int bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        const int bh = 3 + i * 3;
        t.fillRect(x + i * 4, yBottom - bh, 3, bh, i < bars ? Theme::CYAN : Theme::W95_SHADOW);
    }
}

void drawPick(TFT_eSPI& t) {
    const int w = t.width();
    label(t, 22, Theme::VAPOR_PINK, "PICK YOUR WIFI");
    const int n = pickRowCount(t);
    if (!OtaWifi::netCount()) {
        para(t, ROW_Y0 + 4, Theme::WHITE, "No networks found. Move closer to the router and press RESCAN.");
    }
    for (int i = 0; i < n; i++) {
        const OtaWifi::Net* net = OtaWifi::net((uint8_t)i);
        const int y = ROW_Y0 + i * (ROW_H + ROW_GAP);
        t.fillRect(8, y, w - 16, ROW_H, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, ROW_H, Theme::VAPOR_PURPLE);
        const bool saved = OtaWifi::savedIndexOf(net->ssid) >= 0;
        const char* tag = saved ? "SAVED" : (net->open ? "OPEN" : "");
        const int tagW = tag[0] ? t.textWidth(tag) + 6 : 0;
        const int maxChars = (w - 16 - 12 - 20 - tagW) / t.textWidth("M");
        char name[40];
        snprintf(name, sizeof name, "%.*s", maxChars > 32 ? 32 : maxChars, net->ssid);
        t.setTextColor(Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + (ROW_H - t.fontHeight()) / 2);
        t.print(name);
        if (tag[0]) {
            t.setTextColor(saved ? Theme::CYAN : Theme::AMBER, Theme::TASKBAR);
            t.setCursor(w - 16 - 20 - tagW, y + (ROW_H - t.fontHeight()) / 2);
            t.print(tag);
        }
        signalBars(t, w - 16 - 16, y + ROW_H - 5, net->rssi);
    }
    const Row r = bottomRow(t, 2);
    Theme::drawWin95Button(t, r.x[0], r.y, r.w, BTN_H, "RESCAN", false);
    Theme::drawWin95Button(t, r.x[r.n - 1], r.y, r.w, BTN_H, "CANCEL", false);
}

void drawReady(TFT_eSPI& t) {
    const Geom g = geom(t);
    const bool same = OtaWifi::upToDate();
    int y = 22;
    label(t, y, same ? Theme::CYAN : Theme::VAPOR_PINK, same ? "YOU'RE UP TO DATE" : "UPDATE AVAILABLE");
    y += lineH(t) + 6;
    pair(t, y, "LATEST", OtaWifi::latestVersion());
    y += lineH(t);
    pair(t, y, "RUNNING", OtaCore::runningVersion());
    y += lineH(t) + 6;
    para(t, y, Theme::W95_SHADOW, "Bluetooth and detection stay off until the board restarts.");
    char b[40];
    snprintf(b, sizeof b, "%s %.20s", same ? "REINSTALL" : "INSTALL", OtaWifi::latestVersion());
    Theme::drawWin95Button(t, g.x, g.switchY, g.w, BTN_H, b, false);
    backButton(t, "CANCEL");
}

void drawWifi(TFT_eSPI& t) {
    switch (OtaWifi::state()) {
        case OtaWifi::State::SCANNING:
            label(t, 24, Theme::AMBER, "LOOKING FOR WIFI...");
            backButton(t, "CANCEL");
            break;
        case OtaWifi::State::PICK:
            drawPick(t);
            break;
        case OtaWifi::State::CONNECTING: {
            char b[48];
            snprintf(b, sizeof b, "JOINING %.28s...", OtaWifi::network());
            label(t, 24, Theme::AMBER, b);
            para(t, 24 + lineH(t) + 8, Theme::WHITE,
                 "Bluetooth and detection are off until the board restarts, to make room for the download.");
            backButton(t, "CANCEL");
            break;
        }
        case OtaWifi::State::CHECKING:
            label(t, 24, Theme::AMBER, "CHECKING FOR UPDATES...");
            backButton(t, "CANCEL");
            break;
        case OtaWifi::State::READY:
            drawReady(t);
            break;
        case OtaWifi::State::DOWNLOADING:
            drawProgress(t, "DOWNLOADING UPDATE", OtaWifi::bytesReceived(), OtaWifi::bytesExpected(),
                         OtaWifi::percent(), "Keep the board powered and in range of your WiFi.");
            break;
        case OtaWifi::State::VERIFYING: drawFinishing(t, "CHECKING SIGNATURE...", Theme::AMBER, false); break;
        case OtaWifi::State::DONE:      drawFinishing(t, "UPDATE INSTALLED", Theme::CYAN, true); break;
        case OtaWifi::State::FAILED:    drawFailed(t, OtaWifi::failureText(), OtaWifi::canTryAgain()); break;
        default: break;
    }
}

}  // namespace

void uiUpdateInit(TFT_eSPI& t) {
    OtaCore::refreshOther();
    s_askSwitch = false;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiUpdateAskSwitch(bool ask) { s_askSwitch = ask; }

void uiUpdateTick(TFT_eSPI& t, uint32_t now, bool full) {
    (void)now;
    if (!full) {
        t.setTextSize(1);
        t.setTextWrap(false);
        if (OtaWifi::state() == OtaWifi::State::DOWNLOADING)
            progressLive(t, OtaWifi::bytesReceived(), OtaWifi::bytesExpected(), OtaWifi::percent());
        else if (OtaBle::state() == OtaBle::State::RECEIVING)
            progressLive(t, OtaBle::bytesReceived(), OtaBle::bytesExpected(), OtaBle::percent());
        return;
    }
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> UPDATE FIRMWARE <<");
    t.setTextSize(1);
    t.setTextWrap(false);

    const bool wifiOn = OtaWifi::state() != OtaWifi::State::OFF;
    const bool btOn   = OtaBle::state()  != OtaBle::State::OFF;
    const bool busyInstalling = (wifiOn && OtaWifi::state() >= OtaWifi::State::VERIFYING &&
                                 OtaWifi::state() != OtaWifi::State::FAILED) ||
                                (btOn && OtaBle::state() == OtaBle::State::DONE);
    if (OtaCore::restartPending() && !busyInstalling) {
        label(t, 24, Theme::CYAN, "RESTARTING");
        para(t, 24 + lineH(t) + 8, Theme::WHITE,
             OtaCore::otherVersion() && !wifiOn && !btOn
                 ? "Switching to the other version. Leave it on for 30 seconds after it comes back."
                 : "Detection and Bluetooth come back after the restart.");
        return;
    }
    if (wifiOn) { drawWifi(t); return; }
    if (btOn)   { drawBt(t);   return; }
    drawMenu(t);
    if (s_askSwitch) drawSwitchPanel(t);
}

UpdateHit uiUpdateHitTest(TFT_eSPI& t, int x, int y, int* netIndex) {
    if (OtaCore::restartPending()) return UpdateHit::NONE;
    const Geom g = geom(t);
    const bool onBack = in(x, y, g.backX, g.backY, g.backW, BTN_H);

    const OtaWifi::State ws = OtaWifi::state();
    if (ws != OtaWifi::State::OFF) {
        switch (ws) {
            case OtaWifi::State::PICK: {
                const int n = pickRowCount(t);
                for (int i = 0; i < n; i++) {
                    const int ry = ROW_Y0 + i * (ROW_H + ROW_GAP);
                    if (y >= ry && y < ry + ROW_H + ROW_GAP && x >= 8 && x <= t.width() - 8) {
                        if (netIndex) *netIndex = i;
                        return UpdateHit::NETWORK;
                    }
                }
                const Row r = bottomRow(t, 2);
                if (in(x, y, r.x[0], r.y, r.w, BTN_H)) return UpdateHit::RESCAN;
                if (in(x, y, r.x[r.n - 1], r.y, r.w, BTN_H)) return UpdateHit::CANCEL;
                return UpdateHit::NONE;
            }
            case OtaWifi::State::READY:
                if (in(x, y, g.x, g.switchY, g.w, BTN_H)) return UpdateHit::INSTALL;
                return onBack ? UpdateHit::CANCEL : UpdateHit::NONE;
            case OtaWifi::State::FAILED:
                if (OtaWifi::canTryAgain()) {
                    const Row r = bottomRow(t, 2);
                    if (in(x, y, r.x[0], r.y, r.w, BTN_H)) return UpdateHit::TRY_AGAIN;
                    if (in(x, y, r.x[1], r.y, r.w, BTN_H)) return UpdateHit::OK;
                    return UpdateHit::NONE;
                }
                return onBack ? UpdateHit::OK : UpdateHit::NONE;
            case OtaWifi::State::SCANNING:
            case OtaWifi::State::CONNECTING:
            case OtaWifi::State::CHECKING:
            case OtaWifi::State::DOWNLOADING:
                return onBack ? UpdateHit::CANCEL : UpdateHit::NONE;
            default:
                return UpdateHit::NONE;
        }
    }

    switch (OtaBle::state()) {
        case OtaBle::State::OFF:
            if (s_askSwitch) {
                const Panel p = panel(t);
                if (in(x, y, p.yesX, p.btnY, p.btnW, BTN_H)) return UpdateHit::SWITCH_CONFIRM;
                if (in(x, y, p.noX,  p.btnY, p.btnW, BTN_H)) return UpdateHit::SWITCH_CANCEL;
                return UpdateHit::NONE;
            }
            if (in(x, y, g.x, g.wifiY, g.w, BTN_H)) return UpdateHit::WIFI_START;
            if (in(x, y, g.x, g.btY,   g.w, BTN_H)) return UpdateHit::BT_START;
#if SQUACH_MESH
            if (in(x, y, g.x, g.squadY, g.w, BTN_H)) return UpdateHit::SQUAD_START;
#endif
            if (OtaCore::otherVersion() && in(x, y, g.x, g.switchY, g.w, BTN_H)) return UpdateHit::SWITCH;
            return onBack ? UpdateHit::BACK : UpdateHit::NONE;
        case OtaBle::State::WAITING:
        case OtaBle::State::CONNECTED:
        case OtaBle::State::RECEIVING:
            return onBack ? UpdateHit::CANCEL : UpdateHit::NONE;
        case OtaBle::State::FAILED:
            return onBack ? UpdateHit::OK : UpdateHit::NONE;
        default:
            return UpdateHit::NONE;
    }
}
