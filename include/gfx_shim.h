// SquachWatch-CYD — TFT_eSPI-compatible layer over Arduino_GFX
// The app code is written against TFT_eSPI/TFT_eSprite. This board's panel
// is a parallel-RGB ST7701 that Arduino_GFX drives (and TFT_eSPI cannot),
// so when ESP32_4848S040 is defined this header IS TFT_eSPI.h: a full-screen
// class and an 8-bit sprite class with the same names and the API surface
// the app uses, both drawing to the same underlying primitives. All drawing
// funnels through four virtuals (px/hline/vline/fillBox); the full-screen
// instance forwards them to the panel, the sprite writes a 230KB PSRAM
// buffer that pushSprite() ships to the panel as one RGB565 frame.
#pragma once

#include <Arduino.h>
#include <Print.h>
#include <stdlib.h>
#include <string.h>

#include <Arduino_GFX_Library.h>
// The library defines WHITE/BLACK/NAVY/... as object-like macros that
// expand to RGB565(...), which collides with the app's own constexpr
// color names (theme.h: `constexpr uint16_t WHITE = 0xFFFF;`). The app
// has its own palette; the library's macros are not used.
#undef BLACK
#undef WHITE
#undef NAVY
#undef DARKGREEN
#undef DARKCYAN
#undef MAROON
#undef PURPLE
#undef OLIVE
#undef LIGHTGREY
#undef DARKGREY
#undef BLUE
#undef GREEN
#undef CYAN
#undef RED
#undef MAGENTA
#undef YELLOW
#undef ORANGE
#undef GREENYELLOW
#undef PINK
#undef PALERED
#undef RGB565
#undef RGB16TO24

#include "esp_heap_caps.h"

#define TFT_BLACK 0x0000
#define TFT_NAVY   0x000F
#define TFT_DARKGREEN 0x03E0
#define TFT_DARKCYAN 0x03EF
#define TFT_MAROON 0x7800
#define TFT_PURPLE 0x780F
#define TFT_OLIVE 0x7BE0
#define TFT_LIGHTGREY 0xC618
#define TFT_DARKGREY 0x4208
#define TFT_ORANGE 0xFD20
#define TFT_GREEN 0x07E0
#define TFT_CYAN 0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_RED 0xF800
#define TFT_YELLOW 0xFFE0
#define TFT_WHITE 0xFFFF

// The two built-in fonts (GLCD 6x8 and the 16-row proportional face).
#include "../src/gfx_fonts.h"

// The panel every shim object draws to.
Arduino_GFX* gfxPanel();
void gfxInit();
// Raw command/data writes to the panel's bus (MADCTL for color order).
void gfxWriteCommand(uint8_t c);
void gfxWriteData(uint8_t d);

// A thick line (TFT_eSPI drawWideLine replacement; width wd = radius).
void drawWideLine(Arduino_GFX* g, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                  float wd, uint16_t color);

class TFT_eSPI : public Print {
public:
    // Print needs this to exist regardless of redirect target.
    size_t write(uint8_t) { return 1; }
    using Print::write;

    TFT_eSPI() { reset(); }
    void init() { gfxInit(); }
    virtual ~TFT_eSPI() {}

    // ---- the four targets every drawing call finally reaches ----
    // The root class IS the panel-backed screen; the sprite overrides
    // all four to write its buffer instead.
    virtual void px(int32_t x, int32_t y, uint32_t c);
    virtual void hline(int32_t x, int32_t y, int32_t w, uint32_t c);
    virtual void vline(int32_t x, int32_t y, int32_t h, uint32_t c);
    virtual void fillBox(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);

