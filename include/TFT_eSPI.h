// SquachWatch-CYD — TFT_eSPI API shim for the ESP32-4848S040 board.
//
// Every other board compiles against the real TFT_eSPI library, and all
// of the app's includes ask for <TFT_eSPI.h>. This board's panel is a
// parallel-RGB ST7701 that only Arduino_GFX can drive, so for the 4848
// env this header (found ahead of the library's own copy via the
// project include path) is TFT_eSPI.h: it redeclares the compatibility
// layer from gfx_shim.h with the same names the app uses.
#pragma once

// Only the 4848 env should ever resolve here; anything else including
// this would be a build configuration mistake.
#ifndef ESP32_4848S040
#error "gfx_shim front TFT_eSPI.h included without ESP32_4848S040 defined"
#endif

#include "gfx_shim.h"