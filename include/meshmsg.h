// SquachWatch-CYD — SquachMesh messages: the pure half.
//
// Everything here is arithmetic over bytes: the word list, rolling a phrase,
// the frame format, text packing, reassembly, replay rejection and counter
// reservation. The cipher is INJECTED (see Crypto), so this compiles and is
// tested on a desktop with no crypto library at all -- which matters, because
// CI builds and tests it on every push and CI has none.
//
// The cipher itself is AES-128-CCM with an 8-byte tag, keyed by
// PBKDF2-HMAC-SHA256 over a five-word phrase. On the device that is mbedtls
// (meshcrypto.cpp). It is pinned to an INDEPENDENT implementation -- Python's
// `cryptography`, via test/gen_meshmsg_vectors.py -- by golden frames the
// device must reproduce byte for byte at boot before it will send or read a
// single message. Two implementations that must agree about a format neither
// owns is a test; one implementation checked against itself is not.
//
// WHERE THIS RIDES: the scan response. The limit that shapes everything below
// is BLE 4.2 legacy advertising -- 31 bytes a packet -- because the ESP32 on
// these boards has no extended advertising. After the packet's own length and
// type bytes and the company ID, a frame gets 27 bytes, and TEXT_FRAME_LEN is
// exactly that.
#pragma once
#if SQUACH_MESH
#include <stdint.h>
#include <stddef.h>

