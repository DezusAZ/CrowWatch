// Backing for gfx_shim.h — see that header for the architecture. All
// drawing funnels through the four virtual primitives, so full-screen
// (panel) and sprite paths share identical algorithms.
#include <Arduino.h>
#include "gfx_shim.h"

static Arduino_DataBus* s_bus = nullptr;
static Arduino_RGB_Display* s_panel = nullptr;

Arduino_GFX* gfxPanel() { return s_panel; }

void gfxInit() {
    if (s_panel) return;
    s_bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
    Arduino_ESP32RGBPanel* rgbpanel = new Arduino_ESP32RGBPanel(
        18, 17, 16, 21,
        11, 12, 13, 14, 0,
        8, 20, 3, 46, 9, 10,
        4, 5, 6, 7, 15,
        1, 10, 8, 50, 1, 10, 8, 20, 0, 12500000);
    s_panel = new Arduino_RGB_Display(
        480, 480, rgbpanel, 0, true,
        s_bus, GFX_NOT_DEFINED, st7701_type9_init_operations,
        sizeof(st7701_type9_init_operations));
    if (!s_panel->begin()) Serial.println("[shim] panel init failed");
}

void gfxWriteCommand(uint8_t c) { if (s_bus) s_bus->writeCommand(c); }
void gfxWriteData(uint8_t d) { if (s_bus) s_bus->write(d); }

// ---- TFT_eSPI root ----

// The full-screen instance draws straight to the panel.
void TFT_eSPI::px(int32_t x, int32_t y, uint32_t c) {
    if (gfxPanel()) gfxPanel()->drawPixel(x, y, c);
}
void TFT_eSPI::hline(int32_t x, int32_t y, int32_t w, uint32_t c) {
    if (gfxPanel()) gfxPanel()->writeFastHLine(x, y, w, c);
}
void TFT_eSPI::vline(int32_t x, int32_t y, int32_t h, uint32_t c) {
    if (gfxPanel()) gfxPanel()->writeFastVLine(x, y, h, c);
}
void TFT_eSPI::fillBox(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    if (gfxPanel()) gfxPanel()->fillRect(x, y, w, h, c);
}

void TFT_eSPI::reset() {
    cursor_x = cursor_y = 0;
    _w = 480; _h = 480;
    _rot = 0; _fg = TFT_WHITE; _bg = TFT_BLACK;
    _fillbg = false; _wrap = true; _size = 1; _font = 1; _gfxFont = nullptr;
}

// Square panel: every rotation is 480x480. Direct panel draws rotate
// through Arduino_GFX; the sprite push rotates itself (convertFrame).
static uint8_t s_screenRot = 0;
void TFT_eSPI::setRotation(uint8_t r) {
    _rot = r & 3;
    _w = _h = 480;
    s_screenRot = _rot;
    if (s_panel) s_panel->setRotation(_rot);
}

void TFT_eSPI::invertDisplay(bool i) {
    // ST7701: 0x21 inverts the whole panel, 0x20 returns it to normal.
    // The manufacturer's init leaves it unset; the app's themes draw
    // "normal" polarity (black backgrounds), so the panel must be in
    // normal mode for them to read correctly. Sent on every change so a
    // later toggle can't leave the panel inverted behind our back.
    gfxWriteCommand(i ? 0x21 : 0x20);
}

void TFT_eSPI::setTextSize(uint8_t s) { _size = s ? s : 1; }
void TFT_eSPI::setTextColor(uint16_t c) { _fg = c; _fillbg = false; }
void TFT_eSPI::setTextColor(uint16_t c, uint16_t b) { _fg = c; _bg = b; _fillbg = true; }
void TFT_eSPI::setTextFont(uint8_t f) { _font = f; }
void TFT_eSPI::setFreeFont(const GFXfont* f) { _gfxFont = f; }
void TFT_eSPI::setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }

static inline uint8_t fontWidthPx(uint8_t font, uint8_t c) {
    return font == 1 ? 6 : (uint8_t)pgm_read_byte(widtbl_f16 + (c - 32));
}

int16_t TFT_eSPI::fontHeight() {
    if (_gfxFont) return _gfxFont->yAdvance ? _gfxFont->yAdvance : 8;
    const uint8_t base = (_font == 2) ? 16 : 8;
    return base * _size;
}

int16_t TFT_eSPI::textWidth(const char* s) {
    if (!s) return 0;
    int16_t w = 0;
    for (const uint8_t* p = (const uint8_t*)s; *p; p++)
        w += fontWidthPx(_font, *p);
    return w * _size;
}

