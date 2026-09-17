// SquachWatch-CYD — the transport-free half of firmware updates. See
// include/ota_core.h.
#include "ota_core.h"
#include "ota_pubkey.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "mbedtls/pk.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef SQW_ENV
#define SQW_ENV "unknown"
#endif

// The Arduino core marks a freshly booted image valid before setup() even
// runs, unless this says to wait -- and waiting is the entire point of
// rollback. tick() confirms the image once it has proved it can run. C
// linkage, because the weak default it replaces lives in esp32-hal-misc.c.
extern "C" bool verifyRollbackLater() { return true; }

namespace OtaCore {
namespace {

// The NVS namespace this module owns. Per-slot records ("v_app0", "e_app0")
// say which version and which build each slot last BOOTED as; "try" names the
// slot an update or switch just pointed the bootloader at.
const char* NVS_NS = "ota";

SemaphoreHandle_t      s_lock       = nullptr;
const esp_partition_t* s_target     = nullptr;
esp_ota_handle_t       s_handle     = 0;
bool                   s_open       = false;
mbedtls_sha256_context s_sha;
bool                   s_shaOpen    = false;
uint32_t               s_size       = 0;
volatile uint32_t      s_written    = 0;
uint8_t                s_sig[80];
uint8_t                s_sigLen     = 0;

// The version, inside the signed bytes.
//
// An ESP32 image does carry a version field of its own, but on an Arduino
// build it holds the framework's ("esp-idf: v4.4.6 ..."), not ours -- read
// off a real release to check. So every build stamps its own marker into its
// rodata, and an incoming image is scanned for the same marker as it
// arrives. It sits inside what the signature covers, so it cannot be
// rewritten by whoever is serving the file.
const char SQW_IMAGE_VERSION[] = "SQWVER:" FIRMWARE_VERSION ";";
const char             VER_TAG[]    = "SQWVER:";
uint8_t                s_tagMatch   = 0;
bool                   s_verTaking  = false;
bool                   s_verFound   = false;
char                   s_imgVer[32] = "";

// One pass over the bytes as they arrive, stopping at the first marker.
void scanVersion(const uint8_t* data, size_t len) {
    if (s_verFound) return;
    for (size_t i = 0; i < len; i++) {
        const char c = (char)data[i];
        if (s_verTaking) {
            if (c == ';' || s_tagMatch >= sizeof s_imgVer - 1) {
                s_imgVer[s_tagMatch] = 0;
                s_verTaking = false;
                s_verFound  = true;
                return;
            }
            s_imgVer[s_tagMatch++] = c;
            continue;
        }
        if (c == VER_TAG[s_tagMatch]) {
            if (++s_tagMatch == sizeof VER_TAG - 1) { s_tagMatch = 0; s_verTaking = true; }
        } else {
            s_tagMatch = (c == VER_TAG[0]) ? 1 : 0;
        }
    }
}

bool     s_restart   = false;
uint32_t s_restartAt = 0;

char s_otherVer[32] = "";
bool s_otherOk      = false;

bool        s_probation = false;
const char* s_noteHead  = nullptr;
const char* s_noteSub   = nullptr;
bool        s_noteGood  = false;
char        s_noteSubBuf[40];

void lock()   { if (!s_lock) s_lock = xSemaphoreCreateMutex(); xSemaphoreTake(s_lock, portMAX_DELAY); }
void unlock() { xSemaphoreGive(s_lock); }

// Caller holds the lock.
void closeLocked() {
    if (s_open)    { esp_ota_abort(s_handle); s_open = false; }
    if (s_shaOpen) { mbedtls_sha256_free(&s_sha); s_shaOpen = false; }
}

}  // namespace

const char* failWords(Fail f) {
    switch (f) {
        case Fail::CANCELLED:       return "Cancelled. Your current version is untouched.";
        case Fail::LOST_CONNECTION: return "The browser disconnected. Your current version is untouched. Try again closer to the board.";
        case Fail::BAD_CODE:        return "The wrong code was entered three times. Start again for a new code.";
        case Fail::TOO_BIG:         return "That firmware is too big for this board. Nothing was changed.";
        case Fail::NOT_FIRMWARE:    return "That file is not firmware. Nothing was changed.";
        case Fail::WRITE_ERROR:     return "Could not write to the board's memory. Your current version is untouched.";
        case Fail::BAD_SIGNATURE:   return "Not an official build for this board, so it was refused. Your current version is untouched.";
        case Fail::DAMAGED:         return "The firmware arrived damaged. Your current version is untouched. Try again.";
        case Fail::TIMEOUT:         return "The download stopped. Your current version is untouched.";
        case Fail::LOCKED:          return "Unlock the board first.";
        case Fail::RADIO_BUSY:      return "Bluetooth was busy. Leave this screen and try again.";
        case Fail::WIFI_NOT_FOUND:  return "Couldn't find that WiFi network. Move closer to the router and try again.";
        case Fail::WIFI_PASSWORD:   return "Couldn't join that WiFi network. Check the password and try again.";
        case Fail::NO_SITE:         return "Joined WiFi, but couldn't reach squachwatch.com. Check the internet connection.";
        case Fail::NOT_SIGNED:      return "The latest release can't be installed over the air yet. Use the USB flasher.";
        case Fail::TOO_OLD:         return "That firmware is older than the one running. Nothing was changed.";
        case Fail::LOW_MEMORY:      return "Not enough memory to download. Restart the board and try again.";
        default:                    return "";
    }
}

bool available() { return esp_ota_get_next_update_partition(nullptr) != nullptr; }

const char* runningSlot() {
    const esp_partition_t* p = esp_ota_get_running_partition();
    return p ? p->label : "?";
}
const char* runningVersion() { return FIRMWARE_VERSION; }
static char     s_avail[16]  = "";
static char     s_availFrom[13] = "";
static bool     s_availSaid = true;
// The release's own words, from the manifest. Sized for the window that
// shows them: thirty-six characters is what the NOTES tab fits in portrait,
// and a line written longer than that was written for the wrong screen.
static char     s_relName[20] = "";
static char     s_news[NEWS_MAX][40];
static uint8_t  s_newsN = 0;

// "1.7.8" or "v1.7.8" into three numbers; false for anything else.
static bool verParts(const char* s, unsigned v[3]) {
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    return sscanf(s, "%u.%u.%u", &v[0], &v[1], &v[2]) == 3;
}
static bool verNewer(const char* a, const char* b) {   // a newer than b
    unsigned x[3], y[3];
    if (!verParts(a, x) || !verParts(b, y)) return false;
    for (int i = 0; i < 3; i++) { if (x[i] != y[i]) return x[i] > y[i]; }
    return false;
}

void noteAvailable(const char* version, const char* who) {
    if (!verNewer(version, runningVersion())) return;
    if (s_avail[0] && !verNewer(version, s_avail)) return;   // already know one as new
    if (*version == 'v' || *version == 'V') version++;
    // A different version from the one the lines were about: drop them
    // rather than show one release's notes under another's number.
    if (strcmp(s_avail, version) != 0) { s_relName[0] = '\0'; s_newsN = 0; }
    snprintf(s_avail, sizeof s_avail, "%s", version);
    snprintf(s_availFrom, sizeof s_availFrom, "%s", who ? who : "");
    s_availSaid = false;
    Serial.printf("[ota] newer release known: %s%s%s\n", s_avail, s_availFrom[0] ? " via " : "", s_availFrom);
}
const char* availableVersion() { return s_avail; }
void noteRelease(const char* name, const char* const* lines, uint8_t n) {
    snprintf(s_relName, sizeof s_relName, "%s", name ? name : "");
    s_newsN = n > NEWS_MAX ? NEWS_MAX : n;
    for (uint8_t i = 0; i < s_newsN; i++) snprintf(s_news[i], sizeof s_news[i], "%s", lines[i] ? lines[i] : "");
}
const char* releaseName() { return s_relName; }
uint8_t     newsCount()   { return s_newsN; }
const char* newsAt(uint8_t i) { return i < s_newsN ? s_news[i] : ""; }
const char* availableFrom()    { return s_availFrom; }
bool takeAvailableNotice() {
    if (s_availSaid || !s_avail[0]) return false;
    s_availSaid = true;
    return true;
}

const char* buildName()      { return SQW_ENV; }
uint32_t maxImageSize() {
    const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
    return p ? p->size : 0;
}

void boot() {
    // Printed, not just declared: a marker nothing reads is a marker the
    // linker is entitled to drop, and the whole point is that it ships
    // inside the signed image.
    Serial.printf("[ota] %s\n", SQW_IMAGE_VERSION);
    const esp_partition_t* run = esp_ota_get_running_partition();
    if (!run) return;

    Preferences p;
    p.begin(NVS_NS, false);
    char key[16];
    snprintf(key, sizeof key, "v_%s", run->label);
    if (p.getString(key, "") != FIRMWARE_VERSION) p.putString(key, FIRMWARE_VERSION);
    snprintf(key, sizeof key, "e_%s", run->label);
    if (p.getString(key, "") != SQW_ENV) p.putString(key, SQW_ENV);

    esp_ota_img_states_t st;
    s_probation = esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;

    const String tried = p.getString("try", "");
    if (tried.length() && tried != run->label) {
        // The bootloader was pointed at another slot and we are not in it:
        // that image failed to confirm itself and was rolled back.
        p.remove("try");
        s_noteHead = "UPDATE UNDONE";
        s_noteSub  = "New version didn't start";
        s_noteGood = false;
        Serial.printf("[ota] %s did not confirm; rolled back to %s\n", tried.c_str(), run->label);
    } else if (tried.length() && !s_probation) {
        p.remove("try");
    }
    p.end();

    if (s_probation) {
        Serial.printf("[ota] %s on probation: confirms after %lu s\n", run->label,
                      (unsigned long)(CONFIRM_MS / 1000));
    }
}

void tick(uint32_t now) {
    if (s_restart && now >= s_restartAt) {
        Serial.println("[ota] restarting");
        Serial.flush();
        ESP.restart();
    }
    if (!s_probation || now < CONFIRM_MS) return;
    s_probation = false;
    if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) return;
    Preferences p;
    p.begin(NVS_NS, false);
    p.remove("try");
    p.end();
    snprintf(s_noteSubBuf, sizeof s_noteSubBuf, "Now on %s", FIRMWARE_VERSION);
    s_noteHead = "UPDATE CONFIRMED";
    s_noteSub  = s_noteSubBuf;
    s_noteGood = true;
    Serial.println("[ota] new firmware confirmed");
}

