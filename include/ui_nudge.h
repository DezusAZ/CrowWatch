// SquachWatch-CYD — the SQUAD UPDATE prompt: what a board shows when another
// board with the phrase has asked everyone in range to update.
//
// A countdown, not a question. The owner turned REMOTE UPDATE on to get
// exactly this, so the board proceeds unless somebody taps SKIP; the count
// is there so a board in a hand has a moment to say no, and NOW is there
// for the person who is watching and does not want to wait.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

enum class NudgeHit : uint8_t { NONE, NOW, SKIP };

// `from` is the sender's name, `ver` the version they run, `seconds` the count.
void     uiNudgeInit(TFT_eSPI& t, const char* from, const uint8_t ver[3], uint16_t seconds, uint32_t now);
void     uiNudgeTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
NudgeHit uiNudgeHit(TFT_eSPI& t, int x, int y);
int      uiNudgeSecondsLeft(uint32_t now);
#endif
