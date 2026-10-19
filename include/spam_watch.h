// SquachWatch-CYD — spotting a spam flood.
//
// A hacker convention, 2026-09-26: somebody broadcasting fake AirTags, and
// later fake Google tags, each burst from a fresh address. 91 AirTag alerts
// in 22 minutes, 50 of the 69 addresses heard exactly once -- and the only
// way to make the watch usable again was to switch AirTags off entirely.
//
// A real tag keeps announcing for as long as it is near. A fake is heard for
// one burst and never again. So the evidence is a pile of addresses of one
// type that were heard for a few seconds and then vanished -- or, when the
// flood is too fast for anything to live long enough to vanish, more new
// addresses of one type in a minute than any crowd produces. Either one trips
// the type into SPAM: one alert says so, and every sighting of that type after
// it is still logged but does not interrupt, until five minutes pass with no
// more evidence. Real tags of that type are held along with the fakes for
// those five minutes; that is the trade, and the alert says so.
//
// Header-only and free of Arduino, so the host tests drive it with their own
// clock and the emulator's stand-in engine carries one for nothing.
#pragma once
#include <stdint.h>

struct SpamWatch {
    static const uint8_t  TYPES      = 32;       // DetectionType::COUNT fits; see detection.cpp
    static const uint32_t SHORT_MS   = 10000;    // heard for less than this, then gone
    // Seven fakes' worth of evidence, times 16, after it has faded a little
    // between arrivals: eight in quick succession trip it, and a flood of one
    // every fifteen seconds (the convention's rate) trips it in about ten.
    static const uint16_t TRIP16     = 112;      // this much evidence trips it...
    static const uint32_t HALF_MS    = 90000;    // ...halving every ninety seconds
    static const uint16_t BURST_TRIP = 40;       // or this many new addresses of one type in a minute
    static const uint32_t QUIET_MS   = 300000;   // five minutes without evidence ends it

    struct Type {
        uint16_t score16   = 0;   // the evidence, times 16
        uint16_t fakes     = 0;   // short-lived addresses counted in this flood
        uint32_t lastMs    = 0;   // the last evidence
        uint8_t  active    = 0;
        uint8_t  announced = 0;
    };
    Type t[TYPES];

    // A log row went quiet. True the moment its type trips.
    bool noteVanish(uint8_t type, uint32_t heardMs, uint16_t hits, uint32_t now) {
        if (type >= TYPES || heardMs >= SHORT_MS || hits > 1) return false;
        return evidence(type, 1, now, false);
    }
    // New addresses of one type in the last minute. True the moment it trips.
    bool noteBurst(uint8_t type, uint16_t newInAMinute, uint32_t now) {
        if (type >= TYPES || newInAMinute < BURST_TRIP) return false;
        return evidence(type, newInAMinute, now, true);
    }
    // Whether sightings of this type should stay quiet right now. Ends a
    // flood that has gone five minutes without evidence.
    bool active(uint8_t type, uint32_t now) {
        if (type >= TYPES) return false;
        Type& x = t[type];
        if (x.active && now - x.lastMs > QUIET_MS) { x = Type(); }
        return x.active != 0;
    }
    // The one alert a flood gets: true once per flood.
    bool takeAnnounce(uint8_t type) {
        if (type >= TYPES || !t[type].active || t[type].announced) return false;
        t[type].announced = 1;
        return true;
    }
    uint16_t fakes(uint8_t type) const { return type < TYPES ? t[type].fakes : 0; }

private:
    bool evidence(uint8_t type, uint16_t n, uint32_t now, bool burst) {
        Type& x = t[type];
        uint32_t s  = x.score16;
        uint32_t el = x.lastMs ? now - x.lastMs : 0;
        while (el >= HALF_MS && s) { s >>= 1; el -= HALF_MS; }
        if (el >= HALF_MS) el = 0;
        s -= s * el / (2 * HALF_MS);                   // the rest of a half, near enough
        if (!x.active && s < 16) x.fakes = 0;          // a stray from long ago is not a flood
        s += (uint32_t)n * 16;
        x.score16 = (uint16_t)(s > 0xFFFF ? 0xFFFF : s);
        x.fakes   = (uint16_t)((uint32_t)x.fakes + n > 0xFFFF ? 0xFFFF : x.fakes + n);
        x.lastMs  = now ? now : 1;
        if (x.active) return false;
        if (burst || s >= TRIP16) { x.active = 1; x.announced = 0; return true; }
        return false;
    }
};
