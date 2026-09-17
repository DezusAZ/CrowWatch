// SquachWatch-CYD — CROWD: how many SquachWatches share the main screen.
// The desk has its own count, on Settings' DESK MODE page.
//
// This was one row on the SquachMesh menu that cycled a number. A page has
// room to say what each choice does; the desk's switch lived here for a
// while too, before the desk got a page of its own.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>

class DetectionEngine;

enum class CrowdRow : uint8_t {
    HOW_MANY = 0,   // one visitor, or up to N of them roaming
    COUNT,
    NONE = 255
};

void     uiCrowdInit(TFT_eSPI& t);
void     uiCrowdTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
CrowdRow uiCrowdHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
#endif
