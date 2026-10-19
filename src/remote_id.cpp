// SquachWatch-CYD — ASTM F3411 Remote ID decoder. See remote_id.h.
#include "remote_id.h"
#include <string.h>

namespace RemoteId {

// ---- the wire format ----------------------------------------------------
// A Bluetooth Legacy advert is a run of AD structures, each one a length
// byte, a type byte, then length-1 bytes of data. Remote ID rides in the
// Service Data structure (type 0x16) under UUID 0xFFFA, and inside that
// sits a one-byte application code, a one-byte message counter, then
// exactly 25 bytes of message.
static const uint8_t AD_SERVICE_DATA_16 = 0x16;
static const uint16_t ODID_UUID         = 0xFFFA;
static const uint8_t ODID_APP_CODE      = 0x0D;
static const uint8_t MSG_SIZE           = 25;

// Message types live in the TOP nibble of byte 0. The bottom nibble is the
// protocol version, which we do not check: the field layouts this reads
// have been stable across every published version, and refusing to decode
// a drone because it advertised version 3 would be worse than decoding it.
static const uint8_t MSG_BASIC_ID = 0x0;
static const uint8_t MSG_LOCATION = 0x1;
static const uint8_t MSG_SYSTEM   = 0x4;

// Fixed-point scales, straight out of the reference implementation:
// coordinates are degrees x 10^7, and altitude is half-metres offset by
// 1000m so that below-sea-level values still fit an unsigned 16-bit field.
static const double LATLON_MULT = 10000000.0;
static const float  ALT_DIV     = 0.5f;
static const float  ALT_ADDER   = 1000.0f;

// Little-endian, because the standard's structs are packed and every
// device implementing it is little-endian. Read byte by byte rather than
// cast: the payload comes off the radio unaligned, and a 32-bit load at an
// odd address is a LoadStoreError exception on this chip rather than a
// slow read like it would be on a PC.
static int32_t rd32(const uint8_t* p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

const char* uaTypeName(uint8_t t) {
    switch (t) {
        case 1:  return "PLANE";
        case 2:  return "QUAD";
        case 3:  return "HELI";
        case 4:  return "GYRO";
        case 5:  return "VTOL";
        case 6:  return "ORNITH";
        case 7:  return "GLIDER";
        case 8:  return "KITE";
        case 9:  return "FREEBAL";
        case 10: return "CAPBAL";
        case 11: return "AIRSHIP";
        case 12: return "PARA";
        case 13: return "ROCKET";
        case 14: return "TETHER";
        case 15: return "GROUND";
        default: return "UNKNOWN";
    }
}

void reset(Info& out) { out = Info(); }

// Decodes one 25-byte message body into `out`.
static bool mergeMessage(const uint8_t* m, Info& out, uint32_t now) {
    const uint8_t type = (uint8_t)((m[0] >> 4) & 0x0F);

    if (type == MSG_BASIC_ID) {
        // Byte 1 splits IDType (high) from UAType (low); the serial is the
        // next 20 bytes, space- or NUL-padded rather than terminated.
        out.uaType = (uint8_t)(m[1] & 0x0F);
        memcpy(out.serial, m + 2, 20);
        out.serial[20] = '\0';
        for (int i = 19; i >= 0; i--) {
            if (out.serial[i] == ' ' || out.serial[i] == '\0') out.serial[i] = '\0';
            else break;
        }
        out.haveBasic = true;
        out.at = now;
        return true;
    }

    if (type == MSG_LOCATION) {
        const int32_t la = rd32(m + 5);
        const int32_t lo = rd32(m + 9);
        // 0,0 is the standard's "unknown", and it is also a real place, so
        // treating it as no-fix is the lesser wrong of the two.
        if (la == 0 && lo == 0) return true;
        out.lat  = (float)((double)la / LATLON_MULT);
        out.lon  = (float)((double)lo / LATLON_MULT);
        out.altM = (float)rd16(m + 15) * ALT_DIV - ALT_ADDER;
        out.haveLoc = true;
        out.at = now;
        return true;
    }

    if (type == MSG_SYSTEM) {
        // The interesting one. Bytes 2-9 are where the pilot is standing.
        const int32_t la = rd32(m + 2);
        const int32_t lo = rd32(m + 6);
        if (la == 0 && lo == 0) return true;
        out.opLat = (float)((double)la / LATLON_MULT);
        out.opLon = (float)((double)lo / LATLON_MULT);
        out.haveOperator = true;
        out.at = now;
        return true;
    }

    // Auth, Self-ID, Operator-ID and Message Pack all reach here. Pack
    // (0xF) is the one worth naming: it wraps several messages at once and
    // is what the WiFi Beacon form uses, but it does not fit in a 31-byte
    // legacy advert, so nothing that arrives on this path can be one.
    return false;
}

bool merge(const uint8_t* payload, uint8_t len, Info& out, uint32_t now) {
    if (!payload || len < 4) return false;

    uint8_t i = 0;
    while (i < len) {
        const uint8_t adLen = payload[i];
        // A zero length is the standard's end-of-data padding, and any
        // structure claiming to run past the buffer is malformed. Both mean
        // stop rather than continue: this is attacker-reachable input.
        if (adLen == 0) break;
        if ((uint16_t)i + 1u + adLen > (uint16_t)len) break;

        const uint8_t  adType = payload[i + 1];
        const uint8_t* adData = payload + i + 2;
        const uint8_t  adSize = (uint8_t)(adLen - 1);   // type byte excluded

        if (adType == AD_SERVICE_DATA_16 && adSize >= 3 &&
            rd16(adData) == ODID_UUID && adData[2] == ODID_APP_CODE) {
            // UUID, app code, counter, then the message itself.
            if (adSize >= 4 + MSG_SIZE) {
                return mergeMessage(adData + 4, out, now);
            }
            return false;
        }
        i = (uint8_t)(i + 1 + adLen);
    }
    return false;
}

// The 0xFFFA service-data structure in a legacy advert, or null. Remote ID is
// the only place it can live -- the 25-byte message fills the advert, leaving
// no room to also list 0xFFFA as a service UUID -- so this tells a real drone
// from anything merely advertising that UUID.
static const uint8_t* findServiceData(const uint8_t* payload, uint8_t len, uint8_t& size) {
    if (!payload || len < 4) return nullptr;
    uint8_t i = 0;
    while (i < len) {
        const uint8_t adLen = payload[i];
        if (adLen == 0) break;
        if ((uint16_t)i + 1u + adLen > (uint16_t)len) break;
        const uint8_t  adType = payload[i + 1];
        const uint8_t* adData = payload + i + 2;
        const uint8_t  adSize = (uint8_t)(adLen - 1);
        if (adType == AD_SERVICE_DATA_16 && adSize >= 3 &&
            rd16(adData) == ODID_UUID && adData[2] == ODID_APP_CODE) {
            size = adSize;
            return adData;
        }
        i = (uint8_t)(i + 1 + adLen);
    }
    return nullptr;
}

bool present(const uint8_t* payload, uint8_t len) {
    uint8_t size = 0;
    return findServiceData(payload, len, size) != nullptr;
}

// The WiFi Beacon form: an ASD-STAN vendor element (OUI FA:0B:BC, app code
// 0x0D) packing several messages together, in a beacon's tagged parameters.
// Many drones broadcast this as well as (or instead of) the Bluetooth form.
static const uint8_t IE_VENDOR  = 221;
static const uint8_t ASD_OUI[3] = { 0xFA, 0x0B, 0xBC };
static const uint8_t MSG_PACK   = 0xF;
static const uint8_t PACK_MAX   = 9;

bool mergeBeacon(const uint8_t* ies, uint16_t len, Info& out, uint32_t now) {
    if (!ies) return false;
    uint16_t i = 0;
    while ((uint32_t)i + 2u <= len) {
        const uint8_t id = ies[i], ieLen = ies[i + 1];
        const uint8_t* body = ies + i + 2;
        if ((uint32_t)i + 2u + ieLen > len) break;
        if (id == IE_VENDOR && ieLen >= 8 && memcmp(body, ASD_OUI, 3) == 0 && body[3] == ODID_APP_CODE) {
            const uint8_t* pack = body + 5;
            const uint8_t  room = (uint8_t)(ieLen - 5);
            if (((pack[0] >> 4) & 0x0F) != MSG_PACK || pack[1] != MSG_SIZE) return false;
            uint8_t n = pack[2];
            if (n > PACK_MAX) n = PACK_MAX;
            if (3u + (uint16_t)n * MSG_SIZE > room) n = (uint8_t)((room - 3) / MSG_SIZE);
            for (uint8_t k = 0; k < n; k++) mergeMessage(pack + 3 + k * MSG_SIZE, out, now);
            return true;
        }
        i = (uint16_t)(i + 2 + ieLen);
    }
    return false;
}

}  // namespace RemoteId
