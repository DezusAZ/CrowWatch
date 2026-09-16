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
// Which saved network to share: the index into OtaWifi's saved list of the
// one the scan on this screen actually found in the room, or -1 while the
// scan is running and when none of them is here. Sending credentials for the
// network at home to a board that is not at home teaches it a password it
// can never use, and it would sit on the air in a frame anyone can hear.
int8_t         uiSquadUpdateShareIndex();
// The nudge went out (or did not). Switches to the tally view when it did.
void           uiSquadUpdateSent(bool ok, uint32_t now);
// A board reported in. Duplicates by name are folded.
void           uiSquadUpdateReported(const char* name);
#endif