    // ---- primitives ----
    virtual void drawPixel(int32_t x, int32_t y, uint32_t c) { px(x, y, c); }
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { hline(x, y, w, c); }
    virtual void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { vline(x, y, h, c); }
    virtual void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) { fillBox(x, y, w, h, c); }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c,
                  uint8_t d) { (void)d; fillBox(x, y, w, h, c); }
    void fillScreen(uint32_t c) { fillBox(0, 0, _w, _h, c); }
    virtual void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
    void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
    void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
    void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);
    void drawEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c);
    void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c);
    void drawWideLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, float wd, uint32_t c);

    // TFT_eSPI's 9-arg drawArc(x, y, r, ir, start, end, color, bg, smooth):
    // the arc [start..end] in color, the rest of the ring in bg.
    void drawArc(int32_t x, int32_t y, int32_t r, int32_t ir,
                 uint16_t start, uint16_t end, uint32_t color, uint32_t bg, bool);
    void fillScreen(uint16_t c) { fillBox(0, 0, _w, _h, c); }

    // ---- rotation / panel control ----
    void setRotation(uint8_t r);
    uint8_t getRotation() const { return _rot; }
    void invertDisplay(bool i);
    void writecommand(uint8_t c) { gfxWriteCommand(c); }
    void writedata(uint8_t d) { gfxWriteData(d); }

    // Raw resistive-touch accessors (never used on this board; AWOK-shaped
    // callers stay compiling).
    bool getTouchRawZ() { return false; }
    void getTouchRaw(int16_t*, int16_t*) { }
    bool getTouch(int16_t*, int16_t*, uint16_t*) { return false; }

    int16_t width() const { return _w; }
    int16_t height() const { return _h; }

    // Nop'd for the panel-class object (the window-writer fast sprite
    // has its own); the app's touch driver calls these unconditionally.
    void setWindow(int16_t, int16_t, int16_t, int16_t) { }
    void pushColor(uint16_t) { }
    void pushColor(uint16_t, uint32_t) { }
    void pushImage(int32_t, int32_t, int32_t, int32_t, uint8_t*, bool = false) { }

    // ---- text ----
    void setTextSize(uint8_t s);
    void setTextSize(uint8_t sx, uint8_t sy) { setTextSize(sx > sy ? sx : sy); }
    void setTextColor(uint16_t c);
    void setTextColor(uint16_t c, uint16_t b);
    void setTextWrap(bool w) { _wrap = w; }
    void setTextFont(uint8_t f);
    void setFreeFont(const GFXfont* f);
    void setCursor(int16_t x, int16_t y);
    void setCursor(int16_t x, int16_t y, uint8_t f) { setTextFont(f); setCursor(x, y); }
    int16_t getCursorX() { return cursor_x; }
    int16_t getCursorY() { return cursor_y; }
    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
    }
    int16_t textWidth(const char* s);
    int16_t textWidth(const String& s) { return textWidth(s.c_str()); }
    int16_t textWidth(const __FlashStringHelper* s);
    uint8_t textwidth(const char* s) { return (uint8_t)textWidth(s); }
    int16_t fontHeight();
    int16_t fontHeight(uint8_t font) { return font == 2 ? 16 : 8; }

    void print(const char* s) { while (*s) drawChar(*(const uint8_t*)s++); }
    void print(String s) { for (size_t i = 0; i < s.length(); i++) drawChar((uint8_t)s[i]); }
    void print(char c) { drawChar((uint8_t)c); }
    void print(int v) { char b[12]; itoa(v, b, 10); print(b); }
    void print(long v) { char b[16]; ltoa(v, b, 10); print(b); }
    void print(unsigned v) { char b[12]; utoa(v, b, 10); print(b); }
    void print(unsigned long v) { char b[16]; ultoa(v, b, 10); print(b); }
    void print(double v) { dtostrf(v, 0, 2, _fbuf); print(_fbuf); }
    void print(float v) { dtostrf(v, 0, 2, _fbuf); print(_fbuf); }
    void print(const __FlashStringHelper* s) { print((const char*)s); }
    void println() { drawChar('\n'); }
    void println(const char* s) { print(s); println(); }
    void println(String s) { print(s); println(); }
    void println(char c) { print(c); println(); }
    void println(int v) { print(v); println(); }
    void println(long v) { print(v); println(); }
    void println(unsigned v) { print(v); println(); }
    void println(unsigned long v) { print(v); println(); }
    void println(double v) { print(v); println(); }
    void println(float v) { print(v); println(); }
    void println(const __FlashStringHelper* s) { print(s); println(); }
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    void startWrite() { }
    void endWrite() { }
    void setAddrWindow(int16_t, int16_t, int16_t, int16_t) { }

    // Viewport API on the base class too: theme.cpp's backdrops call it
    // through a TFT_eSPI& that may be the screen. On the screen the
    // viewport is the whole panel and never clips anything.
    void setViewport(int32_t, int32_t, int32_t, int32_t, bool vpDatum = false) { (void)vpDatum; }
    void resetViewport() { }
    int32_t getViewportX() { return 0; }
    int32_t getViewportY() { return 0; }
    int32_t getViewportWidth() { return _w; }
    int32_t getViewportHeight() { return _h; }
    bool getViewportDatum() { return false; }
    uint16_t readPixel(int32_t, int32_t) { return 0; }

    // A positioned GLCD glyph with the current text colour, no
    // background (theme.cpp's rain uses it).
    void drawChar(uint16_t c, int32_t x, int32_t y) {
        const uint16_t fg = _fg;
        const bool fb = _fillbg;
        _fillbg = false;
        _fg = fg;
        drawCharCell(x, y, (uint8_t)c, _size);
        _fillbg = fb;
    }

