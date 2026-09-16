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


// What the glass shows now, so a tick redraws only what changed. The update
// flow runs this board with no frame buffer (given up for the download's
// TLS handshake), straight onto the panel, where clearing and redrawing the
// whole screen on every key was a flash on every key. So the board is drawn
// once, and after that a key is redrawn when the finger lands on or leaves
// it, the field when a character lands or the last one goes back to *, the
// cursor when it blinks, and every key when the page or SHIFT changes.
uint32_t s_initAt = 0;
bool     s_full   = true;
int8_t   s_drawnLit   = -1;
bool     s_drawnShift = false, s_drawnSym = false, s_drawnShow = false;
uint8_t  s_drawnLen   = 0;
bool     s_drawnReveal = false, s_drawnCursor = false;
int      s_cursorX = 0;
// The transition effect writes over the frame for its first 220 ms; the
// whole screen is drawn every frame until it is done.
const uint32_t SETTLE_MS = 400;

int fieldW(int w) { return showX(w) - GAP - MARGIN; }

void drawKey(TFT_eSPI& t, uint8_t i) {
    const Key& k = s_keys[i];
    if (k.ch == K_NONE) { t.fillRect(k.x, k.y, k.w, k.h, Theme::BG); return; }   // a blank on the symbols page
    const bool lit = (s_armed == (int8_t)i) || (k.ch == K_SHIFT && s_shift);
    Theme::drawSteelKey(t, k.x, k.y, k.w, k.h, lit);
    char one[2];
    const char* lab = label(k.ch, one);
    t.setTextSize(2);
    if (t.textWidth(lab) > k.w - 6) t.setTextSize(1);
    t.setTextColor(lit ? Theme::VAPOR_YELLOW : Theme::WHITE);
    t.setCursor(k.x + (k.w - t.textWidth(lab)) / 2, k.y + (k.h - t.fontHeight()) / 2);
    t.print(lab);
    t.setTextSize(1);
}

// The field: the password as stars but for the last character for a moment
// (or all of it with SHOW on), and the cursor.
void drawField(TFT_eSPI& t, int w, uint32_t now) {
    const int fw = fieldW(w);
    t.fillRect(MARGIN + 1, FIELD_Y + 1, fw - 2, FIELD_H - 2, Theme::BG);
    t.drawRect(MARGIN, FIELD_Y, fw, FIELD_H, Theme::VAPOR_PURPLE);
    const bool reveal = s_len && now - s_typedAt < 900;
    char shown[PASS_MAX + 1];
    for (uint8_t i = 0; i < s_len; i++) shown[i] = (s_show || (reveal && i + 1 == s_len)) ? s_buf[i] : '*';
    shown[s_len] = '\0';
    t.setTextSize(2);
    const int cw = t.textWidth("M");
    const int fits = (fw - 8) / cw;
    const char* tail = s_len > fits ? shown + (s_len - fits) : shown;
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(MARGIN + 4, FIELD_Y + (FIELD_H - t.fontHeight()) / 2);
    t.print(tail);
    s_cursorX = MARGIN + 4 + t.textWidth(tail) + 1;
    const bool cursor = (now / 500) % 2;
    t.fillRect(s_cursorX, FIELD_Y + 3, 2, FIELD_H - 6, cursor ? Theme::CYAN : Theme::BG);
    t.setTextSize(1);
    s_drawnLen = s_len; s_drawnReveal = reveal; s_drawnCursor = cursor;
}

void drawShow(TFT_eSPI& t, int w) {
    t.setTextSize(1);
    Theme::drawWin95Button(t, showX(w), FIELD_Y, SHOW_W, FIELD_H, s_show ? "HIDE" : "SHOW", false);
    s_drawnShow = s_show;
}

void drawKeys(TFT_eSPI& t) {
    for (uint8_t i = 0; i < s_keyN; i++) drawKey(t, i);
    s_drawnLit = s_armed; s_drawnShift = s_shift; s_drawnSym = s_sym;
}

void drawAll(TFT_eSPI& t, uint32_t now) {
    const int w = t.width(), h = t.height();
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
    drawField(t, w, now);
    drawShow(t, w);
    drawKeys(t);
    s_full = false;
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
    s_full   = true;
    s_initAt = millis();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiWifiPassTick(TFT_eSPI& t, uint32_t now) {
    const int w = t.width(), h = t.height();
    layout(w, h);
    if (s_full || now - s_initAt < SETTLE_MS) { drawAll(t, now); return; }
    // Every key when the page or SHIFT changes: the labels are different
    // and the SHIFT key lights while it is on.
    if (s_drawnShift != s_shift || s_drawnSym != s_sym) drawKeys(t);
    else if (s_drawnLit != s_armed) {
        const int8_t old = s_drawnLit;
        s_drawnLit = s_armed;
        if (old >= 0)     drawKey(t, (uint8_t)old);
        if (s_armed >= 0) drawKey(t, (uint8_t)s_armed);
    }
    if (s_drawnShow != s_show) { drawShow(t, w); drawField(t, w, now); }
    const bool reveal = s_len && now - s_typedAt < 900;
    const bool cursor = (now / 500) % 2;
    if (s_drawnLen != s_len || s_drawnReveal != reveal) drawField(t, w, now);
    else if (s_drawnCursor != cursor) {
        // The cursor alone, painted over, so the blink never clears the field.
        t.fillRect(s_cursorX, FIELD_Y + 3, 2, FIELD_H - 6, cursor ? Theme::CYAN : Theme::BG);
        s_drawnCursor = cursor;
    }
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
