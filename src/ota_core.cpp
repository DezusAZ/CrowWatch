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
        case Fail::CANCELLED:       return "Cancelled. Nothing was changed.";
        case Fail::LOST_CONNECTION: return "The browser disconnected. Nothing was changed. Try again closer to the board.";
        case Fail::BAD_CODE:        return "The wrong code was entered three times. Start again for a new code.";
        case Fail::TOO_BIG:         return "That firmware is too big for this board. Nothing was changed.";
        case Fail::NOT_FIRMWARE:    return "That file is not firmware. Nothing was changed.";
        case Fail::WRITE_ERROR:     return "Could not write to the board's memory. Nothing was changed.";
        case Fail::BAD_SIGNATURE:   return "Not an official build for this board, so it was refused. Nothing was changed.";
        case Fail::DAMAGED:         return "The firmware arrived damaged. Nothing was changed. Try again.";
        case Fail::TIMEOUT:         return "The download stopped. Nothing was changed.";
        case Fail::LOCKED:          return "Unlock the board first.";
        case Fail::RADIO_BUSY:      return "Bluetooth was busy. Leave this screen and try again.";
        case Fail::WIFI_NOT_FOUND:  return "Couldn't find that WiFi network. Move closer to the router and try again.";
        case Fail::WIFI_PASSWORD:   return "Couldn't join that WiFi network. Check the password and try again.";
        case Fail::NO_SITE:         return "Joined WiFi, but couldn't reach squachwatch.com. Check the internet connection.";
        case Fail::NOT_SIGNED:      return "The latest release can't be installed over the air yet. Use the USB flasher.";
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
const char* buildName()      { return SQW_ENV; }

uint32_t maxImageSize() {
    const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
    return p ? p->size : 0;
}

void boot() {
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
    const esp_err_t e = esp_ota_begin(target, size, &s_handle);
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

void abort() {
    lock();
    closeLocked();
    s_written = 0;
    unlock();
}

}  // namespace OtaCore