protected:
    void reset();
    void drawChar(uint8_t c);
    // Positioned glyph (GLCD); used by fast_sprite's drawChar override
    // as its fall-back, with an explicit color and background.
    virtual void drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size);
    void drawCharCell(int32_t x, int32_t y, uint8_t c, uint8_t size);
    void drawChar16(int32_t x, int32_t y, uint8_t c, uint8_t size);
    void newLine();

    int16_t cursor_x = 0, cursor_y = 0;
    int16_t _w = 480, _h = 480;
    uint8_t _rot = 0;
    uint16_t _fg = TFT_WHITE, _bg = TFT_BLACK;
    bool _fillbg = false, _wrap = true;
    uint8_t _size = 1, _font = 1;
    const GFXfont* _gfxFont = nullptr;
    char _fbuf[24];
};

class TFT_eSprite : public TFT_eSPI {
public:
    explicit TFT_eSprite(TFT_eSPI*) { reset(); }
    ~TFT_eSprite() { deleteSprite(); }

    bool created() { return _created; }
    uint8_t getColorDepth() { return _bpp; }
    void setColorDepth(uint8_t b) { if (b == 8) _bpp = 8; }
    bool createSprite(int16_t w, int16_t h);
    void deleteSprite();
    void pushSprite(int16_t x, int16_t y);
    void pushSprite(int16_t x, int16_t y, uint16_t) { pushSprite(x, y); }
    void pushSprite(int16_t x, int16_t y, int16_t, int16_t, int16_t, int16_t) { pushSprite(x, y); }
    void setPivot(int16_t x, int16_t y) { _px = x; _py = y; }

    // TFT_eSPI sprite viewport semantics: _vpX/_vpY are the origin and
    // _vpW/_vpH the exclusive END of the viewport (FastSprite's fast path
    // checks against exactly those as bounds).
    void setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum = false);
    void resetViewport() { setViewport(0, 0, _iwidth, _iheight, false); }
    int32_t getViewportX() { return _xDatum; }
    int32_t getViewportY() { return _yDatum; }
    int32_t getViewportWidth() { return _xWidth; }
    int32_t getViewportHeight() { return _yHeight; }
    bool getViewportDatum() { return _vpDatum; }

    // Drawing routed to the PSRAM buffer.
    void px(int32_t x, int32_t y, uint32_t c) override;
    void hline(int32_t x, int32_t y, int32_t w, uint32_t c) override;
    void vline(int32_t x, int32_t y, int32_t h, uint32_t c) override;
    void fillBox(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) override;

    int16_t width() const { return _iwidth; }
    int16_t height() const { return _iheight; }
    uint16_t readPixel(int32_t x, int32_t y);
    const char* fontname() { return "shim"; }

    // Window state for drawWedgeLine's sprite fast path.
    void setWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void pushColor(uint16_t c);
    void pushColor(uint16_t c, uint32_t n);
    int32_t getWindowX() { return _xs; }
    int32_t getWindowY() { return _ys; }

    uint8_t rotation = 0;

protected:
    void spriteClip(int32_t& x, int32_t& y, int32_t& w, int32_t& h);
    static uint8_t to8(uint32_t c) {
        return (uint8_t)((c & 0xE000) >> 8 | (c & 0x0700) >> 6 | (c & 0x0018) >> 3);
    }
    static uint16_t from8(uint8_t c);

    int16_t _iwidth = 0, _iheight = 0;
    int16_t _dwidth = 0, _dheight = 0;
    int16_t _bitwidth = 0;
    uint8_t* _img8 = nullptr;
    uint8_t _bpp = 8;
    bool _created = false;
    bool _vpOoB = false, _vpDatum = false;
    int32_t _vpX = 0, _vpY = 0, _vpW = 0, _vpH = 0;
    int32_t _xDatum = 0, _yDatum = 0;
    int32_t _xs = 0, _ys = 0, _xe = 0, _ye = 0, _xptr = 0, _yptr = 0;
    int32_t _sx = 0, _sy = 0, _sw = 0, _sh = 0;
    int32_t _px = 0, _py = 0;
    bool _cp437 = false;
    int32_t _xWidth = 0, _yHeight = 0;
};