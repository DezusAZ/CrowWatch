// SquachWatch-CYD — UPDATE SQUAD: tell every SquachWatch in range with the
// phrase to update to the version this board runs. Reached from the UPDATE
// FIRMWARE screen, because that is what it is.
//
// Two views on one screen. Before SEND: what it does, and whether to share
// this board's saved WiFi with the nudge. After: a tally of the boards that
// have reported back, filled in by main.cpp as the replies arrive.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

enum class SquadUpdateHit : uint8_t { NONE, SHARE, SEND, BACK };

void           uiSquadUpdateInit(TFT_eSPI& t);
void           uiSquadUpdateTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
SquadUpdateHit uiSquadUpdateHit(TFT_eSPI& t, int x, int y);
bool           uiSquadUpdateShareWifi();
void           uiSquadUpdateToggleShare();
// The nudge went out (or did not). Switches to the tally view when it did.
void           uiSquadUpdateSent(bool ok, uint32_t now);
// A board reported in. Duplicates by name are folded.
void           uiSquadUpdateReported(const char* name);
#endif