int16_t TFT_eSPI::textWidth(const __FlashStringHelper* s) {
    return textWidth((const char*)s);
}

int TFT_eSPI::printf(const char* fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) print(buf);
    return n;
}

void TFT_eSPI::newLine() {
    cursor_x = 0;
    cursor_y += fontHeight();
}

void TFT_eSPI::drawChar(uint8_t c) {
    if (c == '\n') { newLine(); return; }
    const uint8_t cw = fontWidthPx(_font, c) * _size;
    if (_wrap && (cursor_x + cw) > _w) { cursor_x = 0; cursor_y += fontHeight(); }
    // The cursor is the glyph's top-left for the built-in fonts.
    if (_font == 1) drawCharCell(cursor_x, cursor_y, c, _size);
    else            drawChar16(cursor_x, cursor_y, c, _size);
    cursor_x += cw;
}

void TFT_eSPI::drawCharCell(int32_t x, int32_t y, uint8_t c, uint8_t size) {
    // GLCD glyphs are column-major: five bytes, byte i is column i, and
    // each byte's low bit is the top of that column's 8-row cell. The
    // library's order is five table columns plus a sixth blank column,
    // which is what the "6-wide" cell means.
    if (c >= 32) {
        for (int8_t i = 0; i < 6; i++) {
            uint8_t line = (i == 5) ? 0 : pgm_read_byte(font + c * 5 + i);
            for (int8_t j = 0; j < 8; j++) {
                const bool on = line & 0x01;
                if (on || _fillbg) vline(x + i * size, y + j * size, size, on ? _fg : _bg);
                line >>= 1;
            }
        }
    } else if (_fillbg) {
        fillBox(x, y, 6 * size, 8 * size, _bg);
    }
}

void TFT_eSPI::drawChar16(int32_t x, int32_t y, uint8_t c, uint8_t size) {
    if (c >= 32) {
        const uint8_t* g = (const uint8_t*)pgm_read_ptr(chrtbl_f16 + (c - 32));
        const uint8_t w = (uint8_t)pgm_read_byte(widtbl_f16 + (c - 32));
        if (size > 1) {
            for (uint8_t row = 0; row < 16; row++) {
                uint8_t bits = pgm_read_byte(g + row);
                for (uint8_t col = 0; col < w; col++) {
                    if (bits & (0x80 >> col)) fillBox(x + col * size, y + row * size, size, size, _fg);
                    else if (_fillbg) fillBox(x + col * size, y + row * size, size, size, _bg);
                }
                if (_fillbg) fillBox(x + w * size, y + row * size, size, size, _bg);
            }
        } else {
            for (uint8_t row = 0; row < 16; row++) {
                uint8_t bits = pgm_read_byte(g + row);
                for (uint8_t col = 0; col < w; col++) {
                    if (bits & (0x80 >> col)) px(x + col, y + row, _fg);
                    else if (_fillbg) px(x + col, y + row, _bg);
                }
                if (_fillbg) px(x + w, y + row, _bg);
            }
        }
    } else if (_fillbg) {
        fillBox(x, y, 6, 16, _bg);
    }
}

void TFT_eSPI::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) {
    // GLCD glyph at a given position with explicit colors (the
    // fast_sprite path's fall-back). Column-major like drawCharCell.
    if (c < 32 || c > 254) return;
    const bool fillbg = (color != bg);
    const uint8_t fg = color, bgb = bg;
    for (uint8_t col = 0; col < 6; col++) {
        uint8_t line = (col == 5) ? 0 : pgm_read_byte(font + c * 5 + col);
        for (uint8_t row = 0; row < 8; row++) {
            const bool on = line & 0x01;
            if (!on && !fillbg) continue;
            fillBox(x + col * size, y + row * size, size, size, on ? fg : bgb);
            line >>= 1;
        }
    }
}

// ---- geometry ----

void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
    const bool steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) { int32_t t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    int32_t dx = x1 - x0, dy = abs(y1 - y0);
    int32_t err = dx >> 1;
    const int32_t ystep = (y0 < y1) ? 1 : -1;
    for (; x0 <= x1; x0++) {
        if (steep) px(y0, x0, c); else px(x0, y0, c);
        err -= dy;
        if (err < 0) { y0 += ystep; err += dx; }
    }
}

void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    hline(x, y, w, c);
    hline(x, y + h - 1, w, c);
    vline(x, y, h, c);
    vline(x + w - 1, y, h, c);
}