const char* takeBootNote(const char** sub, bool* good) {
    const char* h = s_noteHead;
    if (!h) return nullptr;
    if (sub)  *sub  = s_noteSub;
    if (good) *good = s_noteGood;
    s_noteHead = nullptr;
    return h;
}

void refreshOther() {
    s_otherOk     = false;
    s_otherVer[0] = '\0';
    const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
    if (!other) return;

    // Rolled back or never finished: the bootloader will not take it.
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(other, &st) == ESP_OK &&
        (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED)) return;

    // Something with an image header at all. After a website install the
    // other slot is blank, or holds the tail of an old single-slot image.
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) != ESP_OK) return;

    // And something that booted HERE, as THIS board's build. A slot that was
    // never booted has no record; one from a different board's build does not
    // match, and switching into a wrong display driver is a white screen.
    Preferences p;
    p.begin(NVS_NS, true);
    char key[16];
    snprintf(key, sizeof key, "v_%s", other->label);
    const String v = p.getString(key, "");
    snprintf(key, sizeof key, "e_%s", other->label);
    const String e = p.getString(key, "");
    p.end();
    if (!v.length() || e != SQW_ENV) return;

    strncpy(s_otherVer, v.c_str(), sizeof s_otherVer - 1);
    s_otherVer[sizeof s_otherVer - 1] = '\0';
    s_otherOk = true;
}

