// SquachWatch-CYD — the regulars: devices he sees day after day.
//
// A device seen on three different days gets a name from a short list --
// the Ring you pass every morning is Gary from then on -- and Squachy
// greets it like a neighbour. Sixteen of them, kept in one small NVS blob;
// when the table is full the one not seen for longest makes room. Days
// come from the clock, so nothing becomes a regular until it is set.
#pragma once
#include <stdint.h>
#include "state.h"

namespace Regulars {

static const uint8_t CAP        = 16;
static const uint8_t DAYS_TO_BE = 3;   // seen on this many days: a regular

void begin();
// A sighting (RAM only; called from the Bluetooth host task). Counts one
// per local day per device.
void note(const uint8_t* mac, DetectionType type);
// The same, for a given day number -- what the tests and the emulator use.
void noteOnDay(const uint8_t* mac, DetectionType type, uint32_t day);
// Writes the table back a while after it changed. From loop().
void tick(uint32_t now);

// The device's name, or null while it is not yet a regular.
const char* nameFor(const uint8_t* mac);
uint8_t     daysFor(const uint8_t* mac);
// True once, the first time a device crosses the line: it just got its name.
bool takeNewRegular(const uint8_t* mac);
uint8_t count();   // regulars (named) in the table

void reset();

}  // namespace Regulars
