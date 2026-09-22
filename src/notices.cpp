// SquachWatch-CYD — what Squachy notices. See notices.h.
#include "notices.h"
#include "dex.h"
#include "detection.h"
#include "blackbox.h"
#include "clock.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

namespace Notices {

namespace {

char s_buf[96];

// The first sentence of a card's text, so it fits a two-line bubble.
void firstSentence(const char* text, char* out, size_t cap) {
    size_t n = 0;
    while (text[n] && n + 1 < cap) {
        out[n] = text[n];
        if (text[n] == '.' || text[n] == '!' || text[n] == '?') { n++; break; }
        n++;
    }
    out[n] = 0;
    // Too long for the bubble even so: cut at a word and add a dot.
    if (n > 74) {
        size_t k = 70;
        while (k > 40 && out[k] != ' ') k--;
        out[k] = '.'; out[k + 1] = 0;
    }
}

// One of the types he has caught, or one he has not, at random.
DetectionType pickType(const DetectionEngine& eng, bool caught) {
    DetectionType pool[Dex::ENTRIES];
    uint8_t n = 0;
    for (uint8_t i = 0; i < Dex::ENTRIES; i++) {
        const DetectionType t = Dex::typeAt(i);
        if ((eng.lifetimeTypeCount(t) > 0) == caught) pool[n++] = t;
    }
    return n ? pool[random(0, n)] : DetectionType::UNKNOWN;
}

const char* loreLine(const DetectionEngine& eng) {
    const DetectionType t = pickType(eng, true);
    if (t == DetectionType::UNKNOWN) return nullptr;
    char s[80];
    firstSentence(Dex::lore(t), s, sizeof s);
    static const char* const OPENERS[] = { "Did you know? ", "Fun fact. ", "From the DEX: ", "" };
    snprintf(s_buf, sizeof s_buf, "%s%s", OPENERS[random(0, 4)], s);
    return s_buf;
}

const char* nudgeLine(const DetectionEngine& eng) {
    const DetectionType t = pickType(eng, false);
    if (t == DetectionType::UNKNOWN) return nullptr;
    char s[72];
    firstSentence(Dex::hint(t), s, sizeof s);
    static const char* const OPENERS[] = { "Card %02u is still blank. %s", "Still no %02u. %s", "No. %02u? Never met one. %s" };
    snprintf(s_buf, sizeof s_buf, OPENERS[random(0, 3)], (unsigned)(Dex::indexOf(t) + 1), s);
    return s_buf;
}

// Sightings today and yesterday, from the black box. Counted at most once
// a minute: it is a walk of the whole flash ring.
uint32_t s_countedAt = 0;
uint16_t s_today = 0, s_yesterday = 0;
uint32_t dayOf(uint32_t epoch) {
    struct tm tmv;
    const time_t t = (time_t)epoch;
    localtime_r(&t, &tmv);
    // Days since a fixed point, local: enough to compare two days.
    return (uint32_t)(tmv.tm_year * 400 + tmv.tm_yday);
}
void countDays() {
    const uint32_t now = millis();
    if (s_countedAt && now - s_countedAt < 60000u) return;
    s_countedAt = now ? now : 1;
    struct W { uint32_t today; uint16_t t, y; } w = { dayOf(Clock::nowEpoch()), 0, 0 };
    BlackBox::forEachDetection([](const BlackBox::DetRecord& r, void* c) {
        W& w = *(W*)c;
        if (!r.epoch) return true;
        const uint32_t d = dayOf(r.epoch);
        if (d == w.today) w.t++;
        else if (d + 1 == w.today) w.y++;
        else return false;   // newest first: past yesterday, done
        return true;
    }, &w);
    s_today = w.t; s_yesterday = w.y;
}

uint32_t s_dayLineAt = 0;   // millis() of the last today/yesterday line
const char* dayLine() {
    if (!Clock::trusted()) return nullptr;
    const uint32_t now = millis();
    if (s_dayLineAt && now - s_dayLineAt < 3600000u) return nullptr;   // once an hour
    countDays();
    if (!s_yesterday && !s_today) return nullptr;
    s_dayLineAt = now;
    if (!s_yesterday)            snprintf(s_buf, sizeof s_buf, "%u today. Yesterday: nothing. Odd.", (unsigned)s_today);
    else if (s_today > s_yesterday * 2 && s_today >= 6)
                                 snprintf(s_buf, sizeof s_buf, "%u today, %u yesterday. Busy. Suspicious.", (unsigned)s_today, (unsigned)s_yesterday);
    else if (s_today * 2 < s_yesterday && s_yesterday >= 6)
                                 snprintf(s_buf, sizeof s_buf, "Quieter than yesterday. %u vs %u. Suspicious.", (unsigned)s_today, (unsigned)s_yesterday);
    else                         snprintf(s_buf, sizeof s_buf, "%u today, %u yesterday. About par.", (unsigned)s_today, (unsigned)s_yesterday);
    return s_buf;
}

// The calendar. Each key fires once per local day; the 3:33 one once per
// boot, since nobody is up for it twice.
uint32_t s_calDay = 0; uint16_t s_calSaid = 0;   // bit per key, this day
bool once(uint8_t key) {
    const uint32_t d = Clock::localDay();
    if (d != s_calDay) { s_calDay = d; s_calSaid = 0; }
    if (s_calSaid & (1u << key)) return false;
    s_calSaid |= (1u << key);
    return true;
}
const char* calendarLine() {
    if (!Clock::trusted()) return nullptr;
    struct tm tmv;
    const time_t t = (time_t)Clock::nowEpoch();
    localtime_r(&t, &tmv);
    const int mon = tmv.tm_mon + 1, day = tmv.tm_mday, hr = tmv.tm_hour, mn = tmv.tm_min;
    if (hr == 3 && mn >= 30 && mn <= 36 && once(0)) return "3:33. The witching hour. I'm the witch.";
    if (tmv.tm_wday == 5 && day == 13 && once(1)) return "Friday the 13th. Every camera's a little spookier.";
    if (mon == 10 && day >= 25 && once(2)) return day == 31 ? "Halloween. Finally, a night I blend in." : "Halloween week. I've been practising my lurk.";
    if (mon == 12 && day == 31 && hr >= 20 && once(3)) return "New year's coming. Same cameras, new digits.";
    if (mon == 1 && day == 1 && once(4)) return "New year. I resolve to be seen by nobody.";
    if (mon == 2 && day == 14 && once(5)) return "Valentine's. The AirTags are extra clingy today.";
    if (mon == 4 && day == 1 && once(6)) return "Nothing detected. Ever. April fools.";
    if (mon == 6 && day == 21 && once(7)) return "Longest day. Longest shift.";
    if (mon == 12 && day == 21 && once(8)) return "Shortest day. My kind of day.";
    if (mon == 12 && day == 25 && once(9)) return "Merry whatever. Every new gadget under a tree is on my list tomorrow.";
    if (mon == 7 && day == 4 && once(10)) return "Fireworks tonight. Drones too, probably.";
    return nullptr;
}

}  // namespace

const char* idleLine(const DetectionEngine& eng) {
    // The calendar first, since it only ever speaks on its day; then the
    // rest by dice.
    if (const char* c = calendarLine()) return c;
    switch (random(0, 6)) {
        case 0: case 1: return loreLine(eng);
        case 2:         return nudgeLine(eng);
        case 3:         return dayLine();
        default:        return nullptr;
    }
}

}  // namespace Notices
