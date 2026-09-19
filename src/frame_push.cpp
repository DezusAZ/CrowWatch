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

static bool s_ready   = false;
static bool s_enabled = true;

bool begin() {
    if (s_ready) return true;
    for (int i = 0; i < 256; i++) s_lut[i] = rgb332Wire((uint8_t)i);
    s_ready = true;
    Serial.println("[push] overlapped frame push on");
    return true;
}

bool available() { return s_ready; }
void setEnabled(bool on) { s_enabled = on; }
bool enabled() { return s_ready && s_enabled; }

// 32 pixels into 16 words, in the order the FIFO sends them: two pixels per
// word, the first pixel in the low half.
static inline void convertBurst(const uint8_t* p, uint32_t* words) {
    for (uint32_t i = 0; i < PX_PER_BURST / 2; i++) {
        words[i] = (uint32_t)s_lut[p[0]] | ((uint32_t)s_lut[p[1]] << 16);
        p += 2;
    }
}

bool push(TFT_eSPI& tft, TFT_eSprite& spr, int32_t x, int32_t y) {
    if (!s_ready || !s_enabled) return false;
    if (spr.getColorDepth() != 8) return false;

    const int32_t w = spr.width();
    const int32_t h = spr.height();
    if (w <= 0 || h <= 0) return false;
    const uint32_t total = (uint32_t)w * (uint32_t)h;
    // A frame that does not divide into whole bursts would need the
    // library's remainder handling; every frame this board pushes does
    // divide, so decline the odd one out rather than carry a second path.
    if (total % PX_PER_BURST != 0) return false;

    // Null when the sprite was never created, or was lost to a failed
    // re-create after a rotate. The caller's fallback handles it -- and
    // handles it by doing nothing, same as pushSprite() would.
    const uint8_t* p = (const uint8_t*)spr.getPointer();
    if (!p) return false;

    // Exactly what pushSprite() -> pushImage() does around its own loop:
    // one transaction, chip select held low across the whole frame, the
    // address window and RAMWR sent, the data/command line left on data.
    tft.startWrite();
    tft.setAddrWindow(x, y, w, h);

    // The library's own setup for its 64-byte bursts: 512 bits per kick.
    WRITE_PERI_REG(SPI_MOSI_DLEN_REG(SPI_PORT), 511);

    uint32_t words[PX_PER_BURST / 2];
    const uint8_t* end = p + total;
    convertBurst(p, words);
    p += PX_PER_BURST;

    for (;;) {
        // Wait for the previous burst to finish leaving the FIFO -- the one
        // wait in the loop, and by the time it is reached the next burst
        // has already been converted, so it is a wait on the wire alone.
        while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_USR) {}
        for (uint32_t i = 0; i < PX_PER_BURST / 2; i++)
            WRITE_PERI_REG(SPI_W0_REG(SPI_PORT) + (i << 2), words[i]);
        SET_PERI_REG_MASK(SPI_CMD_REG(SPI_PORT), SPI_USR);

        if (p >= end) break;
        // The whole point: these 32 pixels convert while the 32 before them
        // are still going out.
        convertBurst(p, words);
        p += PX_PER_BURST;
    }
    while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_USR) {}

    tft.endWrite();
    return true;
}

#else  // every other board, and the emulator: built out, not switched off

bool begin() { return false; }
bool available() { return false; }
void setEnabled(bool) {}
bool enabled() { return false; }
bool push(TFT_eSPI&, TFT_eSprite&, int32_t, int32_t) { return false; }

#endif

}  // namespace FramePush