const char* otherVersion() { return s_otherOk ? s_otherVer : nullptr; }

Fail switchToOther() {
    refreshOther();
    const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
    // set_boot_partition verifies the whole image before it agrees.
    if (!s_otherOk || !other || esp_ota_set_boot_partition(other) != ESP_OK) return Fail::DAMAGED;
    Preferences p;
    p.begin(NVS_NS, false);
    p.putString("try", other->label);
    p.end();
    Serial.printf("[ota] switching to %s (%s)\n", other->label, s_otherVer);
    restartSoon(1500);
    return Fail::NONE;
}

void restartSoon(uint32_t ms) {
    s_restart   = true;
    s_restartAt = millis() + ms;
}
bool restartPending() { return s_restart; }

// ---- installer --------------------------------------------------------------

Fail begin(uint32_t size, const uint8_t* sig, uint8_t sigLen) {
    abort();
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) return Fail::WRITE_ERROR;
    if (size < 1024 || size > target->size) return Fail::TOO_BIG;
    if (!sig || sigLen == 0 || sigLen > sizeof s_sig) return Fail::BAD_SIGNATURE;
    Serial.printf("[ota] receiving %lu bytes into %s\n", (unsigned long)size, target->label);

    lock();
    // Erase as the writes arrive, a sector at a time -- NOT the whole image up
    // front. v1.7.2 erased all ~1.6 MB in one call, which held the flash long
    // enough that core 0's idle task missed the 5 s task watchdog, and the
    // board rebooted mid-update on the very first real release it tried to
    // fetch (the bench image before it was just small enough to squeak past).
    // Sequential erase costs a few tens of milliseconds per 4 KB sector, spread
    // across the download, and never blocks anything for long.
    const esp_err_t e = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (e == ESP_OK) {
        s_target  = target;
        s_open    = true;
        s_size    = size;
        s_written = 0;
        memcpy(s_sig, sig, sigLen);
        s_sigLen  = sigLen;
        mbedtls_sha256_init(&s_sha);
        mbedtls_sha256_starts_ret(&s_sha, 0);
        s_shaOpen = true;
        static const char PREFIX[] = "SQWOTA1\n";
        mbedtls_sha256_update_ret(&s_sha, (const uint8_t*)PREFIX, sizeof PREFIX - 1);
        mbedtls_sha256_update_ret(&s_sha, (const uint8_t*)SQW_ENV, strlen(SQW_ENV));
        mbedtls_sha256_update_ret(&s_sha, (const uint8_t*)"\n", 1);
    }
    unlock();
    if (e != ESP_OK) {
        Serial.printf("[ota] esp_ota_begin failed: 0x%x\n", (unsigned)e);
        return Fail::WRITE_ERROR;
    }
    return Fail::NONE;
}

