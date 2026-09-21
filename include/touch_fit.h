// SquachWatch-CYD — raw touch to screen, as plain arithmetic.
//
// One mapping for every board and every rotation. A Fit turns the touch
// chip's two raw numbers into a point on the panel in its NATIVE frame --
// rotation 0, portrait -- and the rotation step afterwards is pure display
// geometry that has nothing to do with the touch chip at all. So a board is
// calibrated once, in whichever rotation it happens to be in, and the result
// holds in the other three.
//
// That replaces three schemes that each got something wrong:
//   - The 2.8" boards stored one min/max per raw axis and assumed the
//     calibration was taken in landscape. Taken in portrait, the corners it
//     averaged came out nearly equal and the result was refused, or taken
//     upside down, it came out inverted.
//   - AWOK and the 3.5" used TFT_eSPI's calibrateTouch(), whose targets sit
//     in the very corners -- under the lip of a case -- and whose result is
//     only right in the rotation it was taken in, hence the 3.5" asking once
//     per rotation.
//   - None of them could correct a digitiser that is slightly skewed against
//     the glass, because a min/max per axis cannot express one. A Fit is a
//     full affine map (six numbers), fitted by least squares over five taps.
//
// No Arduino, no TFT_eSPI: test/touchfit_test.cpp checks all of this on a
// desktop, including that the old mappings are reproduced exactly.
#pragma once
#include <stdint.h>
#include <math.h>

