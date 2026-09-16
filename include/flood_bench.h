// SquachWatch-CYD -- the fake flood, for the bench only (build flag FLOOD_BENCH).
//
// Feeds the Bluetooth stack's own scan handler made-up adverts: random
// addresses, the connectable-and-scannable kind, and never a scan response.
// That is the exact shape of a college IT floor as the scanner sees it --
// devices it has asked and will never hear back from -- which no emitter on
// the desk can reproduce, because an emitter two feet away always answers.
// FLOOD N on the console starts N adverts a second; FLOOD 0 stops.
#pragma once
#include <stdint.h>

#if FLOOD_BENCH
void floodSet(uint16_t perSecond);
void floodTick();          // from loop(): posts the next burst to the host task
#else
inline void floodSet(uint16_t) {}
inline void floodTick() {}
#endif
