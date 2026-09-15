// SquachWatch-CYD — boot splash screen
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

void uiBootInit(TFT_eSPI& t);
void uiBootTick(TFT_eSPI& t, uint32_t now);
bool uiBootDone(uint32_t startMs, uint32_t holdMs = 3000);
