// SquachWatch-CYD — the WiFi password keyboard. See include/ui_wifipass.h.
#include "ui_wifipass.h"
#include "theme.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

// macOS's <limits.h> defines PASS_MAX as a macro (the legacy getpass()
// limit), which the emulator build there trips over. Nothing here wants
// that one.
#ifdef PASS_MAX
#undef PASS_MAX
#endif
const uint8_t PASS_MAX = 63;          // the WPA2 limit

// Keys that are not characters.
const char K_NONE  = 0;
const char K_SHIFT = 1;
const char K_SYM   = 2;
const char K_DEL   = '\b';
const char K_OK    = '\n';

struct Key { int16_t x, y, w, h; char ch; };
Key     s_keys[48];
uint8_t s_keyN = 0;

char     s_ssid[33] = "";
char     s_buf[PASS_MAX + 1] = "";
uint8_t  s_len     = 0;
bool     s_shift   = false;
bool     s_sym     = false;
bool     s_show    = false;
uint32_t s_typedAt = 0;               // the last character shows for a moment
WifiPassResult s_result = WifiPassResult::NONE;

int8_t   s_armed    = -1;
int8_t   s_lastKey  = -1;
uint32_t s_lastUpAt = 0;
const uint32_t BOUNCE_MS = 60;        // the panel dropping out for a frame

// The payphone's jump filter, in miniature: a single sample far from the last
// is contact loss, a run of agreeing ones is the finger really moving.
struct Filter {
    int16_t x = 0, y = 0, cx = 0, cy = 0;
    uint8_t cn = 0;
    void down(int px, int py) { x = px; y = py; cn = 0; }
    void move(int px, int py) {
        if (abs(px - x) + abs(py - y) <= 40) { x = px; y = py; cn = 0; return; }
        if (cn && abs(px - cx) + abs(py - cy) <= 8) {
            if (++cn >= 3) { x = px; y = py; cn = 0; }
        } else {
            cx = px; cy = py; cn = 1;
        }
    }
} s_filter;

const int TOP_H   = 52;               // header and field above the keys
const int MARGIN  = 4, GAP = 2, ROW_GAP = 4;
const int BACK_X  = 4, BACK_Y = 3, BACK_W = 52, BACK_H = 18;
const int FIELD_Y = 28, FIELD_H = 20;
const int SHOW_W  = 50;

