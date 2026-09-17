// SquachWatch-CYD — the bingo card's own arithmetic.
//
// What this guards: the card is only fun if it is winnable and if it keeps
// what it earned. A card with a repeated type has a square nobody can fill;
// a line that does not call, or marks that do not survive a restart, take
// away the only thing the game gives back.
#include "test_util.h"
#include "bingo.h"
#include "detection.h"
#include <Preferences.h>
#include <filesystem>
#include <stdlib.h>
#include <string.h>

// The clock the card asks for the day and the week. Held still here, then
// moved a week on by hand.
namespace Clock {
uint32_t g_day     = 20000;      // days since the epoch
bool     g_trusted = true;
bool     trusted()   { return g_trusted; }
uint32_t nowEpoch()  { return g_day * 86400u; }
uint8_t  weekday()   { return (uint8_t)((g_day + 4u) % 7u); }
uint32_t localDay()  { return g_day; }
}

static DetectionEngine eng;

// The card saves a second after the last mark, so a test that never moves
// millis() on never sees a write. Every step here takes the card past that.
static uint32_t g_ms = 1000;
static void settle() {
    Bingo::tick(g_ms);
    g_ms += 1500;
    Bingo::tick(g_ms);
    g_ms += 100;
}

static uint8_t fillRow(uint8_t row) {
    for (uint8_t k = 0; k < 4; k++) Bingo::note(Bingo::typeAt((uint8_t)(row * 4 + k)));
    settle();
    return Bingo::markedCount();
}

int main() {
    const char* dir = "bingo_test_nvs";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    setenv("SQUACHSIM_NVS", dir, 1);

    suite("A fresh card");
    Bingo::begin(eng);
    {
        bool everyCellFilled = true, noRepeats = true;
        for (uint8_t i = 0; i < Bingo::CELLS; i++) {
            const DetectionType a = Bingo::typeAt(i);
            if (a == DetectionType::UNKNOWN) everyCellFilled = false;
            for (uint8_t j = (uint8_t)(i + 1); j < Bingo::CELLS; j++)
                if (Bingo::typeAt(j) == a) noRepeats = false;
        }
        ck("sixteen squares, all real types", everyCellFilled);
        ck("no type twice -- a repeat is an unfillable square", noRepeats);
        ck("nothing marked yet", Bingo::markedCount() == 0 && Bingo::linesCalled() == 0);
    }

    suite("Marking");
    {
        Bingo::note(Bingo::typeAt(5));
        settle();
        ck("a sighting marks its square", Bingo::marked(5));
        ck("and only its square", Bingo::markedCount() == 1);
        ck("with the day on it", Bingo::markDay(5) >= 1 && Bingo::markDay(5) <= 7);
        DetectionType ev = DetectionType::UNKNOWN;
        ck("and says so once", Bingo::takeEvent(ev) == Bingo::Event::MARKED &&
                               ev == Bingo::typeAt(5));
        ck("then has nothing more to say", Bingo::takeEvent(ev) == Bingo::Event::NONE);
        // Seeing the same thing again is not a second square.
        Bingo::note(Bingo::typeAt(5));
        settle();
        ck("seeing it again changes nothing", Bingo::markedCount() == 1);
    }

    suite("A line");
    {
        fillRow(0);
        ck("four in a row is one line", Bingo::linesCalled() == 1);
        ck("the squares know they are in it", Bingo::inCalledLine(0) && Bingo::inCalledLine(3));
        ck("and the others do not", !Bingo::inCalledLine(6));
        DetectionType ev = DetectionType::UNKNOWN;
        ck("the line is what it announces", Bingo::takeEvent(ev) == Bingo::Event::LINE);
        ck("lines ever counts it", Bingo::linesEver() == 1);
    }

    suite("What it keeps across a restart");
    {
        const DetectionType was = Bingo::typeAt(0);
        const uint8_t marks = Bingo::markedCount();
        Bingo::begin(eng);        // as a reboot does
        ck("the same card", Bingo::typeAt(0) == was);
        ck("the same marks", Bingo::markedCount() == marks);
        ck("the same lines", Bingo::linesCalled() == 1);
    }

    suite("A full card, then the week turns");
    {
        for (uint8_t i = 0; i < Bingo::CELLS; i++) Bingo::note(Bingo::typeAt(i));
        settle();
        ck("sixteen of sixteen", Bingo::markedCount() == Bingo::CELLS);
        ck("ten lines", Bingo::linesCalled() == Bingo::LINES);
        DetectionType ev = DetectionType::UNKNOWN;
        ck("a full card announces itself", Bingo::takeEvent(ev) == Bingo::Event::FULL);
        ck("best card remembered", Bingo::bestFilled() == Bingo::CELLS);

        Clock::g_day += 7;        // next week
        settle();
        ck("a new card is dealt", Bingo::markedCount() == 0);
        ck("the finished one is counted", Bingo::cardsFilled() == 1);
        ck("and the streak is running", Bingo::streak() == 1 && Bingo::bestStreak() == 1);
        ck("it says a new card arrived", Bingo::takeEvent(ev) == Bingo::Event::NEW_CARD);
    }

    suite("A week that got away");
    {
        Bingo::note(Bingo::typeAt(0));
        Clock::g_day += 7;
        settle();
        ck("the streak ends", Bingo::streak() == 0);
        ck("the best streak stands", Bingo::bestStreak() == 1);
        ck("cards filled stands", Bingo::cardsFilled() == 1);
    }

    suite("Asking for a new card mid-week");
    {
        fillRow(0);
        const uint16_t lines = Bingo::linesEver();
        Bingo::newCard();
        ck("the card is empty again", Bingo::markedCount() == 0 && Bingo::linesCalled() == 0);
        ck("the lines already called are kept", Bingo::linesEver() == lines);
        ck("but the streak is gone", Bingo::streak() == 0);
    }

    suite("No clock, no weeks");
    {
        Clock::g_trusted = false;
        Bingo::note(Bingo::typeAt(2));
        settle();
        const uint8_t marks = Bingo::markedCount();
        Clock::g_day += 21;
        settle();
        ck("the card stands until somebody asks", Bingo::markedCount() == marks);
        Clock::g_trusted = true;
    }

    std::filesystem::remove_all(dir);
    return report();
}
