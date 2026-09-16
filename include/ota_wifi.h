// SquachWatch-CYD — firmware updates over WiFi: the transport only.
//
// The board finds nearby networks, you pick yours and type the password on
// the board, and it downloads the latest release for its own build straight
// from squachwatch.com. No computer, no browser, so it works for anybody --
// iPhone owners included. Everything that makes the install itself safe is in
// OtaCore (ota_core.h); this file only gets the bytes there.
//
// WHY BLUETOOTH GOES AWAY. A TLS handshake wants 40 KB or more of CONTIGUOUS
// heap, and a running board has about 18 KB in its largest block. Bluetooth's
// controller and host hold far more than that, and they are no use during a
// download, so the moment a network is chosen they are shut down and their
// memory handed back. NimBLE cannot be brought back up after its memory has
// been released, which is why leaving WiFi update mode restarts the board.
//
// THE PASSWORD. Saved only if the network joins, in its own NVS namespace
// ("otawifi"), which the duress PIN erases along with the other secrets.
//
// OTA_WIFI_BASE overrides where it downloads from, for a bench test against a
// local server: PLATFORMIO_BUILD_FLAGS='-DOTA_WIFI_BASE=\"http://192.168.4.42:8767/\"'.
// Plain http is accepted there and nowhere else by default.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ota_core.h"

namespace OtaWifi {

enum class State : uint8_t {
    OFF = 0,
    SCANNING,       // looking for networks
    PICK,           // a list to choose from
    CONNECTING,     // joining the chosen network (Bluetooth is off from here)
    CHECKING,       // fetching the latest version number and its signature
    READY,          // latestVersion() is known; waiting for INSTALL
    DOWNLOADING,
    VERIFYING,
    DONE,           // installed; the board restarts shortly
    FAILED,         // see failureText()
};

struct Net {
    char   ssid[33];
    int8_t rssi;
    bool   open;
};
static const uint8_t NET_MAX = 12;

// Enter update mode and start a scan. The caller pauses detection first (see
// DetectionEngine::startUpdateRadio). Refuses while the device is locked.
bool begin();
// Leave update mode. True when the board is about to restart to get
// Bluetooth back (it had already been released); false when the caller should
// resume detection itself.
bool end();
void tick(uint32_t now);

void rescan();

State       state();
uint8_t     netCount();
const Net*  net(uint8_t i);

bool        hasSaved();
const char* savedSsid();
// The saved password, for the squad update nudge to share. Into the
// caller's buffer, which the caller wipes.
bool        savedPass(char* out, size_t cap);
void        forget();

// The list behind those: up to SAVED_MAX networks, managed on the WIFI
// NETWORKS screen. savedSsid() and savedPass() above are the one marked USE:
// the boot check tries it first when it is in range, and the squad update
// nudge shares it. A network added on the board is not checked by joining --
// joining means giving Bluetooth up until a restart -- so its password is
// tried at the next boot check, and savedResult() says how that went.
static const uint8_t SAVED_MAX = 6;
enum class SavedResult : uint8_t { UNTRIED = 0, JOINED, BAD_PASSWORD, NOT_FOUND };
uint8_t     savedCount();
const char* savedSsidAt(uint8_t i);
uint8_t     savedUse();
int8_t      savedIndexOf(const char* ssid);   // -1 when it is not saved
SavedResult savedResult(uint8_t i);
// Add a network, or replace the password of one already saved. False when
// the list is full.
bool        saveNetwork(const char* ssid, const char* pass);
void        removeSaved(uint8_t i);
void        useSaved(uint8_t i);
// Join saved network i. connectSaved() below joins the best saved network in
// the last scan -- the one marked USE if it is there, else the strongest --
// and the one marked USE blind when the scan showed none.
void        connectSavedAt(uint8_t i);
// The list on serial, for the bench: WIFI on the console.
void        printSaved();

// Join a network and check for the latest release. `save` keeps the password
// if the network joins.
void connect(const char* ssid, const char* pass, bool save);
void connectSaved();
// The boot check. Joins the saved network, reads the site's manifest for
// this build, hands anything newer to OtaCore::noteAvailable, and shuts
// WiFi down again. Blocking, time-boxed by `budgetMs`, and ONLY safe before
// Bluetooth starts: joining WiFi with NimBLE up means giving it up until
// the next restart, which is why the running-board path is a whole mode.
// False when there is no saved network, or nothing came back in time.
bool bootCheck(uint32_t budgetMs);

const char* network();          // the one being joined or used
const char* latestVersion();    // meaningful from READY on
bool        upToDate();
void        install();          // READY -> DOWNLOADING

// From FAILED, before anything was downloaded: back to the network list.
bool        canTryAgain();
void        tryAgain();

uint8_t     percent();
uint32_t    bytesReceived();
uint32_t    bytesExpected();
const char* failureText();

}  // namespace OtaWifi
