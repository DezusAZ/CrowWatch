// SquachWatch-CYD — the two built-in TFT_eSPI fonts this app uses: GLCD
// (font 1, 6x8 cells) and font 2 (the 16-row proportional face). Copied
// from TFT_eSPI's Fonts/ tree so the shim renders the same glyphs.
#pragma once

#include <Arduino.h>

// Font 2's glyph tables (widtbl_f16, chrtbl_f16).
#include "fonts/Font16.h"

// Font 1's glyph table (font[] PROGMEM, 5 data columns per char).
#include "fonts/glcdfont.c"