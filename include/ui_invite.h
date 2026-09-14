// SquachWatch-CYD — the ADD TO SQUAD screen, both sides of it.
//
// One screen that reads MeshTalk's invite state and shows the right page:
// the inviter waiting, the invitee being asked, both comparing four digits,
// the phrase going over, done, or why not. The buttons are the only inputs;
// the radio dance happens in meshtalk.cpp.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>
#include "meshtalk.h"

class DetectionEngine;

enum class InviteHit : uint8_t { NONE, ACCEPT, DECLINE, MATCH, NOMATCH, CANCEL, SHOW, BACK };

void      uiInviteInit(TFT_eSPI& t);
void      uiInviteTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
InviteHit uiInviteHit(TFT_eSPI& t, int x, int y);
// After SHOW on a failed invite: the five words, for the other person to type.
bool      uiInviteShowingPhrase();

// For the emulator: draw as if the invite were in this state, with this code
// and this name. Cleared by uiInviteInit().
void      uiInviteDemo(MeshTalk::InviteState st, uint16_t code, const char* name, bool inviter);
#endif
