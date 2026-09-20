// SquachWatch-CYD — where a frame's time goes.
//
// The [frame] line on serial has always said how long a frame takes and how
// much of it is the push. That left the other 40 ms as one number, and
// nobody had ever broken it down -- every framerate conversation was a
// guess about which of the background, the mascot, the chrome or the radio
// bookkeeping was the expensive one.
//
// This splits it. Each frame is a sequence of laps: the time since the
// previous lap is charged to the named slot. Seven micros() reads a frame,
// which is nothing, and the split prints on its own [frame] line every ten
// seconds beside the total.
//
// Slots are laid out for the main screen, which is the one whose frame rate
// anybody sees. On every other screen there are no laps between PRE and
// POST, so POST is simply "drawing that screen" -- still a useful number.
#pragma once
#include <Arduino.h>

namespace FrameProf {

enum Slot : uint8_t {
    PRE,      // loop start to the screen switch: touch, radio, OTA, mesh
    BG,       // the animated background
    SQUACHY,  // the mascot, or the visit / crowd standing in for him
    IDLE,     // the idle events (UFO, etc.)
    HEADLINE, // the NEARBY headline: 24 outline passes and the fill
    CHROME,   // title bar, counters, buttons, and the geometry
    POST,     // after the screen returns, up to the push: toasts, transition
    PUSH,     // the sprite onto the panel
    X1, X2, X3, X4, X5, X6,  // scratch, for digging inside one thing; printed only if used
    N
};

void begin();        // at the top of loop(): starts the clock, zeroes the frame
void lap(Slot s);    // charges the time since the last lap (or begin) to s
void endFrame();     // folds this frame into the running averages
void print();        // one [frame] line with every slot, in ms

}  // namespace FrameProf