// Letters page and symbols page, row by row. A zero is a blank key.
const char* const PAGE_ABC[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
const char* const PAGE_SYM[4] = { "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/", "?`~\0\0\0\0" };

char charFor(uint8_t row, uint8_t col) {
    char c = s_sym ? PAGE_SYM[row][col] : PAGE_ABC[row][col];
    if (!s_sym && s_shift && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return c;
}

void put(int x, int y, int w, int h, char ch) {
    if (s_keyN >= sizeof s_keys / sizeof s_keys[0]) return;
    s_keys[s_keyN++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, ch };
}

void layout(int w, int h) {
    s_keyN = 0;
    const int avail = w - 2 * MARGIN;
    const int bandTop = TOP_H + 4, bandBottom = h - 4;
    int rh = (bandBottom - bandTop - 4 * ROW_GAP) / 5;
    if (rh > 40) rh = 40;
    if (rh < 22) rh = 22;
    int y = bandBottom - (5 * rh + 4 * ROW_GAP);

    // Rows 0-1: ten across.
    const int w1 = (avail - 9 * GAP) / 10;
    const int x1 = MARGIN + (avail - (10 * w1 + 9 * GAP)) / 2;
    for (uint8_t r = 0; r < 2; r++) {
        for (uint8_t i = 0; i < 10; i++) put(x1 + i * (w1 + GAP), y, w1, rh, charFor(r, i));
        y += rh + ROW_GAP;
    }
    // Row 2: nine, centred on the same unit.
    const int x2 = MARGIN + (avail - (9 * w1 + 8 * GAP)) / 2;
    for (uint8_t i = 0; i < 9; i++) put(x2 + i * (w1 + GAP), y, w1, rh, charFor(2, i));
    y += rh + ROW_GAP;
    // Row 3: SHIFT, seven, DEL -- the two wide keys take what the seven leave.
    const int wide3 = (avail - 7 * w1 - 8 * GAP) / 2;
    int x = MARGIN;
    put(x, y, wide3, rh, s_sym ? K_NONE : K_SHIFT); x += wide3 + GAP;
    for (uint8_t i = 0; i < 7; i++) { put(x, y, w1, rh, charFor(3, i)); x += w1 + GAP; }
    put(x, y, MARGIN + avail - x, rh, K_DEL);
    y += rh + ROW_GAP;
    // Row 4: page switch, space, OK.
    const int side = w1 * 2 + GAP;
    put(MARGIN, y, side, rh, K_SYM);
    put(MARGIN + side + GAP, y, avail - 2 * side - 2 * GAP, rh, ' ');
    put(MARGIN + avail - side, y, side, rh, K_OK);
}

int keyAt(int x, int y, int reach) {
    int best = -1;
    long bestD = (long)reach * reach + 1;
    for (uint8_t i = 0; i < s_keyN; i++) {
        const Key& k = s_keys[i];
        if (k.ch == K_NONE) continue;
        const int r = k.x + k.w - 1, b = k.y + k.h - 1;
        const int dx = (x < k.x) ? (k.x - x) : (x > r ? x - r : 0);
        const int dy = (y < k.y) ? (k.y - y) : (y > b ? y - b : 0);
        const long d = (long)dx * dx + (long)dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

const char* label(char ch, char* one) {
    switch (ch) {
        case K_SHIFT: return "SHIFT";
        case K_SYM:   return s_sym ? "abc" : "#+=";
        case K_DEL:   return "DEL";
        case K_OK:    return "OK";
        case ' ':     return "SPACE";
        default:      one[0] = ch; one[1] = '\0'; return one;
    }
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx - 4 && x <= rx + rw + 4 && y >= ry - 4 && y <= ry + rh + 4;
}

int showX(int w) { return w - MARGIN - SHOW_W; }

void press(int x, int y, uint32_t now) {
    s_filter.down(x, y);
    s_armed = (int8_t)keyAt(x, y, 10);
    if (s_armed >= 0 && s_armed == s_lastKey && now - s_lastUpAt < BOUNCE_MS) s_armed = -1;
}

void type(char ch, uint32_t now) {
    switch (ch) {
        case K_SHIFT: s_shift = !s_shift; break;
        case K_SYM:   s_sym = !s_sym; s_shift = false; break;
        case K_DEL:   if (s_len) s_buf[--s_len] = '\0'; break;
        case K_OK:    if (s_len) s_result = WifiPassResult::OK; break;
        case K_NONE:  break;
        default:
            if (s_len < PASS_MAX) { s_buf[s_len++] = ch; s_buf[s_len] = '\0'; s_typedAt = now; }
            break;
    }
}

}  // namespace

void uiWifiPassInit(TFT_eSPI& t, const char* ssid) {
    strncpy(s_ssid, ssid ? ssid : "", sizeof s_ssid - 1);
    s_ssid[sizeof s_ssid - 1] = '\0';
    uiWifiPassClear();
    s_shift = s_sym = s_show = false;
    s_result = WifiPassResult::NONE;
    s_armed = s_lastKey = -1;
    layout(t.width(), t.height());
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiWifiPassTick(TFT_eSPI& t, uint32_t now) {
    const int w = t.width(), h = t.height();
    layout(w, h);
    t.fillRect(0, 0, w, h, Theme::BG);
    t.setTextWrap(false);

    // Header: BACK, then which network this is for.
    Theme::drawWin95Button(t, BACK_X, BACK_Y, BACK_W, BACK_H, "BACK", false);
    t.setTextSize(1);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(BACK_X + BACK_W + 8, BACK_Y + 1);
    t.print("PASSWORD FOR");
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(BACK_X + BACK_W + 8, BACK_Y + 11);
    char ssid[40];
    const int maxChars = (w - (BACK_X + BACK_W + 8) - MARGIN) / t.textWidth("M");
    snprintf(ssid, sizeof ssid, "%.*s", maxChars > 32 ? 32 : maxChars, s_ssid);
    t.print(ssid);

    // The field, with SHOW/HIDE beside it.
    const int fw = showX(w) - GAP - MARGIN;
    t.drawRect(MARGIN, FIELD_Y, fw, FIELD_H, Theme::VAPOR_PURPLE);
    char shown[PASS_MAX + 1];
    for (uint8_t i = 0; i < s_len; i++) {
        const bool last = (i + 1 == s_len) && now - s_typedAt < 900;
        shown[i] = (s_show || last) ? s_buf[i] : '*';
    }
    shown[s_len] = '\0';
    t.setTextSize(2);
    const int cw = t.textWidth("M");
    const int fits = (fw - 8) / cw;
    const char* tail = s_len > fits ? shown + (s_len - fits) : shown;
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(MARGIN + 4, FIELD_Y + (FIELD_H - t.fontHeight()) / 2);
    t.print(tail);
    if ((now / 500) % 2) t.fillRect(MARGIN + 4 + t.textWidth(tail) + 1, FIELD_Y + 3, 2, FIELD_H - 6, Theme::CYAN);
    t.setTextSize(1);
    Theme::drawWin95Button(t, showX(w), FIELD_Y, SHOW_W, FIELD_H, s_show ? "HIDE" : "SHOW", false);

    // Keys.
    for (uint8_t i = 0; i < s_keyN; i++) {
        const Key& k = s_keys[i];
        if (k.ch == K_NONE) continue;
        const bool lit = (s_armed == (int8_t)i) || (k.ch == K_SHIFT && s_shift);
        t.fillRect(k.x, k.y, k.w, k.h, lit ? Theme::PURPLE : Theme::TASKBAR);
        t.drawRect(k.x, k.y, k.w, k.h, Theme::W95_SHADOW);
        char one[2];
        const char* lab = label(k.ch, one);
        t.setTextSize(2);
        if (t.textWidth(lab) > k.w - 4) t.setTextSize(1);
        t.setTextColor(lit ? Theme::VAPOR_YELLOW : Theme::WHITE);
        t.setCursor(k.x + (k.w - t.textWidth(lab)) / 2, k.y + (k.h - t.fontHeight()) / 2);
        t.print(lab);
    }
    t.setTextSize(1);
}

void uiWifiPassTouch(int x, int y, uint32_t now, WifiPassTouch phase) {
    switch (phase) {
        case WifiPassTouch::DOWN:
            if (inRect(x, y, BACK_X, BACK_Y, BACK_W, BACK_H)) { s_result = WifiPassResult::BACK; return; }
            // SHOW sits right of the field; its x depends on the screen width,
            // which the last layout() pass left in the rightmost key.
            if (s_keyN && y >= FIELD_Y - 4 && y <= FIELD_Y + FIELD_H + 4 &&
                x >= (s_keys[9].x + s_keys[9].w) - SHOW_W - 4) {
                s_show = !s_show;
                return;
            }
            press(x, y, now);
            break;
        case WifiPassTouch::MOVE:
            if (s_armed < 0) return;
            s_filter.move(x, y);
            s_armed = (int8_t)keyAt(s_filter.x, s_filter.y, 20);
            break;
        case WifiPassTouch::UP: {
            // Nothing is read from the lift itself -- see the header.
            const int k = s_armed;
            s_armed    = -1;
            s_lastKey  = (int8_t)k;
            s_lastUpAt = now;
            if (k >= 0) type(s_keys[k].ch, now);
            break;
        }
    }
}

WifiPassResult uiWifiPassResult() { return s_result; }
const char*    uiWifiPassText()   { return s_buf; }
const char*    uiWifiPassSsid()   { return s_ssid; }

void uiWifiPassClear() {
    memset(s_buf, 0, sizeof s_buf);
    s_len = 0;
}
