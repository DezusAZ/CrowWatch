// SquachWatch-CYD — desk mode
//
// The board as the thing beside the keyboard: the time, large; the date;
// Squachy underneath, chattering as he does on CLEAR; and a focus timer
// (25 minutes on, 5 off) that he counts down with the light on the back.
// Detection keeps running behind it and an alert still takes the screen;
// this is a place to leave the board, not a mode that switches anything off.
//
// Reached from Settings' DESK MODE row. The power saver does not dim this
// screen: a clock that goes dark is a clock that is not there.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

void uiDeskInit(TFT_eSPI& t);
// `advance` is false on the second of the 3.5"'s two band passes: the same
// frame drawn again, so the background and the mascot must not step twice.
// Every other board passes the default and draws once.
void uiDeskTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);

// The two buttons at the bottom: the timer (FOCUS / the count / BREAK) and
// BACK. Returns true when the tap did something.
bool uiDeskHitTimer(int x, int y, int screenW, int screenH);
bool uiDeskHitBack(int x, int y, int screenW, int screenH);
// A tap on the clock's left or right fifth: -1 or +1, the way an edge tap
// turns the background over; 0 anywhere else.
int  uiDeskHitClockEdge(int x, int y);
// The gear at the bottom left: the DESK MODE page in Settings.
bool uiDeskHitSettings(int x, int y, int screenW, int screenH);
void uiDeskTapTimer(uint32_t now);

// A detection while the desk is up: a small card beside the clock for a
// few seconds instead of the full-screen ALERT, so the clock stays a clock.
// Tapping the card opens the full card. main.cpp feeds these the same way
// CLEAR decides to alert, and lights the LED in the type's colour while
// the card is up.
void             uiDeskAlert(const Detection& d, uint32_t now);
bool             uiDeskAlertUp(uint32_t now);
const Detection* uiDeskAlertDetection();
bool             uiDeskHitAlert(int x, int y, uint32_t now);

// A squad message on the desk: a polaroid of the sender's Squachy, his
// name in the margin, the message on a note beside it, and the time it
// came, standing where our Squachy stands until it is read. A tap on the
// card reads it (and sends the read receipt), the same as opening the
// inbox would.
bool uiDeskHitMessage(int x, int y);

// True for a few seconds after a focus block ends: main.cpp lights the LED
// green off this, the same way HUNT's CAUGHT does.
bool uiDeskChime(uint32_t now);
