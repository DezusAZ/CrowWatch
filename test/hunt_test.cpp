// SquachWatch-CYD — the saved half of an easter-egg hunt.
//
// What this guards: the unlocks have always been saved, but the PROGRESS
// towards one used to live in memory alone. The starfield eye wants two big
// eyes caught in a row, and big eyes are minutes apart -- so a restart in
// between silently threw the first one away and nothing on the board said
// so. This checks the counter goes to the settings store, comes back after a
// restart, is written only when it changes, and does not come back stale
// once the hunt is finished.
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

int main() {
    // A real file behind the store, so "a restart" means what it says.
    setenv("SQUACHSIM_NVS", "out", 1);
    remove("out/settings.nvs");

    suite("A fresh device");
    Settings::load();
    ck("no hunt is under way", Settings::huntProgress(Settings::Hunt::EYE_STREAK) == 0);

    suite("One eye caught");
    Settings::setHuntProgress(Settings::Hunt::EYE_STREAK, 1);
    ck("the board knows", Settings::huntProgress(Settings::Hunt::EYE_STREAK) == 1);

    suite("And then it restarts");
    Settings::load();                       // as a reboot does
    ck("the first eye still counts", Settings::huntProgress(Settings::Hunt::EYE_STREAK) == 1);

    suite("The second eye finishes it");
    Settings::setHuntProgress(Settings::Hunt::EYE_STREAK, 0);
    Settings::load();
    ck("nothing stale is kept", Settings::huntProgress(Settings::Hunt::EYE_STREAK) == 0);

    suite("Writing the same value again");
    Settings::setHuntProgress(Settings::Hunt::EYE_STREAK, 2);
    Settings::setHuntProgress(Settings::Hunt::EYE_STREAK, 2);
    ck("the value stands", Settings::huntProgress(Settings::Hunt::EYE_STREAK) == 2);

    suite("A counter that does not exist");
    Settings::setHuntProgress(Settings::Hunt::COUNT, 7);
    ck("is ignored, not written over a real one",
       Settings::huntProgress(Settings::Hunt::EYE_STREAK) == 2 &&
       Settings::huntProgress(Settings::Hunt::COUNT) == 0);

    return report();
}
