// SquachWatch-CYD — the regulars.
//
// What this guards: a device gets a name on its third DAY, not its third
// sighting; keeps that name; a full table drops the one not seen for
// longest; and "just became a regular" fires exactly once.
#include "test_util.h"
#include "regulars.h"
#include "clock.h"
#include <cstring>

namespace Clock {
bool     trusted()  { return true; }
uint32_t localDay() { return 20000; }
}

static void mac(uint8_t* m, uint16_t n) { memset(m, 0, 6); m[0] = 0x02; m[4] = (uint8_t)(n >> 8); m[5] = (uint8_t)n; }

int main() {
    Regulars::begin();
    uint8_t ring[6], tag[6];
    mac(ring, 1); mac(tag, 2);

    suite("Three days makes a regular");
    for (int k = 0; k < 5; k++) Regulars::noteOnDay(ring, DetectionType::RING, 100);
    ck("five sightings in one day is one day", Regulars::daysFor(ring) == 1 && !Regulars::nameFor(ring));
    Regulars::noteOnDay(ring, DetectionType::RING, 101);
    ck("two days, still no name", !Regulars::nameFor(ring));
    Regulars::noteOnDay(ring, DetectionType::RING, 102);
    const char* n = Regulars::nameFor(ring);
    ck("three days: named", n && *n);
    ck("it just became one, once", Regulars::takeNewRegular(ring) && !Regulars::takeNewRegular(ring));
    Regulars::noteOnDay(ring, DetectionType::RING, 103);
    ck("the name sticks", Regulars::nameFor(ring) == n);
    ck("count is one", Regulars::count() == 1);

    suite("Different devices, different names");
    for (uint32_t d = 100; d < 103; d++) Regulars::noteOnDay(tag, DetectionType::AIRTAG, d);
    ck("both named, differently", Regulars::nameFor(tag) && strcmp(Regulars::nameFor(tag), n) != 0);

    suite("A full table drops the one not seen for longest");
    uint8_t m[6];
    for (uint16_t k = 10; k < 10 + Regulars::CAP; k++) { mac(m, k); Regulars::noteOnDay(m, DetectionType::TILE, 200); }
    ck("the ring, last seen day 103, was dropped", !Regulars::nameFor(ring) && Regulars::daysFor(ring) == 0);
    mac(m, 10);
    ck("a newcomer is in", Regulars::daysFor(m) == 1);

    suite("Reset");
    Regulars::reset();
    ck("empty", Regulars::count() == 0 && Regulars::daysFor(tag) == 0);
    return report();
}
