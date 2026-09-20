// SquachWatch-CYD — what switching iBeacons on means, asked before it happens.
//
// IBEACON is the one detection that ships switched off, and it is off for
// volume rather than importance: a single shop can put more beacons in range
// than this device would otherwise see all week, and each one takes over the
// screen. Somebody flipping it on in DETECTION FILTER deserves to know that
// before the first shop does it to them, so the tap that would turn it on
// opens this instead. Turning it back OFF asks nothing.
//
// The two answers are equals -- ENABLE and KEEP DISABLED, the same system
// button, neither preselected -- for the same reason SquachMesh's gate is
// built that way: a warning whose decline is dressed as the lesser choice is
// arguing, not informing.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

enum class BeaconWarnHit : uint8_t { ENABLE, KEEP_OFF, NONE };

void          uiBeaconWarnInit(TFT_eSPI& t);
// `advance` is false on the second of the 3.5"'s two band passes -- the
// same frame drawn again -- so anything that steps by the call rather
// than by the clock must sit still for it. Other boards draw once.
void          uiBeaconWarnTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);
BeaconWarnHit uiBeaconWarnHitTest(TFT_eSPI& t, int x, int y);