// Corner arcs, radius r, at least one pixel so the AA-tolerant corners
// still show on an 8-bit sprite.
void TFT_eSPI::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    hline(x + r, y, w - 2 * r, c);
    hline(x + r, y + h - 1, w - 2 * r, c);
    vline(x, y + r, h - 2 * r, c);
    vline(x + w - 1, y + r, h - 2 * r, c);
    int32_t t = h + w - r - r - 2;
    if (t < 1) t = 1;
    for (int32_t i = 0; i <= r; i++) {
        int32_t dx = i, dy = r - i;
        px(x + dx, y + dy, c);
        px(x + w - 1 - dx, y + dy, c);
        px(x + dx, y + h - 1 - dy, c);
        px(x + w - 1 - dx, y + h - 1 - dy, c);
    }
}

void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    fillBox(x + r, y, w - 2 * r, h, c);
    for (int32_t i = 0; i <= r; i++) {
        fillBox(x + i, y + r - i, w - 2 * i, 1, c);
        fillBox(x + i, y + h - 1 - r + i, w - 2 * i, 1, c);
    }
}

void TFT_eSPI::drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c) {
    if (r < 0) return;
    int32_t f = 1 - r, ddFx = 1, ddFy = -2 * r, xx = 0, yy = r;
    px(x, y + r, c); px(x, y - r, c); px(x + r, y, c); px(x - r, y, c);
    while (xx < yy) {
        if (f >= 0) { yy--; ddFy += 2; f += ddFy; }
        xx++; ddFx += 2; f += ddFx;
        px(x + xx, y + yy, c); px(x - xx, y + yy, c);
        px(x + xx, y - yy, c); px(x - xx, y - yy, c);
        px(x + yy, y + xx, c); px(x - yy, y + xx, c);
        px(x + yy, y - xx, c); px(x - yy, y - xx, c);
    }
}

void TFT_eSPI::fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) {
    if (r < 0) return;
    int32_t f = 1 - r, ddFx = 1, ddFy = -2 * r, xx = 0, yy = r;
    vline(x, y - r, 2 * r + 1, c);
    while (xx < yy) {
        if (f >= 0) { yy--; ddFy += 2; f += ddFy; }
        xx++; ddFx += 2; f += ddFx;
        vline(x - xx, y - yy, 2 * yy + 1, c);
        vline(x + xx, y - yy, 2 * yy + 1, c);
    }
}

void TFT_eSPI::drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
    drawLine(x0, y0, x1, y1, c);
    drawLine(x1, y1, x2, y2, c);
    drawLine(x2, y2, x0, y0, c);
}

static void edgeSpan(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t ymin,
                     int32_t* xs, int32_t* xe) {
    if (y0 > y1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    int32_t dy = y1 - y0;
    if (dy <= 0) return;
    int32_t dx = x1 - x0;
    int32_t ex = dx / dy, ey = dx % dy;
    int32_t f = 0;
    for (int32_t y = y0; y < y1; y++) {
        int32_t yy = y - ymin;
        if (yy >= 0) {
            if (xs[yy] > x0) xs[yy] = x0;
            if (xe[yy] < x0) xe[yy] = x0;
        }
        f += ey;
        if (f >= dy) { f -= dy; x0 += ex + 1; } else x0 += ex;
    }
}

void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
    int32_t ymin = min(min(y0, y1), y2);
    int32_t ymax = max(max(y0, y1), y2);
    int32_t n = ymax - ymin + 1;
    if (n < 1) return;
    int32_t* xs = (int32_t*)malloc(n * sizeof(int32_t));
    int32_t* xe = (int32_t*)malloc(n * sizeof(int32_t));
    if (!xs || !xe) { free(xs); free(xe); return; }
    for (int32_t i = 0; i < n; i++) { xs[i] = 0x7FFFFFFF; xe[i] = 0x80000000; }
    edgeSpan(x0, y0, x1, y1, ymin, xs, xe);
    edgeSpan(x1, y1, x2, y2, ymin, xs, xe);
    edgeSpan(x2, y2, x0, y0, ymin, xs, xe);
    for (int32_t y = 0; y < n; y++)
        if (xs[y] <= xe[y]) hline(xs[y], y + ymin, xe[y] - xs[y] + 1, c);
    free(xs); free(xe);
}

static inline void ellipsePts(TFT_eSPI* g, int32_t x, int32_t y, int32_t xx, int32_t yy, uint32_t c) {
    g->px(x + xx, y + yy, c); g->px(x - xx, y + yy, c);
    g->px(x + xx, y - yy, c); g->px(x - xx, y - yy, c);
}

