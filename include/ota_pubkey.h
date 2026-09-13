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
#pragma once

#define OTA_PUBKEY_IS_TEST 0

static const char OTA_PUBKEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAExH42akwp2OwUx5sGjUVIJS3VbtXN\n"
    "AVg8Z3YuOKDU/05K3SzLPm/xcWKOi07BROJg6DzekERBobkteEvAHBmwfg==\n"
    "-----END PUBLIC KEY-----\n";
