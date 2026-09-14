// SquachWatch-CYD — the real cipher behind SquachMesh messages: mbedtls,
// AES-128-CCM with an 8-byte tag and PBKDF2-HMAC-SHA256, on the ESP32's
// hardware AES and SHA.
//
// Device only. The emulator links sim/meshcrypto_sim.cpp against this same
// header instead, and the host tests do not link a cipher at all -- see
// include/meshmsg.h for why, and for how the real one is still pinned.
#pragma once
#if SQUACH_MESH
#include "meshmsg.h"

namespace MeshCrypto {

const MeshMsg::Crypto& impl();

// Runs the golden frame from test/gen_meshmsg_vectors.py -- built by Python's
// `cryptography`, an implementation that shares no code with this one --
// through the real key stretching, sealing and opening, and demands the same
// bytes. False means this build's crypto disagrees with an independent
// implementation about what AES-CCM is, and it must not be trusted with a
// single message. Leaves no key set: call it before loading the real one.
bool selfTest();

// ---- the invite's key exchange ------------------------------------------------
// X25519: a fresh keypair per invite, a shared secret from the other side's
// public key, and the two things the invite derives from it -- a one-time
// message key (the same shape the squad key has, so the same sealer carries
// the phrase) and the four digits both people compare. The digits come from
// BOTH public keys, so a third party who swapped either in the middle would
// leave the two screens disagreeing.
constexpr size_t DH_LEN = 32;
void sha256(const uint8_t* in, size_t len, uint8_t out[32]);
bool dhKeypair(uint8_t priv[DH_LEN], uint8_t pub[DH_LEN]);
bool dhShared(const uint8_t priv[DH_LEN], const uint8_t peerPub[DH_LEN], uint8_t out[DH_LEN]);
// key = SHA-256("squachwatch-invite" || shared)[0..16)
void dhSessionKey(const uint8_t shared[DH_LEN], uint8_t key[MeshMsg::KEY_LEN]);
// 0..9999 from SHA-256(min(pubA, pubB) || max(pubA, pubB)), order-free.
uint16_t dhCode(const uint8_t pubA[DH_LEN], const uint8_t pubB[DH_LEN]);

} // namespace MeshCrypto
#endif
