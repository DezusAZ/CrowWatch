// SquachWatch-CYD — pushing the frame buffer without waiting on the wire.
//
// Every screen is drawn into an 8-bit sprite and shipped to the panel by
// TFT_eSPI's own 8-bit push: convert one line of 8-bit colour into the 16-bit
// words the panel wants, hand it to the SPI hardware 64 bytes at a time,
// and SPIN while each 64 bytes goes out. Then the next line. The wire idles
// while a line converts, and the CPU idles while a line transmits -- about
// 7 ms a frame of each waiting on the other, on top of the 15.4 ms (80 MHz)
// or 30.7 ms (40 MHz) the bytes themselves cost.
//
// This reorders that loop and nothing else: kick 64 bytes, and convert the
// NEXT 64 bytes while they go out instead of spinning. Same registers, same
// bus, same chip select and transaction the library's push already holds --
// it is the library's loop with the wait put to use, not a second driver.
// (A second driver is what the DMA attempt was, and the engine never
// completed a single transfer under it. That path is closed; this is the
// other way round to the same 7 ms.)
//
// Gated to the 2.8" CYDs to start with, by agreement, not by mechanism: there
// is nothing here the other boards' bus could not do, and it can be widened
// once it has proven itself on the two boards it can be measured on.
#pragma once
#include <Arduino.h>

class TFT_eSPI;
class TFT_eSprite;

// The 2.8" CYDs, both panel types, fast and slow. -DCYD is also set by the
// two RL Phantom builds, so they are named out rather than assumed away.
#if defined(ARDUINO_ARCH_ESP32) && defined(CYD) && !defined(CYD35) && \
    !defined(AWOK) && !defined(RLPHANTOM) && !defined(RLPHANTOM_R)
  #define SQW_FRAME_PUSH 1
#else
  #define SQW_FRAME_PUSH 0
#endif

namespace FramePush {

// One pixel of 8-bit colour as the 16 bits that go on the wire, in WIRE
// order: the panel wants the high byte first, and the SPI hardware sends a
// word's bytes lowest address first, so the high byte sits in the low half.
// This is TFT_eSPI's own 8-bit arithmetic, character for character, and it
// lives out here so a host test can check all 256 of them -- a version that
// differs by one bit shifts every colour on the device slightly and nothing
// would look obviously wrong.
//
// Blue has four levels, not eight: two bits in an 8-bit colour. That is why
// the near-neutrals on this device are the handful they are, and the table
// is the library's own rather than derived, so the two can never drift.
inline uint16_t rgb332Wire(uint8_t c) {
    static const uint8_t BLUE4[4] = { 0, 11, 21, 31 };
    //                        --- green ---     ------------ red ------------
    const uint8_t hi = (uint8_t)((c & 0x1C) >> 2 | (c & 0xC0) >> 3 | (c & 0xE0));
    //                        --- green ---    --- blue ---
    const uint8_t lo = (uint8_t)((c & 0x1C) << 3 | BLUE4[c & 0x03]);
    return (uint16_t)(hi | (lo << 8));
}

// Builds the lookup table. Once, in setup(), any time after the display is
// up. Returns false on a board this is not built for, and push() then
// declines every frame and the ordinary push carries on.
bool begin();

// True once begin() has succeeded: this build can do it at all.
bool available();

// The switch. Separate from available(), so a board that draws strangely can
// be put back on the ordinary push over the console without reflashing.
void setEnabled(bool on);
bool enabled();

// Ships the sprite. Returns false if it declined -- wrong board, switched
// off, not an 8-bit sprite, no buffer (a rotate can lose it), a size that
// does not divide into the hardware's 64-byte bursts -- and the caller must
// then push it the ordinary way. Never partially draws: it either does the
// whole frame or touches nothing.
bool push(TFT_eSPI& tft, TFT_eSprite& spr, int32_t x, int32_t y);

}  // namespace FramePush
