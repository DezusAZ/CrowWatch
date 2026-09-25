// SquachWatch — transmitter test page. Broadcasts each detection signature
// (BLE advert or raw WiFi frame) so a second unit can be verified.
#pragma once
#include <TFT_eSPI.h>

void uiTxTestInit(TFT_eSPI& t);
void uiTxTestTick(TFT_eSPI& t, uint32_t now);
// A tap at x,y: selects/toggles a device box, or returns true if BACK was hit.
bool uiTxTestTouch(int x, int y);
// Drives the active WiFi transmission; call every loop while on the page.
void uiTxTestRadioTick();
// Stops all transmission and releases the BLE advertiser back to the mesh.
void uiTxTestStop();
