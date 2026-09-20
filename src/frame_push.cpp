#include "frame_push.h"
#include <TFT_eSPI.h>

namespace FramePush {

#if SQW_FRAME_PUSH

// 8-bit colour in, the 16 bits that go on the wire out. 512 bytes, built once
// from rgb332Wire() so the table and the tested arithmetic are the same thing.
static uint16_t s_lut[256];

// The SPI hardware takes 64 bytes at a time: 32 pixels, 16 words. This is
// the library's own burst size, fixed by the FIFO, not a tuning knob.
static const uint32_t PX_PER_BURST = 32;

// ---- what the panel already shows ----
//
// One hash per row of the last frame that went out. A row whose hash has not
// changed is not sent: the panel keeps what it has. On a menu that is every
// row, and the push costs nothing; on the main screen it is the title bar,
// the counters and the buttons -- about a third -- repainted identically
// every frame by code that does not know they did not change.
//
// Rows are remembered by their PANEL row, not their row within the sprite.
// The 3.5" draws its main screen in two bands -- one half-height sprite
// pushed twice, at the top of the screen and then halfway down -- and a
// record indexed within the sprite would have the second band reading the
// first band's rows and skipping whatever happened to match. Indexing by
// (y + row) gives each band its own half of the record and costs nothing.
//
// The record is only as good as its last full push, so it is thrown away
// (invalidate()) whenever anything else could have touched the panel -- a
// rotation, a fall-back to the ordinary push -- and every 64th frame is a
// full one regardless, so nothing that slips past that can stay on screen
// for more than three seconds.
//
// Stretching that to one in 192 was tried and put back. It is worth about a
// third of a millisecond a frame on the 3.5" -- one percent -- and it costs
// every board a stale-row window three times longer: six seconds on a 2.8"
// running at thirty frames a second, where today it is two. That is a bad
// trade to make on five boards for one percent on the sixth.
static const int32_t MAX_ROWS = 480;   // the 3.5" in portrait, the tallest there is
static uint32_t s_rowHash[MAX_ROWS];
static bool     s_valid   = false;
static int32_t  s_validW  = 0, s_validH = 0;
static uint32_t s_frameNo = 0;
static int32_t  s_lastRows = 0;
#if defined(CYD35)
static uint32_t s_hashUs = 0, s_wireUs = 0;
#define SQW_PUSH_CLOCK(v) const uint32_t v = micros()
#define SQW_PUSH_CHARGE(acc, v) acc += micros() - (v)
#else
#define SQW_PUSH_CLOCK(v) ((void)0)
#define SQW_PUSH_CHARGE(acc, v) ((void)0)
#endif

static bool s_ready   = false;
static bool s_enabled = true;

bool begin() {
    if (s_ready) return true;
    for (int i = 0; i < 256; i++) s_lut[i] = rgb332Wire((uint8_t)i);
    s_ready = true;
    Serial.println("[push] overlapped frame push on, unchanged rows skipped");
    return true;
}

bool available() { return s_ready; }
void setEnabled(bool on) { s_enabled = on; }
bool enabled() { return s_ready && s_enabled; }
void invalidate() { s_valid = false; }
#if defined(CYD35)
void newFrame() { s_lastRows = 0; s_hashUs = 0; s_wireUs = 0; }
uint32_t hashUs() { return s_hashUs; }
uint32_t wireUs() { return s_wireUs; }
#else
void newFrame() { s_lastRows = 0; }
uint32_t hashUs() { return 0; }
uint32_t wireUs() { return 0; }
#endif
int32_t lastRows() { return s_lastRows; }

// FNV-1a over a row, a word at a time. Rows are a multiple of four bytes on
// every board this is built for, and the sprite buffer is word-aligned.
static inline uint32_t rowHash(const uint8_t* row, int32_t w) {
    const uint32_t* p = (const uint32_t*)row;
    uint32_t h = 2166136261u;
    for (int32_t n = w >> 2; n > 0; n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

// 32 pixels into 16 words, in the order the FIFO sends them: two pixels per
// word, the first pixel in the low half.
static inline void convertBurst(const uint8_t* p, uint32_t* words) {
    for (uint32_t i = 0; i < PX_PER_BURST / 2; i++) {
        words[i] = (uint32_t)s_lut[p[0]] | ((uint32_t)s_lut[p[1]] << 16);
        p += 2;
    }
}

// `total` pixels from `p`, already inside a window, in bursts -- the next
// burst converts while the previous one is on the wire.
static void pushPixels(const uint8_t* p, uint32_t total) {
    uint32_t words[PX_PER_BURST / 2];
    const uint8_t* end = p + total;
    convertBurst(p, words);
    p += PX_PER_BURST;
    for (;;) {
        while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_USR) {}
        for (uint32_t i = 0; i < PX_PER_BURST / 2; i++)
            WRITE_PERI_REG(SPI_W0_REG(SPI_PORT) + (i << 2), words[i]);
        SET_PERI_REG_MASK(SPI_CMD_REG(SPI_PORT), SPI_USR);
        if (p >= end) break;
        convertBurst(p, words);
        p += PX_PER_BURST;
    }
    while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_USR) {}
}

static uint32_t gcd32(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }

bool push(TFT_eSPI& tft, const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y) {
    if (!s_ready || !s_enabled) return false;
    // Null when the sprite was never created, or was lost to a failed
    // re-create after a rotate. The caller's fallback handles it -- and
    // handles it by doing nothing, same as pushSprite() would.
    if (!src) return false;
    if (w <= 0 || h <= 0 || y < 0 || y + h > MAX_ROWS || (w & 3)) return false;
    const uint32_t total = (uint32_t)w * (uint32_t)h;
    if (total % PX_PER_BURST != 0) return false;

    // A span has to be whole bursts: 320 wide, any row is; 240 wide, two.
    const int32_t align = (int32_t)(PX_PER_BURST / gcd32((uint32_t)w, PX_PER_BURST));

    // A frame is every band of it, so the periodic full refresh counts pushes
    // rather than frames: 64 pushes is 64 frames on one band and 32 on two.
    const bool full = !s_valid || s_validW != w || s_validH != h || (s_frameNo % 64) == 0;
    s_frameNo++;

    // Hash every row, and mark the ones to send. Full: all of them.
    static bool changed[MAX_ROWS];
    int32_t nChanged = 0;
    SQW_PUSH_CLOCK(tHash);
    for (int32_t r = 0; r < h; r++) {
        const uint32_t hv = rowHash(src + (size_t)r * (size_t)w, w);
        changed[r] = full || hv != s_rowHash[y + r];
        s_rowHash[y + r] = hv;
        if (changed[r]) nChanged++;
    }
    SQW_PUSH_CHARGE(s_hashUs, tHash);
    if (nChanged) {
        SQW_PUSH_CLOCK(tWire);
        // Exactly what pushSprite() -> pushImage() does around its own loop:
        // one transaction, chip select held low, the data/command line left
        // on data after each window. 512 bits per kick, as the library does.
        static RowSpan spans[96];
        int n = frameSpans(changed, h, align, spans, 96);
        // Belt and braces: a span that is not whole bursts (it cannot be,
        // for the sizes this is built for) becomes a full push.
        for (int i = 0; i < n; i++)
            if (((uint32_t)(spans[i].r1 - spans[i].r0) * (uint32_t)w) % PX_PER_BURST) { spans[0].r0 = 0; spans[0].r1 = h; n = 1; break; }
        tft.startWrite();
        for (int i = 0; i < n; i++) {
            const int32_t r0 = spans[i].r0, r1 = spans[i].r1;
            tft.setAddrWindow(x, y + r0, w, r1 - r0);
            WRITE_PERI_REG(SPI_MOSI_DLEN_REG(SPI_PORT), 511);
            pushPixels(src + (size_t)r0 * (size_t)w, (uint32_t)(r1 - r0) * (uint32_t)w);
            s_lastRows += r1 - r0;
        }
        tft.endWrite();
        SQW_PUSH_CHARGE(s_wireUs, tWire);
    }
    s_valid  = true;
    s_validW = w;
    s_validH = h;
    return true;
}

#else  // every other board, and the emulator: built out, not switched off

bool begin() { return false; }
bool available() { return false; }
void setEnabled(bool) {}
bool enabled() { return false; }
void invalidate() {}
int32_t lastRows() { return 0; }
bool push(TFT_eSPI&, const uint8_t*, int32_t, int32_t, int32_t, int32_t) { return false; }

#endif

}  // namespace FramePush
