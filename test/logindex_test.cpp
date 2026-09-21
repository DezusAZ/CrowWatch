// SquachWatch-CYD — one LOG row per device.
//
// What this guards: the LOG is the ring in RAM followed by the black box's
// kept sightings, and the flash keeps a record every time a device is first
// seen or comes back -- so without LogIndex a device seen yesterday and again
// today showed as its live row plus a grey KEPT copy, and a device that came
// and went showed once per visit. Nothing crashes when this is wrong; the
// list just quietly repeats itself, which is how it shipped.
#include "test_util.h"
#include "blackbox.h"
#include "log_index.h"
#include "clock.h"
#include <cstring>

namespace Clock {
uint32_t g_epoch = 1789560000u;
bool     trusted()  { return true; }
uint32_t nowEpoch() { return g_epoch; }
}

static Detection det(uint16_t n, DetectionType t = DetectionType::AIRTAG) {
    Detection d;
    memset(&d, 0, sizeof d);
    d.mac[0] = 0x02;
    d.mac[4] = (uint8_t)(n >> 8);
    d.mac[5] = (uint8_t)n;
    d.type = t;
    d.rssi = -60;
    d.hits = 1;
    return d;
}

static uint32_t keyOf(uint16_t n, DetectionType t = DetectionType::AIRTAG) {
    Detection d = det(n, t);
    return LogIndex::key(d.mac, (uint8_t)d.type);
}

// The MAC numbers of the kept rows LogIndex shows, in order.
static int shown(uint16_t* out, int max) {
    int n = 0;
    for (uint16_t row = 0; row < LogIndex::count() && n < max; row++) {
        uint16_t at;
        if (!LogIndex::position(row, at)) break;
        BlackBox::DetRecord r;
        if (BlackBox::readDetectionsAt(&at, 1, &r) != 1) break;
        out[n++] = (uint16_t)((r.mac[4] << 8) | r.mac[5]);
    }
    return n;
}

int main() {
    ck("black box up", BlackBox::begin());

    suite("Yesterday's devices, some of them twice");
    // Oldest first: 1, 2, 3, then 2 came back, then 4, then 1 came back.
    const uint16_t seq[] = { 1, 2, 3, 2, 4, 1 };
    for (uint16_t n : seq) BlackBox::noteDetection(det(n), false);
    ck("six records kept", BlackBox::detectionsKept() == 6);
    ck("index builds", LogIndex::rebuild(nullptr, 0));
    uint16_t got[16];
    int n = shown(got, 16);
    ck("four rows, one per device", n == 4);
    ck("newest visit of each, newest first: 1 4 2 3",
       n == 4 && got[0] == 1 && got[1] == 4 && got[2] == 2 && got[3] == 3);

    suite("Seen again today: the grey copy goes");
    // Today's ring has 2 and 5. Their flash records are written too, as the
    // board does on every first sighting.
    BlackBox::noteDetection(det(2), false);
    BlackBox::noteDetection(det(5), false);
    const uint32_t ring[] = { keyOf(5), keyOf(2) };
    ck("index builds", LogIndex::rebuild(ring, 2));
    n = shown(got, 16);
    ck("2 and 5 only in the live rows: kept shows 1 4 3",
       n == 3 && got[0] == 1 && got[1] == 4 && got[2] == 3);

    suite("Same MAC, different type, is a different row");
    BlackBox::noteDetection(det(3, DetectionType::DEAUTH), false);
    ck("index builds", LogIndex::rebuild(ring, 2));
    ck("four kept rows now", LogIndex::count() == 4);

    suite("Records added after the build keep positions exact");
    const uint16_t before = LogIndex::count();
    uint16_t firstAt;
    LogIndex::position(0, firstAt);
    BlackBox::noteDetection(det(9), false);
    BlackBox::noteDetection(det(9), false);
    uint16_t firstAtNow;
    LogIndex::position(0, firstAtNow);
    ck("same rows until the next rebuild", LogIndex::count() == before);
    ck("row 0 shifted by the two new records", firstAtNow == firstAt + 2);
    n = shown(got, 16);
    ck("and still reads the same device", n == (int)before && got[0] == 3);

    suite("A CLR empties it");
    BlackBox::markCleared();
    ck("index builds", LogIndex::rebuild(ring, 2));
    ck("no kept rows", LogIndex::count() == 0);

    suite("Released, it shows everything, as before");
    for (uint16_t i = 1; i <= 3; i++) BlackBox::noteDetection(det(7), false);
    LogIndex::release();
    ck("all three copies", LogIndex::count() == 3);

    return report();
}
