// SquachWatch-Sim — a STAND-IN cipher for the emulator. This is not
// cryptography and must never be linked into firmware.
//
// The emulator renders screens and never talks to another device, so it needs
// something with the right SHAPE -- round trips work, a tampered frame is
// refused -- not the right security. The firmware's real cipher is mbedtls in
// src/meshcrypto.cpp, which cannot be built on a desktop; see include/meshmsg.h
// for how that one is pinned instead.
#include "meshcrypto.h"
#include <string.h>

namespace {

uint8_t s_key[MeshMsg::KEY_LEN];
bool    s_keyed = false;

uint64_t fnv(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}
void tagOf(const uint8_t* nonce, const uint8_t* aad, size_t aadLen,
           const uint8_t* ct, size_t ctLen, uint8_t tag[MeshMsg::TAG_LEN]) {
    uint64_t h = fnv(0xcbf29ce484222325ULL, s_key, sizeof s_key);
    h = fnv(h, nonce, MeshMsg::NONCE_LEN);
    const uint8_t alen = (uint8_t)aadLen;
    h = fnv(h, &alen, 1);
    h = fnv(h, aad, aadLen);
    h = fnv(h, ct, ctLen);
    for (size_t i = 0; i < MeshMsg::TAG_LEN; i++) tag[i] = (uint8_t)(h >> (8 * i));
}
bool standInDerive(const char* p, size_t pl, const uint8_t* s, size_t sl,
                   uint32_t iters, uint8_t key[MeshMsg::KEY_LEN]) {
    uint64_t h = fnv(fnv(0xcbf29ce484222325ULL, (const uint8_t*)p, pl), s, sl);
    for (size_t i = 0; i < MeshMsg::KEY_LEN; i++)
        key[i] = (uint8_t)((h >> (8 * (i % 8))) ^ i ^ iters);
    return true;
}
bool standInSetKey(const uint8_t key[MeshMsg::KEY_LEN]) {
    memcpy(s_key, key, sizeof s_key);
    s_keyed = true;
    return true;
}
bool standInSeal(const uint8_t* nonce, const uint8_t* aad, size_t aadLen,
                 const uint8_t* pt, size_t ptLen, uint8_t* ct, uint8_t* tag) {
    if (!s_keyed) return false;
    for (size_t i = 0; i < ptLen; i++)
        ct[i] = pt[i] ^ s_key[i % MeshMsg::KEY_LEN] ^ nonce[i % MeshMsg::NONCE_LEN];
    tagOf(nonce, aad, aadLen, ct, ptLen, tag);
    return true;
}
bool standInOpen(const uint8_t* nonce, const uint8_t* aad, size_t aadLen,
                 const uint8_t* ct, size_t ctLen, const uint8_t* tag, uint8_t* pt) {
    if (!s_keyed) return false;
    uint8_t want[MeshMsg::TAG_LEN];
    tagOf(nonce, aad, aadLen, ct, ctLen, want);
    if (memcmp(want, tag, sizeof want) != 0) return false;
    for (size_t i = 0; i < ctLen; i++)
        pt[i] = ct[i] ^ s_key[i % MeshMsg::KEY_LEN] ^ nonce[i % MeshMsg::NONCE_LEN];
    return true;
}
const MeshMsg::Crypto STAND_IN = { standInDerive, standInSetKey, standInSeal, standInOpen };

} // namespace

const MeshMsg::Crypto& MeshCrypto::impl() { return STAND_IN; }

// The key exchange, stood in for: a "public key" is the private one with a
// byte flipped, and the "shared secret" is the XOR of both privates, which
// both sides can reach from what they hold. Enough for the screens.
void MeshCrypto::sha256(const uint8_t* in, size_t len, uint8_t out[32]) {
    // Not SHA-256: FNV spread over 32 bytes. Both ends of the emulator agree.
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) h = (h ^ in[i]) * 16777619u;
    for (size_t i = 0; i < 32; i++) { h = h * 1664525u + 1013904223u; out[i] = (uint8_t)(h >> 24); }
}
bool MeshCrypto::dhKeypair(uint8_t priv[DH_LEN], uint8_t pub[DH_LEN]) {
    static uint32_t seed = 0x5EED1234u;
    for (size_t i = 0; i < DH_LEN; i++) { seed = seed * 1664525u + 1013904223u; priv[i] = (uint8_t)(seed >> 24); }
    for (size_t i = 0; i < DH_LEN; i++) pub[i] = (uint8_t)(priv[i] ^ 0xA5);
    return true;
}
bool MeshCrypto::dhShared(const uint8_t priv[DH_LEN], const uint8_t peerPub[DH_LEN], uint8_t out[DH_LEN]) {
    for (size_t i = 0; i < DH_LEN; i++) out[i] = (uint8_t)(priv[i] ^ (peerPub[i] ^ 0xA5));
    return true;
}
void MeshCrypto::dhSessionKey(const uint8_t shared[DH_LEN], uint8_t key[MeshMsg::KEY_LEN]) {
    for (size_t i = 0; i < MeshMsg::KEY_LEN; i++) key[i] = (uint8_t)(shared[i] ^ shared[i + 16] ^ 0x3C);
}
uint16_t MeshCrypto::dhCode(const uint8_t pubA[DH_LEN], const uint8_t pubB[DH_LEN]) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < DH_LEN; i++) { h = (h ^ (uint32_t)(pubA[i] ^ pubB[i])) * 16777619u; }
    return (uint16_t)(h % 10000u);
}
// Nothing to test against: the golden frame is real AES-CCM and this is not.
bool MeshCrypto::selfTest() { return true; }
