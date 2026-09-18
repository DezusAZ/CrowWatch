// SquachWatch-CYD — a retired background stays retired.
//
// What this guards: the background is saved to NVS as a raw byte, so
// WIREFRAME TUNNEL could not be deleted from the middle of the enum
// without moving every board that had SYNTHWAVE or BLACK saved onto
// something else. The number therefore survives its background. Two
// things have to hold for that to be safe, and neither is visible by
// reading the enum: the picker must never hand it out again, and a
// board that already had it saved must come up somewhere sensible
// rather than on a band nothing paints.
#include "test_util.h"
#include "settings.h"
#include "theme.h"
#include "clock.h"
#include <Preferences.h>
#include <cstdlib>

// The two things settings.cpp reaches for that are not under test.
namespace Theme { void applyPalette(uint8_t) {} }
namespace Clock {
uint8_t     zoneCount()        { return 1; }
const char* zoneName(uint8_t)  { return "UTC"; }
void        applyZone(uint8_t) {}
}

using Settings::Background;

int main() {
    // A real file behind the store, so "a restart" means what it says.
    setenv("SQUACHSIM_NVS", "out", 1);
    remove("out/settings.nvs");

    Settings::load();

    suite("The picker never lands on it");
    // Forward and back, further than a full lap each way: the ring skips
    // anything unselectable, and one lap of each direction would miss a
    // skip that only fires from one side.
    bool seenFwd = false, seenBack = false;
    for (int i = 0; i < Settings::BACKGROUND_COUNT * 2; i++) {
        Settings::cycleBackground();
        if (Settings::background() == Background::TUNNEL) seenFwd = true;
    }
    for (int i = 0; i < Settings::BACKGROUND_COUNT * 2; i++) {
        Settings::cyclePrevBackground();
        if (Settings::background() == Background::TUNNEL) seenBack = true;
    }
    ck("two laps forward never reach it", !seenFwd);
    ck("two laps back never reach it",    !seenBack);
    ck("and it says so when asked",       !Settings::backgroundSelectable(Background::TUNNEL));

    suite("The ring still turns");
    // The skip is a `continue` in a bounded loop, so the failure mode to
    // rule out is not "it shows the tunnel" but "it stopped moving".
    Background before = Settings::background();
    Settings::cycleBackground();
    ck("cycling still changes the background", Settings::background() != before);

    suite("The desk screen picks from the same ring");
    for (int i = 0; i < Settings::BACKGROUND_COUNT * 2; i++) {
        Settings::cycleDeskBackground();
        if (Settings::deskBackground() == Background::TUNNEL) seenFwd = true;
    }
    ck("the desk never lands on it either", !seenFwd);

    suite("A board that had it saved");
    // Written the way an older firmware wrote it -- straight to the key,
    // behind the API that now refuses the value.
    {
        Preferences p;
        p.begin("settings", false);
        p.putUChar("bg", (uint8_t)Background::TUNNEL);
        p.end();
    }
    Settings::load();                       // as a reboot does
    ck("it does not come back", Settings::background() != Background::TUNNEL);
    ck("and it lands on something the picker offers",
       Settings::backgroundSelectable(Settings::background()));

    suite("Everything else still loads as saved");
    // The coercion is a single equality test; a stray >= or a switch on
    // the wrong value would quietly move the neighbours too.
    for (uint8_t v = 0; v < Settings::BACKGROUND_COUNT; v++) {
        if (v == (uint8_t)Background::TUNNEL) continue;
        if (v == (uint8_t)Background::BLACK)  continue;   // gated on boring mode
        Preferences p;
        p.begin("settings", false);
        p.putUChar("bg", v);
        p.end();
        Settings::load();
        if ((uint8_t)Settings::background() != v) {
            ck("a neighbouring background was moved", false);
            break;
        }
    }
    ck("every other saved background survives a reboot", true);

    return report();
}
