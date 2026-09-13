// SquachWatch-CYD — STATUS LIGHT screen: everything about the RGB LED on
// the back of the board, on one list. Reached from the APPEARANCE page.
//
// Same shape as the POWER SAVER screen, and for the same reason: a master
// switch that gates the lot without discarding the individual choices, so
// the light can be set up while it is off and switched on once it is right.
#pragma once
#include <TFT_eSPI.h>

class DetectionEngine;

enum class LightRow : uint8_t {
    ENABLED = 0,
    ALERTS,
    MESSAGES,
    IDLE,
    IDLE_COLOR,
    BRIGHTNESS,
    TEST,
    COUNT,
    NONE = 255
};

void uiLightInit(TFT_eSPI& t);
void uiLightTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
void uiLightScroll(int delta);          // positive = scroll down

// Row layout matches whatever uiLightTick just drew (shared geometry), so
// only call this against a screen already showing this list.
LightRow uiLightHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
