// SquachWatch-CYD — the UPDATE FIRMWARE screen, from Settings -> SYSTEM.
//
// Three ways off the version you are on: download the latest over WiFi, send
// it from the website over Bluetooth, or switch back to whatever is sitting in
// the other slot. The last needs no download and no browser, so it works for
// anybody. See ota_core.h for how all three stay safe.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

enum class UpdateHit : uint8_t {
    NONE,
    WIFI_START,      // UPDATE OVER WIFI
    BT_START,        // UPDATE OVER BLUETOOTH
    SQUAD_START,     // UPDATE SQUAD: nudge every board in range (SquachMesh builds)
    SWITCH,          // SWITCH TO <other version> -- asks first
    SWITCH_CONFIRM,
    SWITCH_CANCEL,
    NETWORK,         // a row in the WiFi list; see netIndex
    RESCAN,
    FORGET,          // unused since the WIFI NETWORKS screen took over; kept so the numbering holds
    INSTALL,
    TRY_AGAIN,       // back to the WiFi list after a failure
    CANCEL,          // leaves update mode
    OK,              // dismisses a failure and leaves update mode
    BACK,            // out to Settings
};

void      uiUpdateInit(TFT_eSPI& t);
// `full` false repaints only what moves -- the progress bar and its numbers --
// over what is already on the panel. For drawing straight to the display once
// the frame buffer has been released, where a full repaint is a visible flash.
void      uiUpdateTick(TFT_eSPI& t, uint32_t now, bool full = true);
UpdateHit uiUpdateHitTest(TFT_eSPI& t, int x, int y, int* netIndex);
void      uiUpdateAskSwitch(bool ask);