namespace TouchFit {

// native x = xa*a + xb*b + xc,  native y = ya*a + yb*b + yc
struct Fit {
    float xa, xb, xc;
    float ya, yb, yc;
    int16_t w0, h0;   // the native (rotation 0) panel size this maps onto
};

// Rotation r's screen coordinates to the native frame, and back.
//
// Derived from the four-rotation block the 2.8" boards' pollTouch() has used
// for years, which is right on hardware in all four; and cross-checked
// against the MADCTL tables of all three panel drivers: ILI9341 and ST7796
// (MX, MV, MY, MX|MY|MV) and ST7789 (0, MX|MV, MX|MY, MV|MY) give the same
// relationship between rotations, although not the same bits.
// Continuous coordinates, edges at 0 and w -- the same convention map() used.
inline void screenToNative(float sx, float sy, uint8_t rot, int w0, int h0,
                           float& nx, float& ny) {
    switch (rot & 3) {
        case 0:  nx = sx;      ny = sy;      break;
        case 1:  nx = w0 - sy; ny = sx;      break;
        case 2:  nx = w0 - sx; ny = h0 - sy; break;
        default: nx = sy;      ny = h0 - sx; break;
    }
}
inline void nativeToScreen(float nx, float ny, uint8_t rot, int w0, int h0,
                           float& sx, float& sy) {
    switch (rot & 3) {
        case 0:  sx = nx;      sy = ny;      break;
        case 1:  sx = ny;      sy = w0 - nx; break;
        case 2:  sx = w0 - nx; sy = h0 - ny; break;
        default: sx = h0 - ny; sy = nx;      break;
    }
}

inline void toNative(const Fit& f, float a, float b, float& nx, float& ny) {
    nx = f.xa * a + f.xb * b + f.xc;
    ny = f.ya * a + f.yb * b + f.yc;
}

inline void toScreen(const Fit& f, float a, float b, uint8_t rot, float& sx, float& sy) {
    float nx, ny;
    toNative(f, a, b, nx, ny);
    nativeToScreen(nx, ny, rot, f.w0, f.h0, sx, sy);
}

// The raw reading that lands on a native point -- for the diagnostics
// screen, which shows what the panel's corners read as. False if the fit
// cannot be inverted.
inline bool toRaw(const Fit& f, float nx, float ny, float& a, float& b) {
    const float det = f.xa * f.yb - f.xb * f.ya;
    if (fabsf(det) < 1e-9f) return false;
    const float dx = nx - f.xc, dy = ny - f.yc;
    a = ( f.yb * dx - f.xb * dy) / det;
    b = (-f.ya * dx + f.xa * dy) / det;
    return true;
}

// Least-squares fit of n >= 3 raw readings to the native points they were
// taken at. Solved as two 3x3 normal-equation systems in double, since raw
// ADC values squared and summed run past float's 24 bits of mantissa.
// False if the readings cannot determine a map (all on one line, or the
// same reading at every target -- a dead or stuck digitiser).
inline bool solve(const float* a, const float* b, const float* nx, const float* ny,
                  int n, int w0, int h0, Fit& out) {
    if (n < 3) return false;
    // Centred on the mean raw reading, which keeps the sums small and the
    // system well conditioned.
    double ma = 0, mb = 0;
    for (int i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double saa = 0, sab = 0, sbb = 0, sax = 0, sbx = 0, sx = 0, say = 0, sby = 0, sy = 0;
    for (int i = 0; i < n; i++) {
        const double da = a[i] - ma, db = b[i] - mb;
        saa += da * da; sab += da * db; sbb += db * db;
        sax += da * nx[i]; sbx += db * nx[i]; sx += nx[i];
        say += da * ny[i]; sby += db * ny[i]; sy += ny[i];
    }
    const double det = saa * sbb - sab * sab;
    // Relative to the spread itself, so the test means the same thing for a
    // capacitive chip's few hundred counts as for a resistive one's 4096.
    if (!(det > 1e-6 * saa * sbb) || saa <= 0 || sbb <= 0) return false;
    const double xa = (sax * sbb - sbx * sab) / det;
    const double xb = (sbx * saa - sax * sab) / det;
    const double ya = (say * sbb - sby * sab) / det;
    const double yb = (sby * saa - say * sab) / det;
    out.xa = (float)xa; out.xb = (float)xb;
    out.ya = (float)ya; out.yb = (float)yb;
    // Centred model: n = k*(r - mean) + mean(n)  =>  constant = mean(n) - k*mean.
    out.xc = (float)(sx / n - xa * ma - xb * mb);
    out.yc = (float)(sy / n - ya * ma - yb * mb);
    out.w0 = (int16_t)w0;
    out.h0 = (int16_t)h0;
    return true;
}

// The mapping the 2.8" boards have always used, from its min/max pairs:
// raw "a" runs across the native width (aMin at x=0, aMax at x=w0) and raw
// "b" down the native height. Both pollTouch() branches -- XPT2046 and the
// capacitive CST816 -- had exactly this shape in rotation 0, and their other
// three rotations were this plus screen geometry. So a board that skips the
// new calibration keeps precisely the touch it had.
inline Fit fromRanges(float aMin, float aMax, float bMin, float bMax, int w0, int h0) {
    Fit f;
    const float da = (aMax != aMin) ? (aMax - aMin) : 1.0f;
    const float db = (bMax != bMin) ? (bMax - bMin) : 1.0f;
    f.xa = w0 / da; f.xb = 0; f.xc = -aMin * w0 / da;
    f.ya = 0; f.yb = h0 / db; f.yc = -bMin * h0 / db;
    f.w0 = (int16_t)w0; f.h0 = (int16_t)h0;
    return f;
}

// TFT_eSPI's own calibration blob (what calibrateTouch() filled in, taken at
// rotation `rot` on a w x h screen) as a Fit, so AWOK and 3.5" owners who
// skip keep the touch they had. Its convertRawXY() is linear in the raw
// values, so three raw points pushed through it and solved back give the
// same map exactly. p[1] and p[3] are spans, not end points -- that is how
// convertRawXY() divides by them.
inline void tftEspiConvert(const uint16_t* p, float a, float b, int w, int h,
                           float& sx, float& sy) {
    const float x0 = p[0] ? p[0] : 1, x1 = p[1] ? p[1] : 1;
    const float y0 = p[2] ? p[2] : 1, y1 = p[3] ? p[3] : 1;
    const bool swapAxes = p[4] & 1, invX = p[4] & 2, invY = p[4] & 4;
    const float ra = swapAxes ? b : a, rb = swapAxes ? a : b;
    sx = (ra - x0) * w / x1;
    sy = (rb - y0) * h / y1;
    if (invX) sx = w - sx;
    if (invY) sy = h - sy;
}
inline bool fromTftEspi(const uint16_t* p, uint8_t rot, int w, int h, Fit& out) {
    const int w0 = (rot & 1) ? h : w, h0 = (rot & 1) ? w : h;
    float a[3] = { 0, 4096, 0 }, b[3] = { 0, 0, 4096 }, nx[3], ny[3];
    for (int i = 0; i < 3; i++) {
        float sx, sy;
        tftEspiConvert(p, a[i], b[i], w, h, sx, sy);
        screenToNative(sx, sy, rot, w0, h0, nx[i], ny[i]);
    }
    return solve(a, b, nx, ny, 3, w0, h0, out);
}

// The five targets, as fractions of the screen in whatever rotation the
// calibration runs in: four corners pulled well in, plus the centre.
//
// INSET is the point of the exercise. The old targets sat 9 px or less from
// the corners, and many of these boards live in cases whose lip covers
// exactly that -- AWOK's worst of all -- so the finger pressed the case and
// the panel read something else. At 15% the target is 36 px in on a 240-px
// side. The edges are extrapolated, which a linear digitiser makes exact.
static const float INSET = 0.15f;
static const int   TARGETS = 5;
inline void targetFrac(int i, float& fx, float& fy) {
    static const float FX[TARGETS] = { INSET, 1 - INSET, 1 - INSET, INSET, 0.5f };
    static const float FY[TARGETS] = { INSET, INSET, 1 - INSET, 1 - INSET, 0.5f };
    fx = FX[i]; fy = FY[i];
}

}  // namespace TouchFit
