// SquachWatch-CYD — detection bingo
//
// A card of sixteen detection types. A type marks its square the first time
// the board sees one this week; four in a row is a line; sixteen is a full
// card. A fresh card every week, and a streak for weeks finished.
//
// The game the board was always half playing: it already counts every type
// it has ever seen, and the rare ones (a gunshot sensor, a plate reader)
// were numbers that never moved. This gives them a reason to.
//
// Cheap on purpose: the card is sixteen bytes, the marks sixteen more, and
// nothing here runs per frame. note() is called from the detection path on
// the Bluetooth host task and only touches RAM; tick(), from loop(), is what
// writes to flash.
#pragma once
#include <stdint.h>
#include "state.h"

class DetectionEngine;

namespace Bingo {

static const uint8_t CELLS = 16;   // 4x4
static const uint8_t LINES = 10;   // four rows, four columns, two diagonals

// What just happened, for the toast the main loop shows. One at a time,
// newest last; taken by takeEvent().
enum class Event : uint8_t { NONE, MARKED, LINE, FULL, NEW_CARD };

// Reads the card back, or deals the first one. The engine is for the
// lifetime counts: a card leans towards types this board has seen before,
// so it is winnable where the board actually lives.
void begin(const DetectionEngine& eng);

// From loop(): saves a mark that is waiting, and deals a new card when the
// week turns over.
void tick(uint32_t now);

// A sighting. Safe from the radio task: RAM only.
void note(DetectionType t);

DetectionType typeAt(uint8_t i);
bool     marked(uint8_t i);
uint8_t  markDay(uint8_t i);          // 0 unmarked, else 1..7 with Sunday 1
uint8_t  markedCount();
bool     inCalledLine(uint8_t i);     // draw it as part of a finished line
uint8_t  linesCalled();
uint32_t weekNumber();                // the card's week; 0 before the clock is set

uint16_t cardsFilled();
uint16_t linesEver();
uint8_t  streak();
uint8_t  bestStreak();
uint8_t  bestFilled();                // most squares on any one card

// DEAL ME IN. Ends the week's card; a card that was not full ends the streak.
void newCard();

// The next thing to announce, or NONE. `type` is the square that marked.
Event takeEvent(DetectionType& type);

}  // namespace Bingo
