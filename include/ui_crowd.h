// SquachWatch-CYD — CROWD: how many SquachWatches share the screen, and
// which screens they share.
//
// This was one row on the SquachMesh menu that cycled a number. It became a
// page of its own when the desk wanted a say: that menu is exactly full in
// landscape -- seven rows end two pixels above the pinned BACK strip -- so
// an eighth row would have meant shrinking every touch target on it, on a
// resistive panel, to hold one switch. A page costs one more tap to reach
// the number and leaves room to say what each choice does.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>

class DetectionEngine;

enum class CrowdRow : uint8_t {
    HOW_MANY = 0,   // one visitor, or up to N of them roaming
    ON_DESK,        // ...and whether they turn up on the desk clock too
    COUNT,
    NONE = 255
};

void     uiCrowdInit(TFT_eSPI& t);
void     uiCrowdTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
CrowdRow uiCrowdHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
#endif
