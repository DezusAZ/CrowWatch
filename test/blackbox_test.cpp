// SquachWatch-CYD — the black box's rings, against the emulator's flash.
//
// What this guards: the flash it writes is the only record of a crash or of
// a night's detections, and nothing on the board says when a ring has gone
// wrong. The newest must come back first, a full ring must keep its newest
// and lose its oldest, a CLR must hide what came before it, a record torn by
// a power cut must not be read, and the bytes an old board left in this
// space (it was SPIFFS before v1.7.0) must not be mistaken for ours.
#include "test_util.h"
#include "blackbox.h"
#include "clock.h"
#include <cstring>
#include <cstdlib>

// The two things blackbox.cpp asks of the clock.
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
    d.vendor = "Apple";
    snprintf(d.name, sizeof d.name, "tag %u", (unsigned)n);
    return d;
}

static uint16_t macNo(const BlackBox::DetRecord& r) { return (uint16_t)((r.mac[4] << 8) | r.mac[5]); }

struct Seen { uint16_t n; uint16_t first; uint16_t last; bool ordered; };
static Seen walkDets() {
    Seen s = { 0, 0, 0, true };
    BlackBox::forEachDetection([](const BlackBox::DetRecord& r, void* c) {
        Seen& s = *(Seen*)c;
        const uint16_t m = macNo(r);
        if (s.n == 0) s.first = m;
        else if (m >= s.last) s.ordered = false;   // newest first: numbers fall
        s.last = m;
        s.n++;
        return true;
    }, &s);
    return s;
}

int main() {
    suite("An empty space");
    ck("begin() takes it", BlackBox::begin());
    ck("nothing kept", BlackBox::detectionsKept() == 0 && BlackBox::crashesKept() == 0);
    ck("the first boot is boot 1", BlackBox::bootNumber() == 1);

    suite("Detections come back newest first");
    for (uint16_t i = 1; i <= 100; i++) BlackBox::noteDetection(det(i), false);
    Seen s = walkDets();
    ck("100 kept", BlackBox::detectionsKept() == 100 && s.n == 100);
    ck("newest first", s.first == 100 && s.last == 1 && s.ordered);
    {
        BlackBox::DetRecord got;
        memset(&got, 0, sizeof got);
        BlackBox::forEachDetection([](const BlackBox::DetRecord& r, void* c) {
            *(BlackBox::DetRecord*)c = r;
            return false;
        }, &got);
        ck("the fields survive", got.type == (uint8_t)DetectionType::AIRTAG && got.rssi == -60 &&
                                 strcmp(got.vendor, "Apple") == 0 && strcmp(got.name, "tag 100") == 0 &&
                                 got.epoch == Clock::g_epoch && got.boot == 1);
    }

    suite("A reopened board finds the same records");
    BlackBox::testReopen();
    s = walkDets();
    ck("still 100, newest first", BlackBox::detectionsKept() == 100 && s.n == 100 && s.first == 100 && s.ordered);
    ck("the next boot is boot 2", BlackBox::bootNumber() == 2);

    suite("A full ring keeps its newest");
    for (uint16_t i = 101; i <= 5000; i++) BlackBox::noteDetection(det(i), false);
    s = walkDets();
    ck("no more than 30 sectors of records", s.n <= 30 * 63);
    ck("at least 29 sectors of records", s.n >= 29 * 63);
    ck("the newest is the last written", s.first == 5000 && s.ordered);
    ck("the oldest kept is a recent one", s.last == (uint16_t)(5000 - s.n + 1));
    BlackBox::testReopen();
    Seen s2 = walkDets();
    ck("and the same after a reopen", s2.n == s.n && s2.first == 5000 && s2.last == s.last && s2.ordered);

    suite("CLR hides what came before it");
    BlackBox::markCleared();
    ck("nothing kept", BlackBox::detectionsKept() == 0 && walkDets().n == 0);
    BlackBox::noteDetection(det(6001), false);
    BlackBox::noteDetection(det(6002), false);
    s = walkDets();
    ck("only what came after", s.n == 2 && s.first == 6002 && s.last == 6001);
    BlackBox::testReopen();
    ck("and the same after a reopen", BlackBox::detectionsKept() == 2 && walkDets().n == 2);

    suite("A torn record is skipped, and its slot not reused");
    {
        uint32_t off = 0;
        const bool found = BlackBox::testNextDetectionSlot(off);
        ck("the next slot is found", found);
        uint8_t* f = BlackBox::testFlash();
        f[off + 5] = 0x00;   // half a write: some bits cleared, no checksum
        BlackBox::testReopen();
        ck("the torn one is not read", walkDets().n == 2);
        BlackBox::noteDetection(det(6003), false);
        s = walkDets();
        ck("the next write lands after it", s.n == 3 && s.first == 6003);
    }

    suite("Crashes");
    {
        BlackBox::BootRecord b;
        memset(&b, 0, sizeof b);
        b.reason = 1;   // power on
        BlackBox::noteBoot(b);
        ck("a power-on is not a crash", BlackBox::crashesKept() == 0);
        memset(&b, 0, sizeof b);
        b.reason = 4;   // panic
        b.flags = BlackBox::BOOT_DUMP;
        b.pc = 0x400D1234;
        strcpy(b.task, "nimble_host");
        b.epoch = 1789561111u;
        BlackBox::noteBoot(b);
        BlackBox::testReopen();
        BlackBox::BootRecord last;
        ck("a panic is", BlackBox::crashesKept() == 1 && BlackBox::lastCrash(last));
        ck("with where it died", last.pc == 0x400D1234 && strcmp(last.task, "nimble_host") == 0 &&
                                 last.epoch == 1789561111u && last.version[0] != 0);
        const uint16_t before = BlackBox::bootNumber();
        for (int i = 0; i < 300; i++) { memset(&b, 0, sizeof b); b.reason = (i % 3) ? 3 : 6; BlackBox::noteBoot(b); BlackBox::testReopen(); }
        ck("boot numbers keep rising", BlackBox::bootNumber() == before + 300);
        ck("the boot ring holds at most two sectors", BlackBox::crashesKept() <= 2 * 63 / 3 + 2);
    }

    suite("Somebody else's bytes are not ours");
    {
        uint8_t* f = BlackBox::testFlash();
        srand(7);
        for (uint32_t i = 0; i < 32u * 4096u; i++) f[i] = (uint8_t)rand();
        BlackBox::testReopen();
        ck("nothing kept from noise", BlackBox::detectionsKept() == 0 && BlackBox::crashesKept() == 0);
        ck("the boot count starts over", BlackBox::bootNumber() == 1);
        BlackBox::noteDetection(det(7001), false);
        BlackBox::testReopen();
        s = walkDets();
        ck("and it writes over it cleanly", s.n == 1 && s.first == 7001);
    }

    suite("The wipe erases everything");
    BlackBox::wipe();
    BlackBox::testReopen();
    ck("nothing kept", BlackBox::detectionsKept() == 0 && BlackBox::crashesKept() == 0 &&
                       BlackBox::bootNumber() == 1);
    {
        uint8_t* f = BlackBox::testFlash();
        bool blank = true;
        for (uint32_t i = 0; i < 32u * 4096u; i++) if (f[i] != 0xFF) { blank = false; break; }
        ck("every byte erased", blank);
    }

    return report();
}
