// SquachWatch-CYD — which of the black box's kept sightings the LOG shows.
//
// The LOG is the engine's RAM ring (this boot) followed by every sighting the
// black box kept in flash. The flash has a record for every time a device was
// first seen or came back -- today's included -- so a device seen yesterday
// and again today showed twice, a grey KEPT row under its live one, and a
// device that comes and goes showed once per visit.
//
// This keeps one row per device (MAC + type): the live row if the ring has
// it, else its newest kept sighting. Built by one walk of the flash when the
// LOG opens and whenever the ring changes, at most once a second.
#pragma once
#include <stdint.h>

namespace LogIndex {

uint32_t key(const uint8_t* mac, uint8_t type);

// Rebuilds from the ring's keys and the black box as it is now. False when
// the memory for it could not be had; the LOG then shows every kept row, as
// it did before, rather than none.
bool rebuild(const uint32_t* ringKeys, uint16_t ringN);

// Kept rows to show after the ring's.
uint16_t count();

// The row'th of them, as a position in the black box's newest-first list
// right now. Positions only move by records appended since the build, which
// all land at the newest end, so they stay exact between rebuilds.
bool position(uint16_t row, uint16_t& newestIdx);

// Frees the index; count() is then every kept row, unfiltered.
void release();

// Whether the last rebuild succeeded (for the fallback above).
bool filtering();

}  // namespace LogIndex