namespace MeshMsg {

// ---- the word list (meshwords.cpp) -------------------------------------
constexpr uint8_t PHRASE_WORDS = 5;
constexpr uint8_t WORD_MAX     = 8;
// Five words of up to eight letters, four spaces, one NUL.
constexpr size_t  PHRASE_TEXT_MAX = PHRASE_WORDS * (WORD_MAX + 1);
extern const char* const WORDS[];
extern const uint16_t    WORD_N;

// The canned lines a message can carry, by index. Both ends must agree on
// what index N means, so lines may be ADDED at the end and never reordered or
// removed once a release has shipped them.
extern const char* const CANNED[];
extern const uint8_t     CANNED_N;

// How the picker groups them. PRESENTATION ONLY -- none of this travels, so
// unlike the indices above it may be reordered or renamed freely. Same shape
// as the emote picker's tabs (EmoteScript::TABS/PER_TAB) on purpose: both
// halves of the message screen then behave identically.
constexpr uint8_t CANNED_TABS = 6, CANNED_PER_TAB = 8;
extern const char* const CANNED_TAB_NAME[CANNED_TABS];
// The CANNED index at that slot, or 0xFF when the slot is empty.
uint8_t cannedAtTab(uint8_t tab, uint8_t slot);

// ---- phrases ---------------------------------------------------------------
// A uniform index in [0, n). Rejection sampling rather than `rng() % n`, which
// over-weights the low indices whenever n does not divide 2^32 -- slightly,
// but a slightly predictable phrase generator is the one thing this file must
// not be.
uint16_t pickUniform(uint32_t (*rng)(), uint16_t n);
void     roll(uint32_t (*rng)(), uint16_t out[PHRASE_WORDS]);
// "WORD WORD WORD WORD WORD". Returns the length, or 0 for a bad index or a
// buffer too small -- never a truncated phrase, which would derive a key.
size_t   phraseText(const uint16_t idx[PHRASE_WORDS], char* out, size_t cap);
// The words starting with `c`, as indices, in list order. For the picker.
uint8_t  wordsStartingWith(char c, uint16_t* out, uint8_t cap);

// ---- key stretching ----------------------------------------------------------
extern const char SALT[];
constexpr size_t  KEY_LEN = 16;
// How many PBKDF2 rounds the phrase goes through on its way to a key.
//
// FROZEN AT RELEASE. The key depends on this number, so changing it after a
// release changes every group's key and they stop reading each other.
//
// Measured 2026-09-10 on an ESP32 (ST7789 CYD): 2753 ms. Kept rather than
// cut to about a second, because the cost lands only when a phrase is set --
// the derived key is stored and loaded at boot, never re-stretched -- while
// every offline guess at a phrase pays it every time.
constexpr uint32_t ITERS = 20000;

// ---- the cipher, injected -----------------------------------------------------
constexpr size_t NONCE_LEN = 13;
constexpr size_t TAG_LEN   = 8;
// Stateful by design: setKey() is called once when a key is loaded, and
// seal/open reuse it. Setting a cipher up per message is the same allocation
// churn that was already ripped out of the advertising code once.
struct Crypto {
    bool (*derive)(const char* phrase, size_t phraseLen,
                   const uint8_t* salt, size_t saltLen,
                   uint32_t iters, uint8_t key[KEY_LEN]);
    bool (*setKey)(const uint8_t key[KEY_LEN]);
    bool (*seal)(const uint8_t nonce[NONCE_LEN], const uint8_t* aad, size_t aadLen,
                 const uint8_t* pt, size_t ptLen, uint8_t* ct, uint8_t tag[TAG_LEN]);
    // False on ANY failure, authentication included. `pt` is not to be read.
    bool (*open)(const uint8_t nonce[NONCE_LEN], const uint8_t* aad, size_t aadLen,
                 const uint8_t* ct, size_t ctLen, const uint8_t tag[TAG_LEN], uint8_t* pt);
};

// ---- the frame --------------------------------------------------------------
// Version 2. After the manufacturer company ID (0xFFFF, same as SquachMesh):
//
//   0  2  magic 'S','T'
//   2  1  version (high four bits) and kind (low four)
//   3  3  counter, little-endian -- per sender, never reused under one key
//   6  n  ciphertext
//   6+n 8 tag
//
// Version 1 spent ten bytes on this header, four of them on a magic chosen to
// keep out of v1.5.23's way. Nothing released ever read it, so v2 took the
// bytes back for text: six more characters in every part.
//
// The six header bytes are authenticated as associated data. The nonce is the
// SENDER'S BLUETOOTH ADDRESS, the counter, and four zero bytes: with one key
// shared by a whole group, two devices on the same counter would otherwise
// reuse a nonce, which does not weaken AES-CCM, it ends it. Putting the
// address in the nonce also binds a frame to the device that sent it -- the
// same bytes replayed from any other address fail authentication.
//
// SquachMesh's own advert starts 'S','Q', so the two magics cannot collide.
constexpr uint8_t  MAGIC[2]    = { 'S', 'T' };
constexpr uint8_t  VERSION     = 2;
constexpr uint8_t  KIND_CANNED = 1;
constexpr uint8_t  KIND_TEXT   = 2;
constexpr uint8_t  KIND_EMOTE  = 3;     // see "emotes" below
constexpr size_t   HDR_LEN     = 6;
// Three bytes of counter. At three counters a typed message that is sixteen
// million sends: not a limit anybody meets, and a sender refuses rather than
// wraps if one ever does.
constexpr uint32_t COUNTER_MAX = 0xFFFFFFu;

constexpr size_t CANNED_FRAME_LEN = HDR_LEN + 1 + TAG_LEN;   // 15

// ---- typed messages ---------------------------------------------------------
// Up to 48 characters, sent as up to three parts of sixteen. Each part is its
// own sealed frame -- its own counter, its own tag -- with one byte inside the
// ciphertext saying which part it is and how many there are (index in the high
// four bits, total in the low). The parts of one message use consecutive
// counters, so a receiver finds a message's first counter as `counter - index`.
//
// Characters are six-bit codes into TEXT_CHARSET, sixteen to a part in twelve
// bytes. Uppercase, because the display face and both keyboards are.
constexpr uint8_t TEXT_MAX         = 48;
constexpr uint8_t TEXT_PART_CHARS  = 16;
constexpr uint8_t TEXT_PARTS_MAX   = 3;
constexpr size_t  TEXT_PART_BYTES  = TEXT_PART_CHARS * 6 / 8;             // 12
constexpr size_t  TEXT_FRAME_LEN   = HDR_LEN + 1 + TEXT_PART_BYTES + TAG_LEN; // 27
constexpr size_t  FRAME_MAX        = TEXT_FRAME_LEN;
static_assert(TEXT_MAX == TEXT_PART_CHARS * TEXT_PARTS_MAX, "three whole parts");
static_assert(2 + FRAME_MAX <= 29, "a frame and its company ID must fit one legacy AD");

// The characters a message can hold, in code order: code i is TEXT_CHARSET[i].
// Codes may be ADDED at the end once released, never reordered. The code past
// the last character is END, which pads a final part.
extern const char TEXT_CHARSET[];
constexpr uint8_t TEXT_END = 63;
bool    textChar(char c);
// How many parts `s` needs: 1..3, or 0 if it is empty, longer than TEXT_MAX,
// or holds a character outside TEXT_CHARSET.
uint8_t textParts(const char* s);

// ---- emotes -------------------------------------------------------------------
// Something the two Squachys DO rather than something one of them says. Sent
// from the message screen and acted out by both pairs at once: the sender's
// own, and the one on the other board, where the sender is the visitor.
//
// Two sealed bytes under kind 3 -- which v1.5.25 and earlier do not read, so
// they drop an emote without a word rather than show a message they cannot
// place. The first byte is the emote; the second, its SETUP: whatever both
// boards must agree on for the two performances to match. Left to themselves
// the boards would each roll their own dice, and two people standing side by
// side would watch two different games.
//
// It was one byte, four bits of each, until there were more than sixteen
// emotes. Nothing released had sent one, so the format was free to change.
//
// May be ADDED at the end once released, never reordered. (TICKLE was cut
// from the middle of this list before any release carried the scripted ones
// -- v1.5.25 stops at BOO -- so nothing shipped ever sent the numbers that
// moved. That was the last moment it could be done.)
enum class Emote : uint8_t {
    WAVE, HIGH_FIVE, DANCE, RPS, SNOWBALL, BOO,
    // Everything from here on is a script -- see emote_script.h.
    FIST_BUMP, HANDSHAKE, SALUTE, BOW, HUG,
    COIN, DICE, ARM_WRESTLE, TUG, LEAPFROG,
    PIE, BALLOON, PLANE, PILLOW,
    GIFT, SNACK, CHEERS, CONFETTI, FIREWORKS,
    HEART, LAUGH, SAD, GRR, SLEEPY,
    TINFOIL, CAMERA, SPOTTED, HOWL, SELFIE,
    COUNT
};
constexpr size_t EMOTE_FRAME_LEN = HDR_LEN + 2 + TAG_LEN;   // 16
static_assert(EMOTE_FRAME_LEN <= FRAME_MAX, "an emote fits where a message does");

// ---- the squad update ---------------------------------------------------------
// One board tells every board in range with the phrase to update itself.
//
//   NUDGE    the version to update to (three bytes) and how many WIFI parts
//            follow it, at the next counters. Zero when the sender is not
//            sharing its network.
//   WIFI     twelve raw bytes of a blob -- [ssid length][pass length][ssid]
//            [pass] -- in up to six parts, at the counters after the NUDGE.
//            Raw bytes, not the message alphabet: passwords have case and
//            symbols. Sealed like everything else, so only the phrase reads
//            it; the receiver uses it once and never stores it.
//   UPDATED  a board's one reply after the reboot: the version it now runs.
//
// The version is the SENDER'S running version. There is no 'whatever is
// newest': a receiver has to be able to refuse without joining WiFi, since
// leaving WiFi update mode costs a restart. Update one board, then nudge.
constexpr uint8_t KIND_NUDGE   = 4;
constexpr uint8_t KIND_WIFI    = 5;
constexpr uint8_t KIND_UPDATED = 6;
constexpr size_t  NUDGE_FRAME_LEN   = HDR_LEN + 4 + TAG_LEN;                  // 18
constexpr size_t  UPDATED_FRAME_LEN = HDR_LEN + 3 + TAG_LEN;                  // 17
constexpr uint8_t WIFI_PART_BYTES   = 12;
constexpr uint8_t WIFI_PARTS_MAX    = 6;
constexpr size_t  WIFI_BLOB_MAX     = WIFI_PART_BYTES * WIFI_PARTS_MAX;      // 72
constexpr size_t  WIFI_FRAME_LEN    = HDR_LEN + 1 + WIFI_PART_BYTES + TAG_LEN; // 27
constexpr uint8_t WIFI_SSID_MAX     = 32;
constexpr uint8_t WIFI_PASS_MAX     = 38;    // 2 + 32 + 38 = 72, the six parts exactly
static_assert(WIFI_FRAME_LEN <= FRAME_MAX, "a WiFi part fits where a message does");
// The most frames one send can put on the air: a NUDGE and its WIFI parts.
constexpr uint8_t OUT_PARTS_MAX = 1 + WIFI_PARTS_MAX;
static_assert(OUT_PARTS_MAX >= TEXT_PARTS_MAX, "a text message still fits the out queue");

// ---- the invite ---------------------------------------------------------------
// Adding a nearby board to the squad without typing the phrase. Two frame
// kinds, both in parts of twelve bytes over a 48-byte blob:
//
//   INVITE_PUB  [target mac 6][role 1][X25519 public key 32][zero 9]
//               role 0 is the inviter's offer, 1 the invitee's answer. Not
//               sealed -- there is no shared key yet -- so its tag is a plain
//               hash: integrity against the air, nothing against a forger.
//               What defeats a forger is the four-digit code both people
//               compare, derived from both public keys (meshcrypto.h).
//   INVITE_KEY  [phrase length 1][phrase 45][zero 2], sealed with the
//               one-time session key from the exchange, addressed by the
//               nonce's sender and the replay table like any message.
constexpr uint8_t KIND_INVITE_PUB = 7;
constexpr uint8_t KIND_INVITE_KEY = 8;
// HELLO: one sealed byte a board with the phrase puts on the air now and
// then, so the boards around it can tell a squad member from a stranger
// without anyone sending a message. Never recorded in the replay table --
// it is a fact about the sender, not something to act on -- so it costs
// nothing but a counter every couple of minutes.
constexpr uint8_t KIND_HELLO      = 9;
constexpr uint8_t INVITE_PART_BYTES = 12;
constexpr uint8_t INVITE_PARTS      = 4;
constexpr size_t  INVITE_BLOB       = INVITE_PART_BYTES * INVITE_PARTS;   // 48
constexpr size_t  INVITE_FRAME_LEN  = HDR_LEN + 1 + INVITE_PART_BYTES + TAG_LEN; // 27
constexpr size_t  INVITE_PUB_LEN    = 32;
static_assert(6 + 1 + INVITE_PUB_LEN <= INVITE_BLOB, "a public key and its address fit the blob");
static_assert(1 + PHRASE_TEXT_MAX <= INVITE_BLOB, "a phrase fits the blob");

// Plain-hash tag for the unsealed INVITE_PUB parts: the first TAG_LEN bytes
// of SHA-256 over header and payload. Provided by the platform, since the
// hash lives with the rest of the crypto.
typedef void (*HashFn)(const uint8_t* in, size_t len, uint8_t out[32]);

// The blobs.
void invitePubBlob(const uint8_t target[6], uint8_t role, const uint8_t pub[INVITE_PUB_LEN], uint8_t out[INVITE_BLOB]);
bool invitePubUnblob(const uint8_t in[INVITE_BLOB], uint8_t target[6], uint8_t& role, uint8_t pub[INVITE_PUB_LEN]);
size_t inviteKeyBlob(const char* phrase, uint8_t out[INVITE_BLOB]);   // 0 if too long
bool   inviteKeyUnblob(const uint8_t in[INVITE_BLOB], char out[PHRASE_TEXT_MAX + 1]);

size_t sealInvitePub(HashFn h, uint32_t counter, const uint8_t blob[INVITE_BLOB], uint8_t part,
                     uint8_t* out, size_t cap);
size_t sealInviteKey(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                     const uint8_t blob[INVITE_BLOB], uint8_t part, uint8_t* out, size_t cap);

// 'v1.7.5', '1.7.5-3-gabc' -> {1, 7, 5}. False when it does not start that way.
bool parseVersion(const char* s, uint8_t v[3]);
// a newer than b.
bool versionNewer(const uint8_t a[3], const uint8_t b[3]);

// The blob a WIFI series carries. Returns its length, 0 if either is too long.
size_t wifiBlob(const char* ssid, const char* pass, uint8_t out[WIFI_BLOB_MAX]);
bool   wifiUnblob(const uint8_t* blob, size_t len, char ssid[WIFI_SSID_MAX + 1], char pass[WIFI_PASS_MAX + 1]);
uint8_t wifiParts(size_t blobLen);       // 1..6, 0 for an empty blob

size_t sealNudge(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                 const uint8_t ver[3], uint8_t wifiParts, uint8_t* out, size_t cap);
size_t sealWifiPart(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                    const uint8_t* blob, size_t blobLen, uint8_t part, uint8_t total,
                    uint8_t* out, size_t cap);
size_t sealUpdated(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                   const uint8_t ver[3], uint8_t* out, size_t cap);
size_t sealHello(const Crypto& c, const uint8_t mac[6], uint32_t counter, uint8_t* out, size_t cap);
// The setup byte, per emote. RPS: the sender's throw times three, plus the
// receiver's -- each 0 rock, 1 paper, 2 scissors. The rest are described with
// the scripts (EmoteScript::roll); an emote with nothing to agree on sends 0.
size_t sealEmote(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                 uint8_t emote, uint8_t setup, uint8_t* out, size_t cap);

void   nonceFor(const uint8_t mac[6], uint32_t counter, uint8_t nonce[NONCE_LEN]);
bool   isFrame(const uint8_t* in, size_t len);        // magic only: is it ours at all
// Header only, no crypto: what a receiver checks BEFORE spending a decryption.
bool   parseHeader(const uint8_t* in, size_t len, uint32_t& counter, uint8_t& kind);
size_t sealCanned(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                  uint8_t canned, uint8_t* out, size_t cap);
// Part `part` of `total` of `text` -- which must be exactly textParts(text)
// parts long. Returns the frame length, or 0.
size_t sealTextPart(const Crypto& c, const uint8_t mac[6], uint32_t counter,
                    const char* text, uint8_t part, uint8_t total,
                    uint8_t* out, size_t cap);

enum class Open : uint8_t {
    OK,
    NOT_OURS,       // some other 0xFFFF payload -- leave it alone
    BAD_FORMAT,     // ours, but a version, kind or length this build does not read
    BAD_TAG,        // forged, corrupted, from someone else's group, or the wrong sender
    UNKNOWN_LINE,   // authentic, from a newer build with lines this one lacks
};
Open openCanned(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                uint32_t& counter, uint8_t& canned);
// One part of a typed message. `chars` gets its characters, NUL-terminated and
// stopping at END; a code this build has no character for comes out as '?'.
Open openTextPart(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                  uint32_t& counter, uint8_t& part, uint8_t& total,
                  char chars[TEXT_PART_CHARS + 1]);
// An emote and its setup. UNKNOWN_LINE when it is authentic but newer than
// this build: nothing here to act out, and nothing wrong either.
Open openEmote(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
               uint32_t& counter, uint8_t& emote, uint8_t& setup);
Open openHello(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len, uint32_t& counter);
Open openNudge(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
               uint32_t& counter, uint8_t ver[3], uint8_t& wifiParts);
Open openWifiPart(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                  uint32_t& counter, uint8_t& part, uint8_t& total, uint8_t bytes[WIFI_PART_BYTES]);
Open openUpdated(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                 uint32_t& counter, uint8_t ver[3]);
Open openInvitePub(HashFn h, const uint8_t* in, size_t len,
                   uint32_t& counter, uint8_t& part, uint8_t bytes[INVITE_PART_BYTES]);
Open openInviteKey(const Crypto& c, const uint8_t mac[6], const uint8_t* in, size_t len,
                   uint32_t& counter, uint8_t& part, uint8_t bytes[INVITE_PART_BYTES]);

// One invite series in flight, from one sender: four parts of twelve.
struct InviteAssembly {
    uint8_t  mac[6] = {};
    uint8_t  kind = 0;
    uint32_t base = 0;
    uint8_t  have = 0;
    uint8_t  bytes[INVITE_BLOB] = {};
    bool     live = false;
    // True once all four parts are in; `bytes` is then the blob.
    bool add(const uint8_t mac[6], uint8_t kind, uint32_t counter, uint8_t part, const uint8_t in[INVITE_PART_BYTES]);
    void clear();
};

// One WIFI series in flight, from one sender. Same shape as Assembly below,
// for bytes rather than text, and one slot: a nudge is a rare thing.
struct WifiAssembly {
    uint8_t  mac[6] = {};
    uint32_t base = 0;
    uint8_t  total = 0, have = 0;
    uint8_t  bytes[WIFI_BLOB_MAX] = {};
    bool     live = false, done = false;
    // True once the series is complete; the blob is then in `bytes`, its
    // length total * WIFI_PART_BYTES (the tail is zero padding).
    bool add(const uint8_t mac[6], uint32_t counter, uint8_t part, uint8_t total,
             const uint8_t in[WIFI_PART_BYTES]);
    // The finished blob for a series that began at `base`, from `mac`.
    bool take(const uint8_t mac[6], uint32_t base, char ssid[WIFI_SSID_MAX + 1], char pass[WIFI_PASS_MAX + 1]);
};

// ---- reassembly ----------------------------------------------------------------
// Parts arrive in whatever order the scan catches them, and each is repeated
// for as long as the sender is broadcasting. Only AUTHENTICATED parts are
// added -- a forger cannot fill a slot -- and a message is handed over whole
// or not at all.
struct Assembly {
    static constexpr uint8_t N = 2;
    struct E {
        uint8_t  mac[6];
        uint32_t base;          // the message's first counter
        uint8_t  total, have;   // have: one bit per part received
        char     seg[TEXT_PARTS_MAX][TEXT_PART_CHARS + 1];
        uint32_t used;
        bool     live;
    };
    E        e[N] = {};
    uint32_t stamp = 0;
    // True when this part completes its message: the whole text is in `out`,
    // and `base` is the message's first counter.
    bool add(const uint8_t mac[6], uint32_t counter, uint8_t part, uint8_t total,
             const char* chars, char out[TEXT_MAX + 1], uint32_t& base);
};

// ---- replay -----------------------------------------------------------------
// A sender repeats the same frame for as long as it is broadcasting, so every
// frame arrives dozens of times. Each (sender, counter) is delivered once.
//
// fresh() is checked BEFORE decrypting, and record() only AFTER a message has
// authenticated -- a whole typed message, not one of its parts -- otherwise a
// forger could poison the table with a huge counter and silence a real sender
// for good. A typed message records its LAST counter, so its own parts arriving
// again afterwards are stale.
//
// Kept across a reboot: meshtalk.cpp writes the table to flash after every
// message it records. It used to be RAM only, so a frame recorded by somebody
// else and replayed at a freshly booted receiver was shown once more. What is
// still true: the table holds four senders, and one pushed out by four newer
// ones is forgotten, so its old frames count as new again.
struct Replay {
    static constexpr uint8_t N = 4;
    // A count, then each live sender, oldest first: address, last counter.
    static constexpr size_t  BYTES = 1 + N * 10;
    struct E { uint8_t mac[6]; uint32_t last; uint32_t used; bool live; };
    E        e[N] = {};
    uint32_t stamp = 0;
    bool fresh(const uint8_t mac[6], uint32_t counter) const;
    void record(const uint8_t mac[6], uint32_t counter);
    size_t save(uint8_t out[BYTES]) const;
    // Anything that is not exactly what save() writes loads as an empty table
    // and returns false: a table nobody wrote is not one to trust.
    bool   load(const uint8_t* in, size_t len);
};

// ---- the counter ----------------------------------------------------------------
// A sender's counter must never repeat under one key -- including across a
// crash, which is the part that is easy to get wrong. Values are reserved from
// NVS a block at a time: the stored number is always ABOVE anything handed out,
// so a crash can skip values but never reuse one.
//
//   if (c.needsReserve()) nvsWrite(c.reserve(nvsRead()));   // persist FIRST
//   use(c.take());
struct Counter {
    static constexpr uint32_t BLOCK = 64;
    uint32_t next = 0, limit = 0;
    bool     needsReserve() const { return next >= limit; }
    uint32_t reserve(uint32_t stored);   // returns the new high-water mark
    uint32_t take() { return next++; }
};

} // namespace MeshMsg
#endif
