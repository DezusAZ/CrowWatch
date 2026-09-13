// SquachWatch-Sim — pretend firmware updates, so the UPDATE FIRMWARE screen
// can be walked through in the emulator. No radio, no flash: each transport
// plays a scripted run on a timer.
//
// Bluetooth:  0-4 s WAITING, 4-7 s CONNECTED (code accepted at 5.5 s),
//             7-19 s RECEIVING, then VERIFYING, DONE, and back to the menu.
// WiFi:       1.5 s SCANNING, then a list of four networks. Joining takes
//             2 s, checking 2 s, then READY with v1.7.2 on offer. INSTALL
//             downloads for 8 s, verifies, and finishes.
//             A network called "WrongPassword" fails to join, to show TRY AGAIN.
#include "ota_core.h"
#include "ota_ble.h"
#include "ota_wifi.h"
#include <Arduino.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "sim"
#endif

static const uint32_t SIM_SIZE = 1416816;

// ---- OtaCore --------------------------------------------------------------------
namespace OtaCore {

static bool     s_restart   = false;
static uint32_t s_restartAt = 0;

const char* failWords(Fail f) {
    switch (f) {
        case Fail::WIFI_PASSWORD: return "Couldn't join that WiFi network. Check the password and try again.";
        case Fail::CANCELLED:     return "Cancelled. Nothing was changed.";
        default:                  return "Something went wrong. Nothing was changed.";
    }
}
bool        available()                        { return true; }
void        boot()                             {}
void        tick(uint32_t now)                 { if (s_restart && now >= s_restartAt) s_restart = false; }
const char* takeBootNote(const char**, bool*)  { return nullptr; }
const char* runningSlot()                      { return "app0"; }
const char* runningVersion()                   { return "v1.7.1"; }   // reads naturally in the release clip
const char* buildName()                        { return "sim"; }
uint32_t    maxImageSize()                     { return 1966080; }
void        refreshOther()                     {}
const char* otherVersion()                     { return "v1.7.0"; }
Fail        switchToOther()                    { restartSoon(3000); return Fail::NONE; }
void        restartSoon(uint32_t ms)           { s_restart = true; s_restartAt = millis() + ms; }
bool        restartPending()                   { return s_restart; }
Fail        begin(uint32_t, const uint8_t*, uint8_t) { return Fail::NONE; }
bool        write(const uint8_t*, size_t)      { return true; }
uint32_t    written()                          { return 0; }
Fail        finish()                           { return Fail::NONE; }
void        abort()                            {}

}  // namespace OtaCore

// ---- OtaBle ---------------------------------------------------------------------
namespace OtaBle {

static State    s_state = State::OFF;
static uint32_t s_t0    = 0;

bool available() { return true; }
bool begin() { s_state = State::WAITING; s_t0 = millis(); return true; }
void end()   { s_state = State::OFF; }

void tick(uint32_t now) {
    if (s_state == State::OFF || s_state == State::FAILED) return;
    const uint32_t e = now - s_t0;
    if      (e <  4000) s_state = State::WAITING;
    else if (e <  7000) s_state = State::CONNECTED;
    else if (e < 19000) s_state = State::RECEIVING;
    else if (e < 20500) s_state = State::VERIFYING;
    else if (e < 24000) s_state = State::DONE;
    else                s_state = State::OFF;
}

State    state()         { return s_state; }
bool     codeAccepted()  { return s_state == State::CONNECTED && millis() - s_t0 > 5500; }
uint32_t bytesExpected() { return s_state >= State::RECEIVING ? SIM_SIZE : 0; }
uint32_t bytesReceived() {
    if (s_state != State::RECEIVING) return s_state >= State::VERIFYING ? SIM_SIZE : 0;
    return (uint32_t)((uint64_t)SIM_SIZE * (millis() - s_t0 - 7000) / 12000);
}
uint8_t percent() {
    const uint32_t x = bytesExpected();
    return x ? (uint8_t)((uint64_t)bytesReceived() * 100 / x) : 0;
}
const char* failureText() { return "Cancelled. Nothing was changed."; }
const char* deviceName()  { return "SquachWatch-E5E6"; }
uint32_t    pairingCode() { return 482913; }

}  // namespace OtaBle

// ---- OtaWifi --------------------------------------------------------------------
namespace OtaWifi {

static State    s_state = State::OFF;
static uint32_t s_t0    = 0;
static char     s_net[33] = "";
static bool     s_badPass = false;
static bool     s_saved   = true;
static const Net NETS[] = {
    { "SquachNet",     -48, false },
    { "Neighbours5G",  -63, false },
    { "WrongPassword", -70, false },
    { "CoffeeShop",    -81, true  },
};

bool begin() { s_state = State::SCANNING; s_t0 = millis(); return true; }
bool end()   { s_state = State::OFF; return false; }
void rescan() { s_state = State::SCANNING; s_t0 = millis(); }

void tick(uint32_t now) {
    const uint32_t e = now - s_t0;
    switch (s_state) {
        case State::SCANNING:    if (e > 1500) s_state = State::PICK; break;
        case State::CONNECTING:  if (e > 2000) { if (s_badPass) s_state = State::FAILED; else { s_state = State::CHECKING; s_t0 = now; } } break;
        case State::CHECKING:    if (e > 2000) s_state = State::READY; break;
        case State::DOWNLOADING: if (e > 8000) { s_state = State::VERIFYING; s_t0 = now; } break;
        case State::VERIFYING:   if (e > 1500) { s_state = State::DONE; s_t0 = now; } break;
        case State::DONE:        if (e > 3000) s_state = State::OFF; break;
        default: break;
    }
}

State      state()    { return s_state; }
uint8_t    netCount() { return sizeof NETS / sizeof NETS[0]; }
const Net* net(uint8_t i) { return i < netCount() ? &NETS[i] : nullptr; }
bool        hasSaved()  { return s_saved; }
const char* savedSsid() { return s_saved ? "SquachNet" : ""; }
void        forget()    { s_saved = false; }

void connect(const char* ssid, const char*, bool) {
    strncpy(s_net, ssid, sizeof s_net - 1);
    s_badPass = !strcmp(ssid, "WrongPassword");
    s_state = State::CONNECTING;
    s_t0 = millis();
}
void connectSaved() { connect("SquachNet", "", false); }

const char* network()       { return s_net; }
const char* latestVersion() { return "v1.7.2"; }
bool        upToDate()      { return false; }
void        install()       { if (s_state == State::READY) { s_state = State::DOWNLOADING; s_t0 = millis(); } }
bool        canTryAgain()   { return s_state == State::FAILED; }
void        tryAgain()      { rescan(); }

uint32_t bytesExpected() { return s_state >= State::DOWNLOADING ? SIM_SIZE : 0; }
uint32_t bytesReceived() {
    if (s_state != State::DOWNLOADING) return s_state > State::DOWNLOADING ? SIM_SIZE : 0;
    return (uint32_t)((uint64_t)SIM_SIZE * (millis() - s_t0) / 8000);
}
uint8_t percent() {
    const uint32_t x = bytesExpected();
    return x ? (uint8_t)((uint64_t)bytesReceived() * 100 / x) : 0;
}
const char* failureText() { return OtaCore::failWords(OtaCore::Fail::WIFI_PASSWORD); }

}  // namespace OtaWifi