bool write(const uint8_t* data, size_t len) {
    lock();
    scanVersion(data, len);
    bool ok = s_open && s_written + len <= s_size;
    if (ok && s_written == 0 && len && data[0] != 0xE9) ok = false;   // not an ESP32 image
    if (ok) ok = esp_ota_write(s_handle, data, len) == ESP_OK;
    if (ok) {
        mbedtls_sha256_update_ret(&s_sha, data, len);
        s_written += len;
    }
    unlock();
    return ok;
}

uint32_t written() { return s_written; }

Fail finish() {
    lock();
    if (!s_open || s_written != s_size) { closeLocked(); unlock(); return Fail::DAMAGED; }
    uint8_t hash[32];
    mbedtls_sha256_finish_ret(&s_sha, hash);
    mbedtls_sha256_free(&s_sha);
    s_shaOpen = false;
    unlock();

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)OTA_PUBKEY_PEM, sizeof OTA_PUBKEY_PEM);
    if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof hash, s_sig, s_sigLen);
    mbedtls_pk_free(&pk);
    if (rc != 0) {
        Serial.printf("[ota] signature check failed (-0x%04x)\n", (unsigned)-rc);
        abort();
        return Fail::BAD_SIGNATURE;
    }

    // Older than the one running: refused, however well signed it is. The
    // download comes over plain HTTP now, so somebody on the same network can
    // answer with a real, signed, OLD release -- one with a bug that has since
    // been fixed. The signature says the file is ours; this says it is not a
    // step backwards. An equal version is allowed, because reinstalling the
    // version you are on is a repair, not an attack.
    if (s_verFound) {
        if (verNewer(runningVersion(), s_imgVer)) {
            Serial.printf("[ota] refused %s: older than %s\n", s_imgVer, runningVersion());
            abort();
            return Fail::TOO_OLD;
        }
        Serial.printf("[ota] the image says it is %s\n", s_imgVer);
    } else {
        // A release from before the marker existed. Its signature still has
        // to check out; there is simply no version in it to compare.
        Serial.println("[ota] the image carries no version marker");
    }

    // esp_ota_end() checks the image itself: its segments and its own hash.
    lock();
    esp_err_t e = esp_ota_end(s_handle);
    s_open = false;
    unlock();
    if (e != ESP_OK) {
        Serial.printf("[ota] esp_ota_end failed: 0x%x\n", (unsigned)e);
        return e == ESP_ERR_OTA_VALIDATE_FAILED ? Fail::DAMAGED : Fail::WRITE_ERROR;
    }
    e = esp_ota_set_boot_partition(s_target);
    if (e != ESP_OK) {
        Serial.printf("[ota] set_boot_partition failed: 0x%x\n", (unsigned)e);
        return Fail::DAMAGED;
    }

    Preferences p;
    p.begin(NVS_NS, false);
    p.putString("try", s_target->label);
    p.end();
    Serial.printf("[ota] signature good, installed into %s\n", s_target->label);
    return Fail::NONE;
}

const char* testVersionDecision(const char* version) {
    char fake[64];
    const int n = snprintf(fake, sizeof fake, "...SQWVER:%s;...", version ? version : "");
    s_tagMatch  = 0;
    s_verTaking = false;
    s_verFound  = false;
    s_imgVer[0] = 0;
    scanVersion((const uint8_t*)fake, (size_t)n);
    const bool older = s_verFound && verNewer(runningVersion(), s_imgVer);
    static char out[96];
    snprintf(out, sizeof out, "read \"%s\" from the image, running %s: %s",
             s_verFound ? s_imgVer : "(none)", runningVersion(),
             older ? "REFUSED, older" : "accepted");
    s_tagMatch  = 0;
    s_verTaking = false;
    s_verFound  = false;
    s_imgVer[0] = 0;
    return out;
}

void abort() {
    lock();
    closeLocked();
    s_written    = 0;
    s_tagMatch   = 0;
    s_verTaking  = false;
    s_verFound   = false;
    s_imgVer[0]  = 0;
    unlock();
}

}  // namespace OtaCore
