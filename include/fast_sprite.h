// SquachWatch-CYD — the frame buffer's slow primitives, done directly.
//
// Every screen draws into one 8-bit sprite. Measured on the board (PRIM on
// the console), the library's horizontal line and filled rectangle are fine
// -- a memset -- but three things are not:
//
//   drawPixel       0.29 us each: fine alone, ruinous when something plots
//                   pixel by pixel, which the next two do
//   drawFastVLine   80 ns a pixel for a plain store, because its loop
//                   re-multiplies the row stride every pixel (the compiler
//                   cannot prove the store does not change the stride)
//   drawLine        35 us for a hundred pixels: one fully clipped call per
//                   run of one or two pixels
//   text            19 us a character at size 1, 50 at size 2: 48 drawPixel
//                   calls, or 48 fillRects, per character of the built-in
//                   font -- and this device is mostly text
//
// All of those are virtual, so this subclass takes them over for the one
// sprite the firmware has: same clipping, same viewport datum, same colour
// conversion, same Bresenham, same font table, but writing bytes into the
// buffer with a local pointer and stride instead of going through a clipped
// call per pixel. Anything that is not the plain 8-bit case -- another depth,
// a custom font, a character or line that crosses the viewport edge -- is
// handed straight back to the library, which is how the clipping stays its
// clipping rather than a second implementation of it.
//
// selfCheck() is the proof: it draws the same patterns through the library
// path and this one, edge cases and a viewport included, and compares
// checksums of the whole buffer. PRIM on the console runs it.
//
// The emulator's sprite is a different class with none of these members, so
// there this is a plain pass-through and nothing changes.
#pragma once
#include <TFT_eSPI.h>

class FastSprite : public TFT_eSprite {
public:
    explicit FastSprite(TFT_eSPI* tft) : TFT_eSprite(tft) {}

#if defined(ARDUINO_ARCH_ESP32)
    // FAST OFF on the console: every override hands straight to the library,
    // so the two can be compared on one boot.
    static void setFast(bool on) { s_fast = on; }
    static bool fast() { return s_fast; }

    void drawPixel(int32_t x, int32_t y, uint32_t color) override;
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) override;
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) override;
    void drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) override;

    // The library's own versions, reachable for the self-check and for PRIM
    // to time side by side. (The library's line and character still call
    // drawPixel and friends virtually, so they run on THIS class's
    // primitives; the pixel and vertical line are the pure originals, and
    // they are checked first for that reason.)
    void basePixel(int32_t x, int32_t y, uint32_t c)                            { TFT_eSprite::drawPixel(x, y, c); }
    void baseVLine(int32_t x, int32_t y, int32_t h, uint32_t c)                 { TFT_eSprite::drawFastVLine(x, y, h, c); }
    void baseLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c)   { TFT_eSPI::drawLine(x0, y0, x1, y1, c); }
    void baseChar(int32_t x, int32_t y, uint16_t ch, uint32_t c, uint32_t bg, uint8_t s) { TFT_eSprite::drawChar(x, y, ch, c, bg, s); }

    // FNV-1a over the whole buffer.
    uint32_t checksum() const;

    // Draws every pattern twice, library then fast, and compares. Prints one
    // line per primitive; returns how many differed. Scribbles on the
    // sprite, which the next frame repaints.
    int selfCheck(Print& out);

private:
    static bool s_fast;
#endif
};