void TFT_eSPI::drawEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c) {
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    if (rx == 0 || ry == 0) return;
    int32_t dx = 0, dy = ry;
    int32_t rx2 = 2 * rx * rx, ry2 = 2 * ry * ry;
    int32_t xchg = 0, ychg = ry / rx;
    int32_t err = 0;
    ellipsePts(this, x, y, dx, dy, c);
    while (dx < rx) {
        dx++; err += ry2; xchg++;
        if (2 * err * rx >= ry * ry * (2 * xchg - 1)) { dy--; err -= rx2; ychg++; }
        ellipsePts(this, x, y, dx, dy, c);
    }
    while (dy > 0) {
        dy--; err += rx2; ychg++;
        if (2 * err * ry >= rx * rx * (2 * ychg - 1)) { dx--; err -= ry2; xchg++; }
        ellipsePts(this, x, y, dx, dy, c);
    }
}

void TFT_eSPI::fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c) {
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    if (rx == 0 || ry == 0) return;
    vline(x, y - ry, 2 * ry + 1, c);
    int32_t dx = 0, dy = ry;
    int32_t rx2 = 2 * rx * rx, ry2 = 2 * ry * ry;
    int32_t xchg = 0, ychg = ry / rx;
    int32_t err = 0;
    while (dx < rx) {
        dx++; err += ry2; xchg++;
        if (2 * err * rx >= ry * ry * (2 * xchg - 1)) { dy--; err -= rx2; ychg++; }
        vline(x + dx, y - dy, 2 * dy + 1, c);
        vline(x - dx, y - dy, 2 * dy + 1, c);
        if (dy <= 0) { vline(x + dx, y, 1, c); vline(x - dx, y, 1, c); }
    }
}

// A wide line as a swept rectangle: step along the line, stamp a
// perpendicular segment of length 2*wd at each pixel. Sign-corrected so
// the sweep follows the line direction rather than bouncing along it.
void TFT_eSPI::drawWideLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, float wd, uint32_t c) {
    int32_t dx = x1 - x0, dy = y1 - y0;
    const float L = (float)sqrt((double)(dx * dx + dy * dy));
    if (L < 0.5f) {
        const int32_t w = (int32_t)wd;
        fillBox(x0 - w, y0 - w, 2 * w + 1, 2 * w + 1, c);
        return;
    }
    int32_t rx = (int32_t)(-dy * wd / L), ry = (int32_t)(dx * wd / L);
    if (rx == 0 && ry == 0) rx = 1;
    const int32_t xl = (dx > 0) ? 1 : -1, yl = (dy > 0) ? 1 : -1;
    int32_t x = x0, y = y0;
    int32_t err = 0;
    while (x != x1 || y != y1) {
        fillBox(x - rx, y - ry, 2 * rx + 1, 2 * ry + 1, c);
        const int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += xl; }
        if (e2 < dx)  { err += dx; y += yl; }
    }
    fillBox(x - rx, y - ry, 2 * rx + 1, 2 * ry + 1, c);
}

void TFT_eSPI::drawArc(int32_t x, int32_t y, int32_t r, int32_t ir,
                       uint16_t start, uint16_t end, uint32_t color, uint32_t bg, bool) {
    // Polar walk over the annulus. A step of 1/degree up to r=240 keeps
    // consecutive pixels adjacent; the bresenham-free pie leaves a few
    // pixel-sized gaps the sweep fills.
    const float toRad = 0.017453292519943295f;  // PI / 180
    for (int32_t rr = ir; rr <= r; rr++) {
        for (int32_t a = start; a <= end; a++) {
            const float rad = a * toRad;
            px(x + (int32_t)(rr * cosf(rad)), y + (int32_t)(rr * sinf(rad)), color);
        }
    }
    for (int32_t rr = ir; rr <= r; rr++) {
        for (int32_t a = end + 1; a < start + 360; a++) {
            const float rad = a * toRad;
            px(x + (int32_t)(rr * cosf(rad)), y + (int32_t)(rr * sinf(rad)), bg);
        }
    }
}

// ---- sprite ----

bool TFT_eSprite::createSprite(int16_t w, int16_t h) {
    if (_created) return true;
    if (w < 1 || h < 1) return false;
    _iwidth = w; _iheight = h;
    _dwidth = w; _dheight = h;
    _bitwidth = w;
    _bpp = 8;
    _img8 = (uint8_t*)ps_malloc((size_t)w * h);
    if (!_img8) { _created = false; _iwidth = _iheight = 0; return false; }
    memset(_img8, 0, (size_t)w * h);
    _created = true;
    resetViewport();
    return true;
}

