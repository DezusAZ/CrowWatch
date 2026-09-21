// SquachWatch-CYD — which of the black box's kept sightings the LOG shows.
#include "log_index.h"
#include "blackbox.h"
#include <stdlib.h>
#include <string.h>

namespace LogIndex {

// Newest-first positions of the rows to show, as they were at the build.
static uint16_t* s_pos      = nullptr;
static uint16_t  s_n        = 0;
static uint16_t  s_cap      = 0;
static uint16_t  s_builtAt  = 0;      // detectionsKept() when built
static bool      s_ok       = false;

uint32_t key(const uint8_t* mac, uint8_t type) {
    // FNV-1a over the six MAC bytes and the type. 32 bits over at most a
    // couple of thousand devices: a collision, which would hide one row, is
    // about a one-in-a-thousand chance for a full flash.
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) { h ^= mac[i]; h *= 16777619u; }
    h ^= type; h *= 16777619u;
    return h ? h : 1;   // 0 marks an empty slot below
}

namespace {

// Open-addressed set of keys; grows by doubling at half full. Only lives for
// the length of a rebuild.
struct KeySet {
    uint32_t* t = nullptr;
    uint16_t  cap = 0, n = 0;
    static const uint16_t MAX_CAP = 8192;   // 32 KB would be too much; see grow()

    bool init(uint16_t c) {
        t = (uint32_t*)calloc(c, sizeof(uint32_t));
        cap = t ? c : 0;
        return t != nullptr;
    }
    void done() { free(t); t = nullptr; cap = n = 0; }

    bool grow() {
        if (cap >= MAX_CAP / 2) return false;   // stop at 4096 slots, 16 KB
        uint32_t* old = t;
        const uint16_t oldCap = cap;
        if (!init((uint16_t)(oldCap * 2))) { t = old; cap = oldCap; return false; }
        n = 0;
        for (uint16_t i = 0; i < oldCap; i++) if (old[i]) add(old[i]);
        free(old);
        return true;
    }

    // True if k was not there and is now. False if it was there -- or, with
    // `full` set, if there was no room to add it.
    bool add(uint32_t k, bool* full = nullptr) {
        if ((uint32_t)(n + 1) * 2 > cap && !grow()) {
            if (full) *full = true;
            return false;
        }
        uint16_t i = (uint16_t)(k & (cap - 1));
        while (t[i]) {
            if (t[i] == k) return false;
            i = (uint16_t)((i + 1) & (cap - 1));
        }
        t[i] = k;
        n++;
        return true;
    }
};

bool pushPos(uint16_t p) {
    if (s_n == s_cap) {
        const uint16_t c = s_cap ? (uint16_t)(s_cap * 2) : 64;
        uint16_t* grown = (uint16_t*)realloc(s_pos, c * sizeof(uint16_t));
        if (!grown) return false;
        s_pos = grown;
        s_cap = c;
    }
    s_pos[s_n++] = p;
    return true;
}

}  // namespace

void release() {
    free(s_pos);
    s_pos = nullptr;
    s_n = s_cap = 0;
    s_ok = false;
}

bool rebuild(const uint32_t* ringKeys, uint16_t ringN) {
    s_n = 0;
    s_ok = false;
    KeySet seen;
    if (!seen.init(256)) { release(); return false; }
    for (uint16_t i = 0; i < ringN; i++) seen.add(ringKeys[i]);

    struct W { KeySet* seen; uint16_t idx; bool failed; } w = { &seen, 0, false };
    BlackBox::forEachDetection([](const BlackBox::DetRecord& r, void* c) {
        W& w = *(W*)c;
        bool full = false;
        if (w.seen->add(key(r.mac, r.type), &full)) {
            if (!pushPos(w.idx)) { w.failed = true; return false; }
        } else if (full) {
            w.failed = true;
            return false;
        }
        w.idx++;
        return true;
    }, &w);
    seen.done();
    if (w.failed) { release(); return false; }
    s_builtAt = BlackBox::detectionsKept();
    s_ok = true;
    return true;
}

bool filtering() { return s_ok; }

uint16_t count() { return s_ok ? s_n : BlackBox::detectionsKept(); }

bool position(uint16_t row, uint16_t& newestIdx) {
    if (!s_ok) { newestIdx = row; return row < BlackBox::detectionsKept(); }
    if (row >= s_n) return false;
    const uint16_t now = BlackBox::detectionsKept();
    if (now < s_builtAt) return false;     // a CLR since the build; caller rebuilds
    newestIdx = (uint16_t)(s_pos[row] + (now - s_builtAt));
    return true;
}

}  // namespace LogIndex
