// SquachWatch-CYD -- WIFI NETWORKS, on the SYSTEM page.
//
// The board remembers up to OtaWifi::SAVED_MAX networks. This screen lists
// them, marks the one the board tries first (USE), removes them, and adds a
// new one from a scan. A new network's password is not checked here -- that
// would mean joining, and joining means giving Bluetooth up until a restart.
// It is checked at the next boot check instead, and the row says how that
// went: joined, wrong password, or not found.
//
// Two screens: the list (WIFI_NETS) and the scan to add from (WIFI_ADD). The
// password for a locked network is typed on the same keyboard the update
// flow uses (WIFI_PASS); main.cpp routes the result back here.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"

enum class WifiNetsHit : uint8_t { NONE, ROW, USE, REMOVE, ADD, BACK };
enum class WifiAddHit  : uint8_t { NONE, ROW, RESCAN, BACK };

void        uiWifiNetsInit(TFT_eSPI& t);
void        uiWifiNetsTick(TFT_eSPI& t, uint32_t now);
// The row index comes back in *row for ROW.
WifiNetsHit uiWifiNetsHit(TFT_eSPI& t, int x, int y, int* row);
void        uiWifiNetsSelect(int row);   // -1 clears the highlight
int         uiWifiNetsSelected();

void        uiWifiAddInit(TFT_eSPI& t);
void        uiWifiAddTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
WifiAddHit  uiWifiAddHit(TFT_eSPI& t, int x, int y, const DetectionEngine& eng, int* row);
