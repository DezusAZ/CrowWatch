// SquachWatch-CYD — the public half of the firmware signing key.
//
// Over-the-air updates are only installed when their signature verifies
// against this key (see ota_core.h). The private half never lives in this
// repository: it is the OTA_SIGNING_KEY secret the release workflow signs with,
// plus the owner's offline backup.
//
// A public key is safe to publish. What matters is that this one and that
// secret are a PAIR: sign with anything else and every board refuses the image.
//
// The real key, generated 2026-09-12 (ECDSA P-256). Replacing it is not a
// quick change: every board in the field only trusts THIS key, so a new one
// reaches them only through a USB install -- or through an over-the-air
// update signed with the old key that carries the new one.
//
// It is kept here as the RAW CURVE POINT rather than as the PEM text it came
// from. Handing mbedtls a PEM means handing it the general-purpose key parser,
// which drags in base64, ASN.1 key structures and RSA -- 15 KB of flash, for
// one fixed key on one curve that this firmware has known since it was built.
// The point below is exactly what that parser would have produced.
//
// The PEM it was taken from, for anyone checking it against the private half:
//
//     -----BEGIN PUBLIC KEY-----
//     MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAExH42akwp2OwUx5sGjUVIJS3VbtXN
//     AVg8Z3YuOKDU/05K3SzLPm/xcWKOi07BROJg6DzekERBobkteEvAHBmwfg==
//     -----END PUBLIC KEY-----
//
// To regenerate the array after a key change (the last 65 bytes of the DER
// are the point; 0x04 marks it uncompressed, then X and Y, 32 bytes each):
//
//     openssl ec -pubin -in key.pub.pem -outform DER | tail -c 65 | xxd -i
#pragma once

#define OTA_PUBKEY_IS_TEST 0

// Uncompressed P-256 point: 0x04 || X (32) || Y (32).
static const unsigned char OTA_PUBKEY_POINT[65] = {
    0x04, 0xC4, 0x7E, 0x36, 0x6A, 0x4C, 0x29, 0xD8, 0xEC, 0x14, 0xC7, 0x9B,
    0x06, 0x8D, 0x45, 0x48, 0x25, 0x2D, 0xD5, 0x6E, 0xD5, 0xCD, 0x01, 0x58,
    0x3C, 0x67, 0x76, 0x2E, 0x38, 0xA0, 0xD4, 0xFF, 0x4E, 0x4A, 0xDD, 0x2C,
    0xCB, 0x3E, 0x6F, 0xF1, 0x71, 0x62, 0x8E, 0x8B, 0x4E, 0xC1, 0x44, 0xE2,
    0x60, 0xE8, 0x3C, 0xDE, 0x90, 0x44, 0x41, 0xA1, 0xB9, 0x2D, 0x78, 0x4B,
    0xC0, 0x1C, 0x19, 0xB0, 0x7E,
};
