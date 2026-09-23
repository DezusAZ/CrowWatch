// SquachWatch-CYD — persistent touch calibration
// Touch-type-agnostic: works for the resistive XPT2046 (on its own bus or
// the display's) and the capacitive CST816/820 alike, since all of them
// reduce to two raw numbers per touch. The caller supplies a raw-sample
// reader; the mapping from those numbers to the screen is a TouchFit::Fit
// (see touch_fit.h), the same on every board and good for every rotation.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "touch_fit.h"

namespace TouchCal {
    // True and fills a/b with a fresh raw sample if a finger is
    // currently down, false otherwise.
    typedef bool (*RawReader)(int16_t& a, int16_t& b);

    // ---- The calibration every board uses now ----

    // Loads the saved Fit. False (leaving `out` untouched) if there is
    // none, or what is saved does not look like a real one.
    bool loadFit(TouchFit::Fit& out);
    void saveFit(const TouchFit::Fit& fit);

    enum class Outcome : uint8_t {
        SAVED,      // a new Fit passed every check and is in `out`
        SKIPPED,    // the owner chose SKIP / CANCEL; nothing changed
        NO_TOUCH,   // nobody touched the screen; nothing changed
        FAILED,     // the taps did not add up twice running; nothing changed
    };

    // The interactive five-target calibration, drawn straight onto `t` in
    // rotation `rot`. Blocking: it has the owner's full attention by
    // definition. Does not save -- the caller does, on SAVED.
    //
    // `current` is the mapping in force now. When given, the first screen
    // offers SKIP, which is hit-tested through it; when null, a tap anywhere
    // starts. minSpread is the raw distance the diagonal targets must at
    // least be apart (capacitive and resistive chips have very different
    // ranges -- see main.cpp's constants).
    // How much denser this panel's pixels are than the 2.8" board's (5.6 per
    // mm): the tap tolerances are in pixels, and a fingertip is the same
    // size on every screen. The T-Watch S3 packs 240 px into 27 mm, 1.6x.
    void setDensityScale(float scale);
    Outcome runInteractive(TFT_eSPI& t, RawReader readRaw, uint8_t rot,
                           const TouchFit::Fit* current, int16_t minSpread,
                           uint16_t bg, uint16_t fg, uint16_t accent,
                           TouchFit::Fit& out);

    // Erases every saved calibration, old and new -- the recovery path for
    // a bad one that makes touch too inaccurate to reach a button. main.cpp
    // offers it as a hold-anywhere gesture right after boot.
    void reset();

    // ---- Calibrations saved by older firmware ----
    // Read once, to build a Fit for owners who SKIP, so they keep exactly
    // the touch they had. Never written any more.
    struct Cal {
        int16_t aTop, aBottom;   // raw "a" axis at the top row vs. bottom row
        int16_t bLeft, bRight;   // raw "b" axis at the left column vs. right column
    };
    // False (leaving `out` untouched) if none is saved, or it fails the
    // spread check: minSpread is the smallest |a1-a2| / |b1-b2| a real
    // calibration can have.
    bool load(Cal& out, int16_t minSpread);
}
