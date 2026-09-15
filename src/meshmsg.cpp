// SquachWatch-CYD — SquachMesh messages, the pure half. See include/meshmsg.h.
#include "meshmsg.h"

#if SQUACH_MESH
#include <string.h>

namespace MeshMsg {

const char SALT[] = "SquachWatch/msg/v1";

// 43 characters, codes 0..42. See the header before changing a single one.
const char TEXT_CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?!'-";
static const uint8_t TEXT_CHARSET_N = (uint8_t)(sizeof TEXT_CHARSET - 1);
static_assert(sizeof TEXT_CHARSET - 1 < TEXT_END, "END must stay past the last code");

uint16_t pickUniform(uint32_t (*rng)(), uint16_t n) {
    if (n == 0) return 0;
    // The largest multiple of n that fits in 2^32. Anything at or past it is
    // thrown away and drawn again. Done in 64 bits because 2^32 itself does
    // not fit in the type the generator returns.
    const uint64_t lim = 0x100000000ull - (0x100000000ull % n);
    uint32_t r;
    do { r = rng(); } while ((uint64_t)r >= lim);
    return (uint16_t)(r % n);
}

void roll(uint32_t (*rng)(), uint16_t out[PHRASE_WORDS]) {
    for (uint8_t i = 0; i < PHRASE_WORDS; i++) out[i] = pickUniform(rng, WORD_N);
}

size_t phraseText(const uint16_t idx[PHRASE_WORDS], char* out, size_t cap) {
    size_t n = 0;
    for (uint8_t i = 0; i < PHRASE_WORDS; i++) {
        if (idx[i] >= WORD_N) return 0;
        const char* w = WORDS[idx[i]];
        if (i) {
            if (n + 1 >= cap) return 0;
            out[n++] = ' ';
        }
        for (size_t j = 0; w[j]; j++) {
            if (n + 1 >= cap) return 0;
            out[n++] = w[j];
        }
    }
    if (n >= cap) return 0;
    out[n] = '\0';
    return n;
}

uint8_t wordsStartingWith(char c, uint16_t* out, uint8_t cap) {
    uint8_t n = 0;
    for (uint16_t i = 0; i < WORD_N && n < cap; i++)
        if (WORDS[i][0] == c) out[n++] = i;
    return n;
}

// ---- text ----------------------------------------------------------------------
static int codeOf(char c) {
    for (uint8_t i = 0; i < TEXT_CHARSET_N; i++) if (TEXT_CHARSET[i] == c) return i;
    return -1;
}

bool textChar(char c) { return c && codeOf(c) >= 0; }

uint8_t textParts(const char* s) {
    if (!s || !s[0]) return 0;
    size_t n = 0;
    for (; s[n]; n++) {
        if (n >= TEXT_MAX || !textChar(s[n])) return 0;
    }
    return (uint8_t)((n + TEXT_PART_CHARS - 1) / TEXT_PART_CHARS);
}

// Sixteen six-bit codes into twelve bytes, most significant bit first.
static void pack(const uint8_t codes[TEXT_PART_CHARS], uint8_t out[TEXT_PART_BYTES]) {
    memset(out, 0, TEXT_PART_BYTES);
    for (uint8_t i = 0; i < TEXT_PART_CHARS; i++)
        for (uint8_t b = 0; b < 6; b++)
            if (codes[i] & (0x20 >> b)) {
                const unsigned bit = i * 6u + b;
                out[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
            }
}

static void unpack(const uint8_t in[TEXT_PART_BYTES], uint8_t codes[TEXT_PART_CHARS]) {
    for (uint8_t i = 0; i < TEXT_PART_CHARS; i++) {
        uint8_t v = 0;
        for (uint8_t b = 0; b < 6; b++) {
            const unsigned bit = i * 6u + b;
            v = (uint8_t)((v << 1) | ((in[bit / 8] >> (7 - bit % 8)) & 1));
        }
        codes[i] = v;
    }
}

// ---- the frame -------------------------------------------------------------------
void nonceFor(const uint8_t mac[6], uint32_t counter, uint8_t nonce[NONCE_LEN]) {
    memcpy(nonce, mac, 6);
    nonce[6] = (uint8_t)counter;
    nonce[7] = (uint8_t)(counter >> 8);
    nonce[8] = (uint8_t)(counter >> 16);
    nonce[9] = nonce[10] = nonce[11] = nonce[12] = 0;
}

bool isFrame(const uint8_t* in, size_t len) {
    return in && len >= sizeof MAGIC && memcmp(in, MAGIC, sizeof MAGIC) == 0;
}

bool parseHeader(const uint8_t* in, size_t len, uint32_t& counter, uint8_t& kind) {
    if (!isFrame(in, len) || len < HDR_LEN) return false;
    if ((in[2] >> 4) != VERSION) return false;
    kind    = in[2] & 0x0F;
    counter = (uint32_t)in[3] | ((uint32_t)in[4] << 8) | ((uint32_t)in[5] << 16);
    return true;
}

static void writeHeader(uint32_t counter, uint8_t kind, uint8_t* out) {
    memcpy(out, MAGIC, sizeof MAGIC);
    out[2] = (uint8_t)((VERSION << 4) | (kind & 0x0F));
    out[3] = (uint8_t)counter;
    out[4] = (uint8_t)(counter >> 8);
    out[5] = (uint8_t)(counter >> 16);
}

// A canned line is one sealed byte under its kind.
static size_t sealByte(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                       uint8_t kind, uint8_t v, uint8_t* out, size_t cap) {
    if (cap < CANNED_FRAME_LEN || counter > COUNTER_MAX) return 0;
    writeHeader(counter, kind, out);
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    if (!c.seal(nonce, out, HDR_LEN, &v, 1, out + HDR_LEN, out + HDR_LEN + 1)) return 0;
    return CANNED_FRAME_LEN;
}

size_t sealCanned(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                  uint8_t canned, uint8_t* out, size_t cap) {
    if (canned >= CANNED_N) return 0;
    return sealByte(c, mac, counter, KIND_CANNED, canned, out, cap);
}

size_t sealEmote(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                 uint8_t emote, uint8_t setup, uint8_t* out, size_t cap) {
    if (emote >= (uint8_t)Emote::COUNT) return 0;
    if (cap < EMOTE_FRAME_LEN || counter > COUNTER_MAX) return 0;
    writeHeader(counter, KIND_EMOTE, out);
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    const uint8_t pt[2] = { emote, setup };
    if (!c.seal(nonce, out, HDR_LEN, pt, 2, out + HDR_LEN, out + HDR_LEN + 2)) return 0;
    return EMOTE_FRAME_LEN;
}

size_t sealTextPart(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                    const char* text, uint8_t part, uint8_t total,
                    uint8_t* out, size_t cap) {
    if (cap < TEXT_FRAME_LEN || counter > COUNTER_MAX) return 0;
    if (total == 0 || textParts(text) != total || part >= total) return 0;
    uint8_t codes[TEXT_PART_CHARS];
    const size_t n = strlen(text), at = (size_t)part * TEXT_PART_CHARS;
    for (uint8_t i = 0; i < TEXT_PART_CHARS; i++)
        codes[i] = (at + i < n) ? (uint8_t)codeOf(text[at + i]) : TEXT_END;
    uint8_t pt[1 + TEXT_PART_BYTES];
    pt[0] = (uint8_t)((part << 4) | total);
    pack(codes, pt + 1);
    writeHeader(counter, KIND_TEXT, out);
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    if (!c.seal(nonce, out, HDR_LEN, pt, sizeof pt, out + HDR_LEN, out + HDR_LEN + sizeof pt))
        return 0;
    return TEXT_FRAME_LEN;
}

static Open openByte(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                     uint8_t want, uint32_t& counter, uint8_t& v) {
    if (!isFrame(in, len)) return Open::NOT_OURS;
    uint8_t kind;
    if (!parseHeader(in, len, counter, kind)) return Open::BAD_FORMAT;
    // Exact length, not a minimum: bytes after the tag are bytes nothing
    // authenticated, and a format that tolerates them has somewhere to hide.
    if (kind != want || len != CANNED_FRAME_LEN) return Open::BAD_FORMAT;
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    uint8_t pt = 0;
    if (!c.open(nonce, in, HDR_LEN, in + HDR_LEN, 1, in + HDR_LEN + 1, &pt)) return Open::BAD_TAG;
    v = pt;
    return Open::OK;
}

Open openCanned(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                uint32_t& counter, uint8_t& canned) {
    uint8_t pt = 0;
    const Open r = openByte(c, mac, in, len, KIND_CANNED, counter, pt);
    if (r != Open::OK) return r;
    canned = pt;
    return (pt < CANNED_N) ? Open::OK : Open::UNKNOWN_LINE;
}

Open openEmote(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
               uint32_t& counter, uint8_t& emote, uint8_t& setup) {
    if (!isFrame(in, len)) return Open::NOT_OURS;
    uint8_t kind;
    if (!parseHeader(in, len, counter, kind)) return Open::BAD_FORMAT;
    if (kind != KIND_EMOTE || len != EMOTE_FRAME_LEN) return Open::BAD_FORMAT;
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    uint8_t pt[2] = { 0, 0 };
    if (!c.open(nonce, in, HDR_LEN, in + HDR_LEN, 2, in + HDR_LEN + 2, pt)) return Open::BAD_TAG;
    emote = pt[0];
    setup = pt[1];
    return (pt[0] < (uint8_t)Emote::COUNT) ? Open::OK : Open::UNKNOWN_LINE;
}

Open openTextPart(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                  uint32_t& counter, uint8_t& part, uint8_t& total,
                  char chars[TEXT_PART_CHARS + 1]) {
    if (!isFrame(in, len)) return Open::NOT_OURS;
    uint8_t kind;
    if (!parseHeader(in, len, counter, kind)) return Open::BAD_FORMAT;
    if (kind != KIND_TEXT || len != TEXT_FRAME_LEN) return Open::BAD_FORMAT;
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    uint8_t pt[1 + TEXT_PART_BYTES];
    if (!c.open(nonce, in, HDR_LEN, in + HDR_LEN, sizeof pt, in + HDR_LEN + sizeof pt, pt))
        return Open::BAD_TAG;
    part  = pt[0] >> 4;
    total = pt[0] & 0x0F;
    // Authentic, but not a shape this build can place. Refused rather than
    // guessed at: a part put in the wrong slot is a message said wrongly.
    if (total == 0 || total > TEXT_PARTS_MAX || part >= total || counter < part)
        return Open::BAD_FORMAT;
    uint8_t codes[TEXT_PART_CHARS];
    unpack(pt + 1, codes);
    uint8_t n = 0;
    for (; n < TEXT_PART_CHARS && codes[n] != TEXT_END; n++)
        chars[n] = codes[n] < TEXT_CHARSET_N ? TEXT_CHARSET[codes[n]] : '?';
    chars[n] = '\0';
    return Open::OK;
}

// ---- reassembly -----------------------------------------------------------------
bool Assembly::add(const uint8_t mac[6], uint32_t counter, uint8_t part, uint8_t total,
                   const char* chars, char out[TEXT_MAX + 1], uint32_t& base) {
    if (total == 0 || total > TEXT_PARTS_MAX || part >= total || counter < part) return false;
    base = counter - part;
    stamp++;
    // This sender's slot, else an empty one, else the one heard from least
    // recently. One message in flight per sender: a new one replaces it.
    int slot = -1;
    for (uint8_t i = 0; i < N; i++)
        if (e[i].live && memcmp(e[i].mac, mac, 6) == 0) { slot = i; break; }
    if (slot < 0) {
        slot = 0;
        for (uint8_t i = 0; i < N; i++) {
            if (!e[i].live) { slot = i; break; }
            if (e[i].used < e[slot].used) slot = i;
        }
        e[slot].live = false;
    }
    E& s = e[slot];
    if (!s.live || s.base != base || s.total != total) {
        memcpy(s.mac, mac, 6);
        s.base  = base;
        s.total = total;
        s.have  = 0;
        s.live  = true;
    }
    s.used = stamp;
    size_t i = 0;
    for (; i < TEXT_PART_CHARS && chars[i]; i++) s.seg[part][i] = chars[i];
    s.seg[part][i] = '\0';
    s.have |= (uint8_t)(1u << part);
    if (s.have != (uint8_t)((1u << total) - 1)) return false;

    size_t n = 0;
    for (uint8_t p = 0; p < total; p++)
        for (size_t j = 0; s.seg[p][j] && n < TEXT_MAX; j++) out[n++] = s.seg[p][j];
    out[n] = '\0';
    s.live = false;
    return true;
}

// ---- replay and the counter ----------------------------------------------------------
bool Replay::fresh(const uint8_t mac[6], uint32_t counter) const {
    for (uint8_t i = 0; i < N; i++)
        if (e[i].live && memcmp(e[i].mac, mac, 6) == 0) return counter > e[i].last;
    return true;          // a sender we have not heard from yet
}

void Replay::record(const uint8_t mac[6], uint32_t counter) {
    stamp++;
    uint8_t slot = 0;
    bool found = false;
    for (uint8_t i = 0; i < N; i++)
        if (e[i].live && memcmp(e[i].mac, mac, 6) == 0) { slot = i; found = true; break; }
    if (!found) {
        // An empty slot, else the sender heard from least recently.
        for (uint8_t i = 0; i < N; i++) {
            if (!e[i].live) { slot = i; break; }
            if (e[i].used < e[slot].used) slot = i;
        }
        memcpy(e[slot].mac, mac, 6);
        e[slot].live = true;
        e[slot].last = counter;
    } else if (counter > e[slot].last) {
        e[slot].last = counter;
    }
    e[slot].used = stamp;
}

size_t Replay::save(uint8_t out[BYTES]) const {
    // Oldest first, so load() can rebuild who was heard from least recently
    // and a reboot does not change which sender is the next to be dropped.
    uint8_t order[N];
    uint8_t n = 0;
    for (uint8_t i = 0; i < N; i++) if (e[i].live) order[n++] = i;
    for (uint8_t i = 1; i < n; i++)
        for (uint8_t j = i; j > 0 && e[order[j - 1]].used > e[order[j]].used; j--) {
            const uint8_t t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    memset(out, 0, BYTES);
    out[0] = n;
    for (uint8_t k = 0; k < n; k++) {
        const E& s = e[order[k]];
        uint8_t* p = out + 1 + k * 10;
        memcpy(p, s.mac, 6);
        p[6] = (uint8_t)s.last;
        p[7] = (uint8_t)(s.last >> 8);
        p[8] = (uint8_t)(s.last >> 16);
        p[9] = (uint8_t)(s.last >> 24);
    }
    return BYTES;
}

bool Replay::load(const uint8_t* in, size_t len) {
    *this = Replay();
    if (!in || len != BYTES || in[0] > N) return false;
    for (uint8_t k = 0; k < in[0]; k++) {
        const uint8_t* p = in + 1 + k * 10;
        E& s = e[k];
        memcpy(s.mac, p, 6);
        s.last = (uint32_t)p[6] | ((uint32_t)p[7] << 8) | ((uint32_t)p[8] << 16) | ((uint32_t)p[9] << 24);
        s.live = true;
        s.used = ++stamp;
    }
    return true;
}

uint32_t Counter::reserve(uint32_t stored) {
    // The higher of what flash holds and what this run already reserved --
    // normally equal, but taking the maximum means neither can walk the
    // other backwards.
    const uint32_t base = (stored > limit) ? stored : limit;
    next  = base;
    limit = base + BLOCK;
    return limit;
}

// ---- the squad update -------------------------------------------------------------
bool parseVersion(const char* s, uint8_t v[3]) {
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') return false;
        unsigned n = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (unsigned)(*s - '0'); s++; if (n > 255) return false; }
        v[i] = (uint8_t)n;
        if (i < 2) { if (*s != '.') return false; s++; }
    }
    return true;
}

bool versionNewer(const uint8_t a[3], const uint8_t b[3]) {
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

size_t wifiBlob(const char* ssid, const char* pass, uint8_t out[WIFI_BLOB_MAX]) {
    const size_t sl = ssid ? strlen(ssid) : 0, pl = pass ? strlen(pass) : 0;
    if (sl == 0 || sl > WIFI_SSID_MAX || pl > WIFI_PASS_MAX) return 0;
    memset(out, 0, WIFI_BLOB_MAX);
    out[0] = (uint8_t)sl;
    out[1] = (uint8_t)pl;
    memcpy(out + 2, ssid, sl);
    memcpy(out + 2 + sl, pass, pl);
    return 2 + sl + pl;
}

bool wifiUnblob(const uint8_t* blob, size_t len, char ssid[WIFI_SSID_MAX + 1], char pass[WIFI_PASS_MAX + 1]) {
    if (!blob || len < 2) return false;
    const size_t sl = blob[0], pl = blob[1];
    if (sl == 0 || sl > WIFI_SSID_MAX || pl > WIFI_PASS_MAX || 2 + sl + pl > len) return false;
    memcpy(ssid, blob + 2, sl); ssid[sl] = '\0';
    memcpy(pass, blob + 2 + sl, pl); pass[pl] = '\0';
    return true;
}

uint8_t wifiParts(size_t blobLen) {
    if (blobLen == 0 || blobLen > WIFI_BLOB_MAX) return 0;
    return (uint8_t)((blobLen + WIFI_PART_BYTES - 1) / WIFI_PART_BYTES);
}

static size_t sealBytes(const Crypto& c, const uint8_t mac[6], uint32_t counter, uint8_t kind,
                        const uint8_t* pt, size_t ptLen, uint8_t* out, size_t cap) {
    const size_t need = HDR_LEN + ptLen + TAG_LEN;
    if (cap < need || counter > COUNTER_MAX) return 0;
    writeHeader(counter, kind, out);
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    if (!c.seal(nonce, out, HDR_LEN, pt, ptLen, out + HDR_LEN, out + HDR_LEN + ptLen)) return 0;
    return need;
}

static Open openBytes(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                      uint8_t want, size_t ptLen, uint32_t& counter, uint8_t* pt) {
    if (!isFrame(in, len)) return Open::NOT_OURS;
    uint8_t kind;
    if (!parseHeader(in, len, counter, kind)) return Open::BAD_FORMAT;
    if (kind != want || len != HDR_LEN + ptLen + TAG_LEN) return Open::BAD_FORMAT;
    uint8_t nonce[NONCE_LEN];
    nonceFor(mac, counter, nonce);
    if (!c.open(nonce, in, HDR_LEN, in + HDR_LEN, ptLen, in + HDR_LEN + ptLen, pt)) return Open::BAD_TAG;
    return Open::OK;
}

size_t sealNudge(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                 const uint8_t ver[3], uint8_t wifiParts, uint8_t* out, size_t cap) {
    if (wifiParts > WIFI_PARTS_MAX) return 0;
    const uint8_t pt[4] = { ver[0], ver[1], ver[2], wifiParts };
    return sealBytes(c, mac, counter, KIND_NUDGE, pt, sizeof pt, out, cap);
}

size_t sealWifiPart(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                    const uint8_t* blob, size_t blobLen, uint8_t part, uint8_t total,
                    uint8_t* out, size_t cap) {
    if (total == 0 || total > WIFI_PARTS_MAX || part >= total || wifiParts(blobLen) != total) return 0;
    uint8_t pt[1 + WIFI_PART_BYTES];
    pt[0] = (uint8_t)((part << 4) | total);
    memset(pt + 1, 0, WIFI_PART_BYTES);
    const size_t at = (size_t)part * WIFI_PART_BYTES;
    const size_t n  = (blobLen > at) ? (blobLen - at < WIFI_PART_BYTES ? blobLen - at : WIFI_PART_BYTES) : 0;
    memcpy(pt + 1, blob + at, n);
    return sealBytes(c, mac, counter, KIND_WIFI, pt, sizeof pt, out, cap);
}

size_t sealUpdated(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                   const uint8_t ver[3], uint8_t* out, size_t cap) {
    return sealBytes(c, mac, counter, KIND_UPDATED, ver, 3, out, cap);
}

size_t sealHello(const Crypto& c, const uint8_t mac[6], uint32_t counter, const uint8_t ver[3], uint8_t* out, size_t cap) {
    const uint8_t pt[4] = { 1, ver[0], ver[1], ver[2] };
    return sealBytes(c, mac, counter, KIND_HELLO, pt, sizeof pt, out, cap);
}
size_t sealHello(const Crypto& c, const uint8_t mac[6], uint32_t counter, const uint8_t ver[3],
                 uint32_t epoch, uint8_t zonePlusOne, uint8_t* out, size_t cap) {
    const uint8_t pt[9] = { 1, ver[0], ver[1], ver[2],
                            (uint8_t)epoch, (uint8_t)(epoch >> 8), (uint8_t)(epoch >> 16), (uint8_t)(epoch >> 24),
                            zonePlusOne };
    return sealBytes(c, mac, counter, KIND_HELLO, pt, sizeof pt, out, cap);
}

size_t sealRead(const Crypto& c, const uint8_t mac[6], uint32_t counter, uint32_t msgCounter, uint8_t* out, size_t cap) {
    const uint8_t pt[4] = { (uint8_t)msgCounter, (uint8_t)(msgCounter >> 8), (uint8_t)(msgCounter >> 16), (uint8_t)(msgCounter >> 24) };
    return sealBytes(c, mac, counter, KIND_READ, pt, sizeof pt, out, cap);
}

Open openRead(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len, uint32_t& counter, uint32_t& msgCounter) {
    uint8_t pt[4] = { 0, 0, 0, 0 };
    const Open r = openBytes(c, mac, in, len, KIND_READ, sizeof pt, counter, pt);
    msgCounter = (uint32_t)pt[0] | ((uint32_t)pt[1] << 8) | ((uint32_t)pt[2] << 16) | ((uint32_t)pt[3] << 24);
    return r;
}

Open openHello(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len, uint32_t& counter, uint8_t ver[3]) {
    uint32_t epoch = 0; uint8_t zone = 0;
    return openHello(c, mac, in, len, counter, ver, epoch, zone);
}
Open openHello(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len, uint32_t& counter, uint8_t ver[3],
               uint32_t& epoch, uint8_t& zonePlusOne) {
    ver[0] = ver[1] = ver[2] = 0;
    epoch = 0; zonePlusOne = 0;
    if (len == CANNED_FRAME_LEN) {                      // v1.7.7's: one byte, no version
        uint8_t v = 0;
        return openByte(c, mac, in, len, KIND_HELLO, counter, v);
    }
    if (len == CANNED_FRAME_LEN + 3) {                  // v1.7.8's: the version only
        uint8_t pt[4] = { 0, 0, 0, 0 };
        const Open r = openBytes(c, mac, in, len, KIND_HELLO, sizeof pt, counter, pt);
        if (r == Open::OK) { ver[0] = pt[1]; ver[1] = pt[2]; ver[2] = pt[3]; }
        return r;
    }
    uint8_t pt[9] = { 0 };
    const Open r = openBytes(c, mac, in, len, KIND_HELLO, sizeof pt, counter, pt);
    if (r == Open::OK) {
        ver[0] = pt[1]; ver[1] = pt[2]; ver[2] = pt[3];
        epoch = (uint32_t)pt[4] | ((uint32_t)pt[5] << 8) | ((uint32_t)pt[6] << 16) | ((uint32_t)pt[7] << 24);
        zonePlusOne = pt[8];
    }
    return r;
}

Open openNudge(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
               uint32_t& counter, uint8_t ver[3], uint8_t& wifiParts) {
    uint8_t pt[4] = { 0, 0, 0, 0 };
    const Open r = openBytes(c, mac, in, len, KIND_NUDGE, sizeof pt, counter, pt);
    if (r != Open::OK) return r;
    if (pt[3] > WIFI_PARTS_MAX) return Open::BAD_FORMAT;
    ver[0] = pt[0]; ver[1] = pt[1]; ver[2] = pt[2];
    wifiParts = pt[3];
    return Open::OK;
}

Open openWifiPart(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                  uint32_t& counter, uint8_t& part, uint8_t& total, uint8_t bytes[WIFI_PART_BYTES]) {
    uint8_t pt[1 + WIFI_PART_BYTES];
    const Open r = openBytes(c, mac, in, len, KIND_WIFI, sizeof pt, counter, pt);
    if (r != Open::OK) return r;
    part  = pt[0] >> 4;
    total = pt[0] & 0x0F;
    if (total == 0 || total > WIFI_PARTS_MAX || part >= total || counter < part) return Open::BAD_FORMAT;
    memcpy(bytes, pt + 1, WIFI_PART_BYTES);
    return Open::OK;
}

Open openUpdated(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                 uint32_t& counter, uint8_t ver[3]) {
    return openBytes(c, mac, in, len, KIND_UPDATED, 3, counter, ver);
}

bool WifiAssembly::add(const uint8_t m[6], uint32_t counter, uint8_t part, uint8_t tot,
                       const uint8_t in[WIFI_PART_BYTES]) {
    if (tot == 0 || tot > WIFI_PARTS_MAX || part >= tot || counter < part) return false;
    const uint32_t b = counter - part;
    if (!live || done || memcmp(mac, m, 6) != 0 || base != b || total != tot) {
        memcpy(mac, m, 6);
        base = b; total = tot; have = 0; live = true; done = false;
        memset(bytes, 0, sizeof bytes);
    }
    memcpy(bytes + (size_t)part * WIFI_PART_BYTES, in, WIFI_PART_BYTES);
    have |= (uint8_t)(1u << part);
    done = (have == (uint8_t)((1u << tot) - 1));
    return done;
}

bool WifiAssembly::take(const uint8_t m[6], uint32_t b, char ssid[WIFI_SSID_MAX + 1], char pass[WIFI_PASS_MAX + 1]) {
    if (!live || !done || memcmp(mac, m, 6) != 0 || base != b) return false;
    const bool ok = wifiUnblob(bytes, (size_t)total * WIFI_PART_BYTES, ssid, pass);
    // Used once. The blob is wiped whether or not it parsed.
    memset(bytes, 0, sizeof bytes);
    live = false; done = false;
    return ok;
}

// ---- the invite -----------------------------------------------------------------------
void invitePubBlob(const uint8_t target[6], uint8_t role, const uint8_t pub[INVITE_PUB_LEN], uint8_t out[INVITE_BLOB]) {
    memset(out, 0, INVITE_BLOB);
    memcpy(out, target, 6);
    out[6] = role;
    memcpy(out + 7, pub, INVITE_PUB_LEN);
}

bool invitePubUnblob(const uint8_t in[INVITE_BLOB], uint8_t target[6], uint8_t& role, uint8_t pub[INVITE_PUB_LEN]) {
    memcpy(target, in, 6);
    role = in[6];
    if (role > 1) return false;
    memcpy(pub, in + 7, INVITE_PUB_LEN);
    return true;
}

size_t inviteKeyBlob(const char* phrase, uint8_t out[INVITE_BLOB]) {
    const size_t n = phrase ? strlen(phrase) : 0;
    if (n == 0 || n > PHRASE_TEXT_MAX) return 0;
    memset(out, 0, INVITE_BLOB);
    out[0] = (uint8_t)n;
    memcpy(out + 1, phrase, n);
    return 1 + n;
}

bool inviteKeyUnblob(const uint8_t in[INVITE_BLOB], char out[PHRASE_TEXT_MAX + 1]) {
    const size_t n = in[0];
    if (n == 0 || n > PHRASE_TEXT_MAX) return false;
    memcpy(out, in + 1, n);
    out[n] = '\0';
    return true;
}

size_t sealInvitePub(HashFn h, uint32_t counter, const uint8_t blob[INVITE_BLOB], uint8_t part,
                     uint8_t* out, size_t cap) {
    if (!h || cap < INVITE_FRAME_LEN || counter > COUNTER_MAX || part >= INVITE_PARTS) return 0;
    writeHeader(counter, KIND_INVITE_PUB, out);
    out[HDR_LEN] = (uint8_t)((part << 4) | INVITE_PARTS);
    memcpy(out + HDR_LEN + 1, blob + (size_t)part * INVITE_PART_BYTES, INVITE_PART_BYTES);
    uint8_t d[32];
    h(out, HDR_LEN + 1 + INVITE_PART_BYTES, d);
    memcpy(out + HDR_LEN + 1 + INVITE_PART_BYTES, d, TAG_LEN);
    return INVITE_FRAME_LEN;
}

size_t sealInviteKey(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                     const uint8_t blob[INVITE_BLOB], uint8_t part, uint8_t* out, size_t cap) {
    if (part >= INVITE_PARTS) return 0;
    uint8_t pt[1 + INVITE_PART_BYTES];
    pt[0] = (uint8_t)((part << 4) | INVITE_PARTS);
    memcpy(pt + 1, blob + (size_t)part * INVITE_PART_BYTES, INVITE_PART_BYTES);
    return sealBytes(c, mac, counter, KIND_INVITE_KEY, pt, sizeof pt, out, cap);
}

Open openInvitePub(HashFn h, const uint8_t* in, size_t len,
                   uint32_t& counter, uint8_t& part, uint8_t bytes[INVITE_PART_BYTES]) {
    if (!isFrame(in, len)) return Open::NOT_OURS;
    uint8_t kind;
    if (!parseHeader(in, len, counter, kind)) return Open::BAD_FORMAT;
    if (kind != KIND_INVITE_PUB || len != INVITE_FRAME_LEN || !h) return Open::BAD_FORMAT;
    uint8_t d[32];
    h(in, HDR_LEN + 1 + INVITE_PART_BYTES, d);
    if (memcmp(d, in + HDR_LEN + 1 + INVITE_PART_BYTES, TAG_LEN) != 0) return Open::BAD_TAG;
    part = in[HDR_LEN] >> 4;
    if ((in[HDR_LEN] & 0x0F) != INVITE_PARTS || part >= INVITE_PARTS || counter < part) return Open::BAD_FORMAT;
    memcpy(bytes, in + HDR_LEN + 1, INVITE_PART_BYTES);
    return Open::OK;
}

Open openInviteKey(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                   uint32_t& counter, uint8_t& part, uint8_t bytes[INVITE_PART_BYTES]) {
    uint8_t pt[1 + INVITE_PART_BYTES];
    const Open r = openBytes(c, mac, in, len, KIND_INVITE_KEY, sizeof pt, counter, pt);
    if (r != Open::OK) return r;
    part = pt[0] >> 4;
    if ((pt[0] & 0x0F) != INVITE_PARTS || part >= INVITE_PARTS || counter < part) return Open::BAD_FORMAT;
    memcpy(bytes, pt + 1, INVITE_PART_BYTES);
    return Open::OK;
}

bool InviteAssembly::add(const uint8_t m[6], uint8_t k, uint32_t counter, uint8_t part, const uint8_t in[INVITE_PART_BYTES]) {
    if (part >= INVITE_PARTS || counter < part) return false;
    const uint32_t b = counter - part;
    if (!live || memcmp(mac, m, 6) != 0 || kind != k || base != b) {
        memcpy(mac, m, 6);
        kind = k; base = b; have = 0; live = true;
        memset(bytes, 0, sizeof bytes);
    }
    memcpy(bytes + (size_t)part * INVITE_PART_BYTES, in, INVITE_PART_BYTES);
    have |= (uint8_t)(1u << part);
    return have == (uint8_t)((1u << INVITE_PARTS) - 1);
}

void InviteAssembly::clear() {
    memset(bytes, 0, sizeof bytes);
    live = false; have = 0;
}

} // namespace MeshMsg
#endif // SQUACH_MESH
