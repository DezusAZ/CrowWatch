// SquachWatch-CYD — the regulars. See regulars.h.
#include "regulars.h"
#include "clock.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

namespace Regulars {

namespace {

// Neighbours' names. Short, so they fit a LOG row beside the type, and the
// kind of name a doorbell would have if doorbells had names.
const char* const NAMES[] = {
    "Gary", "Deborah", "Carl", "Linda", "Terry", "Pam", "Doug", "Brenda",
    "Kevin", "Sheila", "Dennis", "Marge", "Glen", "Rhonda", "Bruce", "Janet",
    "Norm", "Dot", "Clive", "Bev", "Earl", "Phyllis", "Stan", "Judy",
    "Lyle", "Barb", "Wendell", "Trish", "Roger", "Gail", "Vern", "Nadine",
    "Keith", "Lois", "Todd", "Marlene", "Wayne", "Denise", "Bob", "Carol",
    "Neil", "Sandra", "Rick", "Peggy", "Chuck", "Elaine", "Walt", "Dolores",
};
const uint8_t NAMES_N = sizeof(NAMES) / sizeof(NAMES[0]);

struct __attribute__((packed)) Entry {
    uint8_t  mac[6];
    uint8_t  type;
    uint8_t  days;
    uint32_t lastDay;   // the local day it was last counted on; 0 = empty
    uint8_t  nameIdx;
    uint8_t  fresh;     // just crossed the line; cleared by takeNewRegular()
};

Entry    s_t[CAP];
bool     s_dirty   = false;
uint32_t s_changed = 0;
bool     s_began   = false;
Preferences s_prefs;
const char* NS  = "regulars";
const char* KEY = "tab";

int find(const uint8_t* mac) {
    for (uint8_t i = 0; i < CAP; i++)
        if (s_t[i].lastDay && memcmp(s_t[i].mac, mac, 6) == 0) return i;
    return -1;
}

// A name nobody in the table has, starting from a hash of the address so
// the same device gets the same name on every board that meets it.
uint8_t pickName(const uint8_t* mac) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) { h ^= mac[i]; h *= 16777619u; }
    for (uint8_t k = 0; k < NAMES_N; k++) {
        const uint8_t idx = (uint8_t)((h + k) % NAMES_N);
        bool taken = false;
        for (uint8_t i = 0; i < CAP; i++) if (s_t[i].lastDay && s_t[i].nameIdx == idx) taken = true;
        if (!taken) return idx;
    }
    return (uint8_t)(h % NAMES_N);
}

}  // namespace

void begin() {
    memset(s_t, 0, sizeof s_t);
    s_prefs.begin(NS, false);
    size_t have = s_prefs.getBytesLength(KEY);
    if (have > sizeof s_t) have = sizeof s_t;
    if (have >= sizeof(Entry)) s_prefs.getBytes(KEY, s_t, have - have % sizeof(Entry));
    s_began = true;
}

void noteOnDay(const uint8_t* mac, DetectionType type, uint32_t day) {
    if (!day) return;
    int i = find(mac);
    if (i < 0) {
        // The empty slot, else the one not seen for longest.
        int oldest = 0;
        for (uint8_t k = 0; k < CAP; k++) {
            if (!s_t[k].lastDay) { oldest = k; break; }
            if (s_t[k].lastDay < s_t[oldest].lastDay) oldest = k;
        }
        i = oldest;
        memset(&s_t[i], 0, sizeof(Entry));
        memcpy(s_t[i].mac, mac, 6);
        s_t[i].type    = (uint8_t)type;
        s_t[i].nameIdx = pickName(mac);
    }
    if (s_t[i].lastDay == day) return;
    s_t[i].lastDay = day;
    if (s_t[i].days < 255) s_t[i].days++;
    if (s_t[i].days == DAYS_TO_BE) s_t[i].fresh = 1;
    s_dirty = true;
    s_changed = millis();
}

void note(const uint8_t* mac, DetectionType type) {
    if (!Clock::trusted()) return;
    noteOnDay(mac, type, Clock::localDay());
}

void tick(uint32_t now) {
    if (!s_began || !s_dirty || now - s_changed < 10000u) return;
    s_dirty = false;
    s_prefs.putBytes(KEY, s_t, sizeof s_t);
}

const char* nameFor(const uint8_t* mac) {
    const int i = find(mac);
    if (i < 0 || s_t[i].days < DAYS_TO_BE) return nullptr;
    return NAMES[s_t[i].nameIdx % NAMES_N];
}

uint8_t daysFor(const uint8_t* mac) {
    const int i = find(mac);
    return i < 0 ? 0 : s_t[i].days;
}

bool takeNewRegular(const uint8_t* mac) {
    const int i = find(mac);
    if (i < 0 || !s_t[i].fresh) return false;
    s_t[i].fresh = 0;
    s_dirty = true;
    return true;
}

uint8_t count() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < CAP; i++) if (s_t[i].lastDay && s_t[i].days >= DAYS_TO_BE) n++;
    return n;
}

void reset() {
    memset(s_t, 0, sizeof s_t);
    s_dirty = false;
    if (s_began) s_prefs.putBytes(KEY, s_t, sizeof s_t);
}

}  // namespace Regulars
