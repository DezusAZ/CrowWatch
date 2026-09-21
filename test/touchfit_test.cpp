// SquachWatch-CYD — the touch mapping, off the device.
//
// What this guards: a wrong sign or a swapped axis in touch_fit.h does not
// crash anything. It puts every tap somewhere plausible and wrong, on one
// board in one rotation, and the only way to find it is a person with that
// board tapping buttons. So the arithmetic is pinned here:
//   - the rotation step is a true inverse of itself, in all four rotations;
//   - five taps generated from a known map (skewed, swapped, inverted) are
//     solved back to that map;
//   - a board that skips the new calibration gets EXACTLY the touch the old
//     pollTouch() gave it, in all four rotations -- written out here as the
//     old map() expressions, not re-derived from the new code;
//   - TFT_eSPI's blob converts to a Fit that agrees with TFT_eSPI's own
//     convertRawXY() at every point.
#include "test_util.h"
#include "touch_fit.h"
#include <cstdlib>
#include <algorithm>

using namespace TouchFit;

// Arduino's map(), integer and truncating, as the old pollTouch() used it.
static long amap(long x, long inMin, long inMax, long outMin, long outMax) {
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// The old pollTouch() resistive/capacitive block, verbatim in shape.
static void oldMap(long pa, long pb, long aMin, long aMax, long bMin, long bMax,
                   uint8_t rot, int w, int h, long& x, long& y) {
    const bool landscape = rot % 2 == 1, flipped = rot >= 2;
    if (!landscape) {
        x = flipped ? amap(pa, aMin, aMax, w, 0) : amap(pa, aMin, aMax, 0, w);
        y = flipped ? amap(pb, bMin, bMax, h, 0) : amap(pb, bMin, bMax, 0, h);
    } else {
        x = flipped ? amap(pb, bMin, bMax, w, 0) : amap(pb, bMin, bMax, 0, w);
        y = flipped ? amap(pa, aMin, aMax, 0, h) : amap(pa, aMin, aMax, h, 0);
    }
}

int main() {
    suite("Rotation step is its own inverse");
    {
        float worst = 0;
        for (uint8_t r = 0; r < 4; r++) {
            const int w0 = 240, h0 = 320;
            for (float sx = 0; sx <= 320; sx += 17) {
                for (float sy = 0; sy <= 320; sy += 19) {
                    float nx, ny, bx, by;
                    screenToNative(sx, sy, r, w0, h0, nx, ny);
                    nativeToScreen(nx, ny, r, w0, h0, bx, by);
                    worst = fmaxf(worst, fabsf(bx - sx) + fabsf(by - sy));
                }
            }
        }
        ckf("screen -> native -> screen, all rotations", worst, 0, 1e-4f);
    }

    suite("Screen corners land on the native corners");
    {
        // Rotation 1 is 90 degrees round: its top-left is native top-right.
        float nx, ny;
        screenToNative(0, 0, 1, 240, 320, nx, ny);
        ck("rot 1 top-left is native top-right", nx == 240 && ny == 0);
        screenToNative(0, 0, 2, 240, 320, nx, ny);
        ck("rot 2 top-left is native bottom-right", nx == 240 && ny == 320);
        screenToNative(0, 0, 3, 240, 320, nx, ny);
        ck("rot 3 top-left is native bottom-left", nx == 0 && ny == 320);
    }

    suite("Five taps solve back to the map that made them");
    {
        // Axes swapped, one inverted, a few degrees of skew, an offset: the
        // worst a digitiser could plausibly be glued on.
        Fit truth = { 0.002f, -0.071f, 250.0f, 0.084f, 0.004f, -18.0f, 240, 320 };
        for (uint8_t r = 0; r < 4; r++) {
            const int w = (r & 1) ? 320 : 240, h = (r & 1) ? 240 : 320;
            float a[TARGETS], b[TARGETS], nx[TARGETS], ny[TARGETS];
            for (int i = 0; i < TARGETS; i++) {
                float fx, fy;
                targetFrac(i, fx, fy);
                screenToNative(fx * w, fy * h, r, 240, 320, nx[i], ny[i]);
                // Invert `truth` to get the raw reading at that point.
                toRaw(truth, nx[i], ny[i], a[i], b[i]);
            }
            Fit got{};
            const bool ok = solve(a, b, nx, ny, TARGETS, 240, 320, got);
            float worst = 0;
            for (float ra = 300; ra <= 3800; ra += 350)
                for (float rb = 300; rb <= 3800; rb += 350) {
                    float gx, gy, tx, ty;
                    toNative(got, ra, rb, gx, gy);
                    toNative(truth, ra, rb, tx, ty);
                    worst = fmaxf(worst, fabsf(gx - tx) + fabsf(gy - ty));
                }
            char what[64];
            snprintf(what, sizeof(what), "calibrated in rotation %u", r);
            ck(what, ok);
            ckf("  worst error across the panel, px", worst, 0, 0.05f);
        }
    }

    suite("Noisy taps still fit, and the bad tap shows");
    {
        Fit truth = fromRanges(3800, 250, 300, 3700, 240, 320);
        float a[TARGETS], b[TARGETS], nx[TARGETS], ny[TARGETS];
        srand(7);
        for (int i = 0; i < TARGETS; i++) {
            float fx, fy;
            targetFrac(i, fx, fy);
            nx[i] = fx * 240; ny[i] = fy * 320;
            toRaw(truth, nx[i], ny[i], a[i], b[i]);
            a[i] += (rand() % 21) - 10;   // +/-10 counts, about a pixel
            b[i] += (rand() % 21) - 10;
        }
        Fit got{};
        ck("fits", solve(a, b, nx, ny, TARGETS, 240, 320, got));
        float worst = 0;
        for (int i = 0; i < TARGETS; i++) {
            float gx, gy;
            toNative(got, a[i], b[i], gx, gy);
            worst = fmaxf(worst, hypotf(gx - nx[i], gy - ny[i]));
        }
        ck("residual under 3 px with ordinary noise", worst < 3);
        // One finger slips 400 counts -- about 30 px.
        a[2] += 400;
        solve(a, b, nx, ny, TARGETS, 240, 320, got);
        worst = 0;
        for (int i = 0; i < TARGETS; i++) {
            float gx, gy;
            toNative(got, a[i], b[i], gx, gy);
            worst = fmaxf(worst, hypotf(gx - nx[i], gy - ny[i]));
        }
        ck("a slipped tap leaves a residual over 8 px", worst > 8);
    }

    suite("Degenerate readings are refused");
    {
        float a[5] = { 2000, 2000, 2000, 2000, 2000 }, b[5] = { 1500, 1500, 1500, 1500, 1500 };
        float nx[5] = { 36, 204, 204, 36, 120 }, ny[5] = { 48, 48, 272, 272, 160 };
        Fit f{};
        ck("the same reading at every target", !solve(a, b, nx, ny, 5, 240, 320, f));
        float a2[5] = { 500, 1000, 1500, 2000, 2500 }, b2[5] = { 500, 1000, 1500, 2000, 2500 };
        ck("every reading on one line", !solve(a2, b2, nx, ny, 5, 240, 320, f));
    }

    suite("Skipping keeps the old 2.8\" touch exactly");
    {
        // The XPT2046 defaults, a real saved calibration (inverted, as
        // applyCal() stores them) and the capacitive defaults.
        struct { long aMin, aMax, bMin, bMax; int span; } cases[] = {
            { 200, 3800, 200, 3800, 1 },
            { 3712, 318, 241, 3790, 1 },
            { 32, 166, 10, 308, 0 },
        };
        for (auto& c : cases) {
            Fit f = fromRanges(c.aMin, c.aMax, c.bMin, c.bMax, 240, 320);
            long worst = 0;
            const long hi = c.span ? 4095 : 320;
            const long step = c.span ? 97 : 7;
            for (uint8_t r = 0; r < 4; r++) {
                const int w = (r & 1) ? 320 : 240, h = (r & 1) ? 240 : 320;
                for (long pa = 0; pa <= hi; pa += step)
                    for (long pb = 0; pb <= hi; pb += step) {
                        long ox, oy;
                        oldMap(pa, pb, c.aMin, c.aMax, c.bMin, c.bMax, r, w, h, ox, oy);
                        float sx, sy;
                        toScreen(f, pa, pb, r, sx, sy);
                        worst = std::max(worst, std::max(labs(ox - (long)sx), labs(oy - (long)sy)));
                    }
            }
            char what[80];
            snprintf(what, sizeof(what), "a %ld..%ld b %ld..%ld, all rotations, within 1 px",
                     c.aMin, c.aMax, c.bMin, c.bMax);
            ck(what, worst <= 1);
        }
    }

    suite("TFT_eSPI's blob converts to the same map");
    {
        // Two real shapes: axes straight, and swapped with one inverted.
        uint16_t blobs[2][5] = { { 312, 3390, 245, 3505, 0 }, { 280, 3440, 330, 3360, 3 } };
        const uint8_t rots[2] = { 0, 1 };
        for (int k = 0; k < 2; k++) {
            const uint8_t r = rots[k];
            const int w = (r & 1) ? 480 : 320, h = (r & 1) ? 320 : 480;
            Fit f{};
            const bool ok = fromTftEspi(blobs[k], r, w, h, f);
            float worst = 0;
            for (float ra = 200; ra <= 3900; ra += 300)
                for (float rb = 200; rb <= 3900; rb += 300) {
                    float ex, ey, sx, sy;
                    tftEspiConvert(blobs[k], ra, rb, w, h, ex, ey);
                    toScreen(f, ra, rb, r, sx, sy);
                    worst = fmaxf(worst, fabsf(ex - sx) + fabsf(ey - sy));
                }
            char what[64];
            snprintf(what, sizeof(what), "blob %d (flags %u) at rotation %u", k, blobs[k][4], r);
            ck(what, ok);
            ckf("  worst disagreement with convertRawXY, px", worst, 0, 0.05f);
        }
    }

    return report();
}
