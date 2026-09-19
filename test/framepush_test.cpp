// SquachWatch-CYD — the colours the overlapped frame push puts on the wire.
//
// What this guards: the overlapped push converts 8-bit colour to the panel's
// 16 bits itself, from a table, instead of letting TFT_eSPI do it a pixel at
// a time. A table built from arithmetic that differs by one bit shifts every
// colour on the device slightly; nothing would crash and no screen would look
// obviously wrong, and the only way to notice would be two boards side by
// side. So all 256 of them are checked here rather than by eye.
//
// The reference is derived from what the bits MEAN -- red and green are three
// bits each expanded to five and six, blue is two expanded to five -- rather
// than copied from the same expression the code uses, because a test that
// repeats the code's own arithmetic only proves the file was saved.
#include "test_util.h"
#include "frame_push.h"
#include <cstdint>

// Expand an n-bit field the way a display driver does: shift up and
// replicate the high bits down, so all-ones stays all-ones rather than
// landing just short of full scale.
static uint8_t red5(uint8_t r3)   { return (uint8_t)((r3 << 2) | (r3 >> 1)); }
static uint8_t green6(uint8_t g3) { return (uint8_t)((g3 << 3) | g3); }

// The wire word has the panel's high byte in its LOW half, because the SPI
// hardware sends a word lowest address first. Undo that to read it back as
// an ordinary RGB565 value.
static uint16_t asRgb565(uint16_t wire) {
    return (uint16_t)(((wire & 0xFF) << 8) | (wire >> 8));
}

int main() {
    suite("Every one of the 256 colours");
    {
        int wrongRed = 0, wrongGreen = 0, wrongBlue = 0;
        // The four blue levels are the library's own, and they are NOT the
        // replication the other two channels get: that would give 0/10/21/31
        // and the driver uses 11 for the second one. Named here so the
        // difference is a decision on the record rather than a typo.
        static const uint8_t BLUE5[4] = { 0, 11, 21, 31 };
        for (int i = 0; i < 256; i++) {
            const uint8_t c = (uint8_t)i;
            const uint16_t px = asRgb565(FramePush::rgb332Wire(c));
            if (((px >> 11) & 0x1F) != red5((uint8_t)((c >> 5) & 0x07)))   wrongRed++;
            if (((px >>  5) & 0x3F) != green6((uint8_t)((c >> 2) & 0x07))) wrongGreen++;
            if (( px        & 0x1F) != BLUE5[c & 0x03])                    wrongBlue++;
        }
        ck("red lands where the bits say",   wrongRed   == 0);
        ck("green lands where the bits say", wrongGreen == 0);
        ck("blue lands where the bits say",  wrongBlue  == 0);
    }

    suite("The ends of the scale");
    // Full scale has to stay full scale. An expansion that only shifts leaves
    // white at 248,252,248 -- a faintly dirty white that survives a look.
    ck("black is black", FramePush::rgb332Wire(0x00) == 0x0000);
    ck("white is white", FramePush::rgb332Wire(0xFF) == 0xFFFF);

    suite("Each channel on its own");
    // A swapped pair of shifts is the likeliest slip and it hides well in
    // the sweep, so the primaries are named.
    ck("full red is red alone",     asRgb565(FramePush::rgb332Wire(0xE0)) == 0xF800);
    ck("full green is green alone", asRgb565(FramePush::rgb332Wire(0x1C)) == 0x07E0);
    ck("full blue is blue alone",   asRgb565(FramePush::rgb332Wire(0x03)) == 0x001F);

    suite("Blue really does only have four steps");
    // The constraint the artwork is drawn around. If this ever reads five,
    // the backgrounds were designed against a screen that does not exist.
    {
        bool seen[32] = { false };
        int distinct = 0;
        for (int i = 0; i < 256; i++) {
            const uint8_t b = (uint8_t)(asRgb565(FramePush::rgb332Wire((uint8_t)i)) & 0x1F);
            if (!seen[b]) { seen[b] = true; distinct++; }
        }
        ck("four distinct blues across all 256 colours", distinct == 4);
    }

    suite("The high byte is in the low half of the word");
    // The words go into the FIFO as-is and the hardware sends the low byte
    // first, so this IS the wire order. Backwards gives a recognisably wrong
    // picture on the board and a right one in the emulator -- worth stating.
    ck("red's bits are in the byte that goes out first",
       (FramePush::rgb332Wire(0xE0) & 0xFF) == 0xF8);

    return report();
}
