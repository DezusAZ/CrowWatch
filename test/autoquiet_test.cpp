// SquachWatch-CYD — AUTO SNOOZE: when one device may interrupt again.
//
// What this guards: a device that keeps dropping out of range and coming
// back raises a fresh alert every time it returns, and after a few of those
// the board is just shouting. AUTO SNOOZE caps how many it gets for free —
// but it must never go deaf, because this is a detector, so past the cap the
// device can still interrupt by coming CLOSER than it ever has.
//
// Every rule below is one that is wrong by default if nobody writes it down:
// noise must not count as closer, the bar must be the strongest it ever
// alerted at rather than the last one, the allowance has to come back
// eventually or the device that once came nearest becomes the one you can
// never hear from again, and the WATCH target must be exempt from all of it.
#include "test_util.h"
#include "detection.h"
#include <Arduino.h>
#include <cstring>

using Gate = DetectionEngine::AlertGate;

static DetectionEngine eng;
static const uint8_t MAC[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };

// The stand-in engine appends rather than merging, so the test seeds one
// entry and then moves it about. const_cast because logAt() hands out a
// read-only view for the screens; the radios are what normally write here.
static Detection* entry() { return const_cast<Detection*>(eng.logAt(0)); }

static void seed(int8_t rssi) {
    Detection d{};
    memcpy(d.mac, MAC, 6);
    d.rssi = rssi;
    d.type = DetectionType::AIRTAG;
    d.conf = Confidence::HIGH_CONF;
    d.active = true;
    d.firstSeen = d.lastSeen = millis();
    eng.postBle(d);
}

static void reset(int8_t rssi) {
    Detection* e = entry();
    e->alerts      = 0;
    e->quietBar    = 0;
    e->lastAlertMs = 0;
    e->rssi        = rssi;
}

int main() {
    SimClock::virtualTime = true;
    SimClock::nowMs = 500000;          // well past any decay window from zero
    eng.init();
    seed(-80);

    suite("Off means off");
    {
        bool allAllowed = true;
        for (int i = 0; i < 20; i++)
            if (eng.alertGate(MAC, 0, false) != Gate::ALLOW) allAllowed = false;
        ck("twenty alerts all get through", allAllowed);
        ck("and nothing was counted against it", entry()->alerts == 0);
    }

    suite("The watch target is never held back");
    {
        reset(-80);
        bool allAllowed = true;
        for (int i = 0; i < 20; i++)
            if (eng.alertGate(MAC, 5, true) != Gate::ALLOW) allAllowed = false;
        ck("asking to be told beats asking to be told less", allAllowed);
    }

    suite("Five free ones, then it has to earn them");
    reset(-80);
    ck("the first four are free",
       eng.alertGate(MAC, 5, false) == Gate::ALLOW &&
       eng.alertGate(MAC, 5, false) == Gate::ALLOW &&
       eng.alertGate(MAC, 5, false) == Gate::ALLOW &&
       eng.alertGate(MAC, 5, false) == Gate::ALLOW);
    ck("the fifth says it is the last one", eng.alertGate(MAC, 5, false) == Gate::ALLOW_LAST);
    ck("the sixth is held", eng.alertGate(MAC, 5, false) == Gate::HOLD);

    suite("Noise does not count as coming closer");
    // RSSI moves several dB with nothing moving. A device creeping up by
    // less than the margin has not approached, it has just been measured.
    entry()->rssi = (int8_t)(-80 + DetectionEngine::QUIET_MARGIN_DB - 1);
    ck("a wobble short of the margin is still held", eng.alertGate(MAC, 5, false) == Gate::HOLD);

    suite("Genuinely closer gets through");
    entry()->rssi = (int8_t)(-80 + DetectionEngine::QUIET_MARGIN_DB);
    ck("the margin exactly is enough", eng.alertGate(MAC, 5, false) == Gate::ALLOW);
    ck("...and the bar moved up behind it", eng.alertGate(MAC, 5, false) == Gate::HOLD);

    suite("The bar is the strongest it ever alerted at");
    // Not the most recent one: a device that came very close once and has
    // been far away since should not be able to interrupt from far away.
    reset(-90);
    ck("a distant first alert", eng.alertGate(MAC, 2, false) == Gate::ALLOW);
    entry()->rssi = -60;
    ck("then a close one, which is its last free", eng.alertGate(MAC, 2, false) == Gate::ALLOW_LAST);
    entry()->rssi = -70;
    ck("back at arm's length it is held, though that beats -90",
       eng.alertGate(MAC, 2, false) == Gate::HOLD);

    suite("Gone long enough and it starts over");
    reset(-80);
    ck("uses both", eng.alertGate(MAC, 2, false) == Gate::ALLOW &&
                    eng.alertGate(MAC, 2, false) == Gate::ALLOW_LAST);
    ck("and is held", eng.alertGate(MAC, 2, false) == Gate::HOLD);
    SimClock::nowMs += DetectionEngine::QUIET_DECAY_MS + 1;
    ck("after the decay window it is free again", eng.alertGate(MAC, 2, false) == Gate::ALLOW);
    ck("with a fresh allowance, not a spent one", entry()->alerts == 1);

    suite("A device nobody has logged");
    {
        const uint8_t other[6] = { 0x02, 0x99, 0x88, 0x77, 0x66, 0x55 };
        ck("is allowed through rather than silently swallowed",
           eng.alertGate(other, 5, false) == Gate::ALLOW);
    }

    return report();
}