void TFT_eSprite::deleteSprite() {
    if (_img8) free(_img8);
    _img8 = nullptr;
    _created = false;
    _iwidth = _iheight = _dwidth = _dheight = _bitwidth = 0;
}

void TFT_eSprite::setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum) {
    _xDatum = x; _yDatum = y;
    _xWidth = w; _yHeight = h;
    _vpDatum = false; _vpOoB = false;
    _vpX = 0; _vpY = 0;
    _vpW = _iwidth; _vpH = _iheight;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _iwidth)  w = _iwidth  - x;
    if (y + h > _iheight) h = _iheight - y;
    if (w < 1 || h < 1) {
        _vpOoB = true;
        _xDatum = _yDatum = 0;
        _xWidth = _iwidth; _yHeight = _iheight;
        return;
    }
    if (!vpDatum) { _xDatum = 0; _yDatum = 0; _xWidth = _iwidth; _yHeight = _iheight; }
    _vpX = x; _vpY = y; _vpW = x + w; _vpH = y + h;
    _vpDatum = vpDatum;
}

void TFT_eSprite::px(int32_t x, int32_t y, uint32_t c) {
    if (!_created || _vpOoB) return;
    x += _xDatum; y += _yDatum;
    if (x < _vpX || y < _vpY || x >= _vpW || y >= _vpH) return;
    _img8[x + y * _iwidth] = to8(c);
}

void TFT_eSprite::hline(int32_t x, int32_t y, int32_t w, uint32_t c) {
    if (!_created || _vpOoB) return;
    x += _xDatum; y += _yDatum;
    if (y < _vpY || y >= _vpH || x >= _vpW) return;
    if (x < _vpX) { w += x - _vpX; x = _vpX; }
    if (x + w > _vpW) w = _vpW - x;
    if (w < 1) return;
    memset(_img8 + x + y * _iwidth, to8(c), w);
}

void TFT_eSprite::vline(int32_t x, int32_t y, int32_t h, uint32_t c) {
    if (!_created || _vpOoB) return;
    x += _xDatum; y += _yDatum;
    if (x < _vpX || x >= _vpW || y >= _vpH) return;
    if (y < _vpY) { h += y - _vpY; y = _vpY; }
    if (y + h > _vpH) h = _vpH - y;
    if (h < 1) return;
    uint8_t* p = _img8 + x + y * _iwidth;
    const uint8_t c8 = to8(c);
    while (h--) { *p = c8; p += _iwidth; }
}

void TFT_eSprite::fillBox(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    if (!_created || _vpOoB) return;
    x += _xDatum; y += _yDatum;
    if (x >= _vpW || y >= _vpH) return;
    if (x < _vpX) { w += x - _vpX; x = _vpX; }
    if (y < _vpY) { h += y - _vpY; y = _vpY; }
    if (x + w > _vpW) w = _vpW - x;
    if (y + h > _vpH) h = _vpH - y;
    if (w < 1 || h < 1) return;
    uint8_t* p = _img8 + x + y * _iwidth;
    const uint8_t c8 = to8(c);
    while (h--) { memset(p, c8, w); p += _iwidth; }
}

uint16_t TFT_eSprite::readPixel(int32_t x, int32_t y) {
    if (!_created || _vpOoB) return 0;
    x += _xDatum; y += _yDatum;
    if (x < _vpX || y < _vpY || x >= _vpW || y >= _vpH) return 0;
    return from8(_img8[x + y * _iwidth]);
}

uint16_t TFT_eSprite::from8(uint8_t c) {
    // The inverse of to8(): this puts each 8-bit group back in the same
    // lane it was pulled from, so 8-bit red stays red and white stays
    // white. The byte layout is B in bits 0-1, G in 2-4, R in 5-7.
    return (uint16_t)((c & 0xE0) << 8 | (c & 0x1C) << 6 | (c & 0x03) << 3);
}

// The library's window state, used by drawWedgeLine via the line-drawing
// fast paths. Clamps to the buffer; a fully-clipped window points at the
// spare pixel at _dheight (index 0, row == height), which the sprite's
// writers ignore via the bounds check.
void TFT_eSprite::setWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; }
    if (x0 >= _iwidth || x1 < 0 || y0 >= _iheight || y1 < 0) {
        _xs = 0; _ys = _dheight; _xe = 0; _ye = _dheight;
    } else {
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= _iwidth)  x1 = _iwidth  - 1;
        if (y1 >= _iheight) y1 = _iheight - 1;
        _xs = x0; _ys = y0; _xe = x1; _ye = y1;
    }
    _xptr = _xs; _yptr = _ys;
}

