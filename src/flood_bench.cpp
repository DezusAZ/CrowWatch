// SquachWatch-CYD -- the fake flood. See include/flood_bench.h.
#include "flood_bench.h"
#if FLOOD_BENCH

// The scan's event handler is private to the library; this is a bench file
// and the one place that reaches past that.
#define private public
#include <NimBLEScan.h>
#undef private
#include <NimBLEDevice.h>
#include <Arduino.h>
#if defined(CONFIG_NIMBLE_CPP_IDF)
#include "nimble/nimble_port.h"
#else
#include "nimble/porting/nimble/include/nimble/nimble_port.h"
#endif
#include <string.h>

static volatile uint16_t s_perSec = 0;
static volatile uint16_t s_owed   = 0;   // 50 ms slots owed to the host task
static struct ble_npl_event s_ev;
static bool     s_evReady = false;
static uint32_t s_made    = 0;

// Runs on the host task: one burst, the share of a second that 50 ms holds.
static void burstOnHost(struct ble_npl_event*) {
    // Every 50 ms slot that has elapsed since the last burst, not one: the
    // loop that posts this runs at the frame rate, and a slow frame used to
    // mean a slot silently skipped -- FLOOD 200 delivered 120-180 a second
    // and said 200. The rate the board reports (adv, ble/s) is the truth.
    const uint16_t slots = s_owed;
    s_owed = 0;
    const uint16_t n = (uint16_t)(((s_perSec + 19) / 20) * slots);
    for (uint16_t i = 0; i < n; i++) {
        // A phone's advert: flags, then a short name, from a fresh random address.
        uint8_t data[14] = { 0x02, 0x01, 0x06, 0x0A, 0x09, 'F','L','O','O','D','0','0','0','0' };
        const uint32_t k = s_made++;
        data[10] = (uint8_t)('A' + (k >> 12 & 15)); data[11] = (uint8_t)('A' + (k >> 8 & 15));
        data[12] = (uint8_t)('A' + (k >> 4 & 15));  data[13] = (uint8_t)('A' + (k & 15));
        ble_gap_event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = BLE_GAP_EVENT_DISC;
        ev.disc.event_type  = BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;   // connectable and scannable: the kind a scanner waits on
        ev.disc.length_data = sizeof data;
        ev.disc.data        = data;
        ev.disc.rssi        = (int8_t)(-55 - (int8_t)(k % 30));
        ev.disc.addr.type   = BLE_ADDR_RANDOM;
        for (int b = 0; b < 6; b++) ev.disc.addr.val[b] = (uint8_t)esp_random();
        ev.disc.addr.val[5] |= 0xC0;                             // a static random address
        NimBLEScan::handleGapEvent(&ev, nullptr);
    }
}

void floodSet(uint16_t perSecond) {
    s_perSec = perSecond;
    Serial.printf("[flood] %u fake adverts a second, connectable, never answering\n", (unsigned)perSecond);
}

uint16_t floodRate() { return s_perSec; }

void floodTick() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (!s_perSec) { last = now; return; }
    uint16_t owed = 0;
    while (now - last >= 50 && owed < 20) { last += 50; owed++; }
    if (now - last >= 50) last = now;   // more than a second behind: drop the rest, do not spiral
    if (!owed) return;
    s_owed = (uint16_t)(s_owed + owed);
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!scan || !scan->isScanning()) return;
    if (!s_evReady) { ble_npl_event_init(&s_ev, burstOnHost, nullptr); s_evReady = true; }
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_ev);
}

#endif