void TFT_eSprite::pushColor(uint16_t c) {
    if (!_created) return;
    if (_yptr < _dheight && _xptr < _iwidth)
        _img8[_xptr + _yptr * _iwidth] = to8(c);
    _xptr++;
    if (_xptr > _xe) { _xptr = _xs; _yptr++; }
}

void TFT_eSprite::pushColor(uint16_t c, uint32_t n) {
    while (n--) pushColor(c);
}

// 8bpp -> RGB565 straight into the panel framebuffer, two pixels per
// word. PSRAM-bound (~25 ms at 480x480): the panel refresh reads the
// same PSRAM constantly, and moving this to the other core only made
// the app's own drawing slower.
static uint16_t s_lut[256];
static bool s_lutReady = false;

// Screen (x,y) at rotation r lands on native (X,Y) the same way
// Arduino_GFX and the app's touch fit map it: r1 (W-1-y, x), r2
// (W-1-x, H-1-y), r3 (y, H-1-x). Rotation 0 stays a straight row blit.
static void convertFrame(const uint8_t* img, int32_t w, int32_t h, int32_t x, int32_t y, uint8_t rot) {
    uint16_t* fb = s_panel->getFramebuffer();
    if (!fb) return;
    const int32_t W = 480, H = 480;
    if ((rot & 1) && x == 0 && y == 0 && w == W && h == H) {
        // 90/270: 32x32 tiles, so the source rows stay in cache while
        // each destination row is written contiguously.
        for (int32_t ty0 = 0; ty0 < H; ty0 += 32) {
            for (int32_t tx0 = 0; tx0 < W; tx0 += 32) {
                for (int32_t sx = tx0; sx < tx0 + 32; sx++) {
                    const uint8_t* s = img + ty0 * w + sx;
                    if (rot == 1) {
                        uint16_t* d = fb + sx * W + (W - 1 - ty0);
                        for (int32_t k = 0; k < 32; k++, s += w) d[-k] = s_lut[*s];
                    } else {
                        uint16_t* d = fb + (H - 1 - sx) * W + ty0;
                        for (int32_t k = 0; k < 32; k++, s += w) d[k] = s_lut[*s];
                    }
                }
            }
        }
        s_panel->flush(true);
        return;
    }
    for (int32_t yy = 0; yy < h; yy++) {
        const int32_t sy = y + yy;
        if (sy < 0 || sy >= H) continue;
        const uint8_t* src = img + yy * w;
        int32_t x0 = 0, x1 = w;
        if (x < 0) x0 = -x;
        if (x + x1 > W) x1 = W - x;
        if (rot == 0) {
            uint16_t* dst = fb + sy * W + x;
            int32_t xx = x0;
            if (((uintptr_t)(dst + xx) & 3) && xx < x1) { dst[xx] = s_lut[src[xx]]; xx++; }
            uint32_t* d32 = (uint32_t*)(dst + xx);
            for (; xx + 1 < x1; xx += 2)
                *d32++ = (uint32_t)s_lut[src[xx]] | ((uint32_t)s_lut[src[xx + 1]] << 16);
            if (xx < x1) dst[xx] = s_lut[src[xx]];
        } else if (rot == 2) {
            uint16_t* dst = fb + (H - 1 - sy) * W;
            for (int32_t xx = x0; xx < x1; xx++) dst[W - 1 - (x + xx)] = s_lut[src[xx]];
        } else if (rot == 1) {
            uint16_t* dst = fb + (W - 1 - sy);
            for (int32_t xx = x0; xx < x1; xx++) dst[(x + xx) * W] = s_lut[src[xx]];
        } else {
            uint16_t* dst = fb + sy;
            for (int32_t xx = x0; xx < x1; xx++) dst[(H - 1 - (x + xx)) * W] = s_lut[src[xx]];
        }
    }
    s_panel->flush(true);
}

void TFT_eSprite::pushSprite(int16_t x, int16_t y) {
    if (!_created || !s_panel) return;
    if (!s_lutReady) { for (int i = 0; i < 256; i++) s_lut[i] = from8((uint8_t)i); s_lutReady = true; }
    convertFrame(_img8, _iwidth, _iheight, x, y, s_screenRot);
}