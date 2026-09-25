// SquachWatch — transmitter test page. See ui_txtest.h.
#include "ui_txtest.h"
#include "theme.h"
#include "detection.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <string>

// esp_wifi_80211_tx rejects beacon/deauth frames unless this passes.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) { return 0; }

namespace {

enum Radio : uint8_t { R_BLE_UUID, R_BLE_MFG, R_BLE_NAME, R_BLE_AIRTAG, R_BLE_IBEACON,
                       R_WIFI_OUI, R_WIFI_DEAUTH, R_WIFI_EVILTWIN };

struct Item {
    const char* label;
    Radio       radio;
    uint16_t    u16;
    uint8_t     oui[3];
    const char* name;
};

// One tile per SquachWatch DetectionType (UNKNOWN excluded).
const Item ITEMS[] = {
    { "FLOCK",    R_BLE_MFG,      0x09C8, {0,0,0},          nullptr },
    { "AXON",     R_WIFI_OUI,     0,      {0x00,0x25,0xDF}, "AXONTEST" },
    { "META",     R_BLE_UUID,     0xFD5F, {0,0,0},          nullptr },
    { "SKIMMER",  R_BLE_NAME,     0,      {0,0,0},          "HC-05" },
    { "RAVEN",    R_BLE_UUID,     0x3100, {0,0,0},          nullptr },
    { "AIRTAG",   R_BLE_AIRTAG,   0,      {0,0,0},          nullptr },
    { "DRONE",    R_BLE_UUID,     0xFFFA, {0,0,0},          nullptr },
    { "ALPR",     R_WIFI_OUI,     0,      {0x00,0x04,0x7D}, "ALPRTEST" },
    { "CAMERA",   R_WIFI_OUI,     0,      {0x2C,0xAA,0x8E}, "CAMTEST" },
    { "SMSNGTAG", R_BLE_UUID,     0xFD5A, {0,0,0},          nullptr },
    { "GOOGTAG",  R_BLE_UUID,     0xFEAA, {0,0,0},          nullptr },
    { "TILE",     R_BLE_UUID,     0xFEED, {0,0,0},          nullptr },
    { "RING",     R_WIFI_OUI,     0,      {0xAC,0x9F,0xC3}, "RINGTEST" },
    { "DEAUTH",   R_WIFI_DEAUTH,  0,      {0,0,0},          nullptr },
    { "EVILTWIN", R_WIFI_EVILTWIN,0,      {0,0,0},          "FreeWiFi" },
    { "IBEACON",  R_BLE_IBEACON,  0,      {0,0,0},          nullptr },
    { "HACKER",   R_BLE_UUID,     0x3081, {0,0,0},          nullptr },
};
const int N_ITEMS = sizeof(ITEMS) / sizeof(ITEMS[0]);

const int COLS = 3, ROWS = 6;
const int TOP = 50;          // status band height
int active = -1;
int backX, backY, backW, backH;

bool isWifi(Radio r) { return r == R_WIFI_OUI || r == R_WIFI_DEAUTH || r == R_WIFI_EVILTWIN; }

void cellRect(int i, TFT_eSPI& t, int& x, int& y, int& w, int& h) {
    const int c = i % COLS, r = i / COLS;
    const int cw = t.width() / COLS;
    const int ch = (t.height() - TOP) / ROWS;
    const int gap = 6;
    w = cw - gap; h = ch - gap;
    x = c * cw + gap / 2;
    y = TOP + r * ch + gap / 2;
}

void centered(TFT_eSPI& t, const char* s, int cx, int cy, uint8_t size, uint16_t col) {
    t.setTextSize(size);
    t.setTextColor(col);
    const int w = t.textWidth(s);
    t.setCursor(cx - w / 2, cy - 4 * size);
    t.print(s);
}

// ---- BLE ----
bool bleReady = false;
void bleStart(const Item& it) {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) return;
    adv->stop();
    NimBLEAdvertisementData d;
    d.setFlags(0x06);
    if (it.radio == R_BLE_UUID) {
        d.setCompleteServices(NimBLEUUID((uint16_t)it.u16));
    } else if (it.radio == R_BLE_MFG) {
        std::string m;
        m.push_back((char)(it.u16 & 0xFF)); m.push_back((char)(it.u16 >> 8));
        m.push_back(0x01); m.push_back(0x00);
        d.setManufacturerData(m);
    } else if (it.radio == R_BLE_NAME) {
        d.setName(it.name);
    } else if (it.radio == R_BLE_AIRTAG) {
        const uint8_t b[] = { 0x4C,0x00,0x12,0x19, 0x10,0x00,
            0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,0x11,0x22,0x33,0x44,0x55,
            0x66,0x77,0x88,0x99,0xAA,0xBB, 0x00 };
        d.setManufacturerData(b, sizeof(b));
    } else if (it.radio == R_BLE_IBEACON) {
        const uint8_t b[] = { 0x4C,0x00,0x02,0x15,
            0xE2,0xC5,0x6D,0xB5,0xDF,0xFB,0x48,0xD2,0xB0,0x60,0xD0,0xF5,0xA7,0x10,0x96,0xE0,
            0x00,0x01, 0x00,0x02, 0xC5 };
        d.setManufacturerData(b, sizeof(b));
    }
    adv->setAdvertisementData(d);
    adv->setConnectableMode(0);   // non-connectable broadcast
    adv->start();
}

// ---- WiFi raw TX ----
uint8_t s_frame[256];
uint8_t s_ch = 1;

int buildBeacon(const uint8_t* bssid, const char* ssid, bool enc, uint8_t ch) {
    uint8_t* f = s_frame; int n = 0;
    f[n++] = 0x80; f[n++] = 0x00; f[n++] = 0x00; f[n++] = 0x00;
    for (int i = 0; i < 6; i++) f[n++] = 0xFF;
    for (int i = 0; i < 6; i++) f[n++] = bssid[i];
    for (int i = 0; i < 6; i++) f[n++] = bssid[i];
    f[n++] = 0x00; f[n++] = 0x00;
    for (int i = 0; i < 8; i++) f[n++] = 0x00;
    f[n++] = 0x64; f[n++] = 0x00;
    f[n++] = enc ? 0x11 : 0x01; f[n++] = 0x00;
    const uint8_t sl = strlen(ssid);
    f[n++] = 0x00; f[n++] = sl;
    for (uint8_t i = 0; i < sl; i++) f[n++] = ssid[i];
    f[n++] = 0x01; f[n++] = 0x08;
    f[n++]=0x82; f[n++]=0x84; f[n++]=0x8b; f[n++]=0x96; f[n++]=0x24; f[n++]=0x30; f[n++]=0x48; f[n++]=0x6c;
    f[n++] = 0x03; f[n++] = 0x01; f[n++] = ch;
    return n;
}

int buildDeauth(const uint8_t* bssid) {
    uint8_t* f = s_frame; int n = 0;
    f[n++] = 0xC0; f[n++] = 0x00; f[n++] = 0x00; f[n++] = 0x00;
    for (int i = 0; i < 6; i++) f[n++] = 0xFF;
    for (int i = 0; i < 6; i++) f[n++] = bssid[i];
    for (int i = 0; i < 6; i++) f[n++] = bssid[i];
    f[n++] = 0x00; f[n++] = 0x00;
    f[n++] = 0x07; f[n++] = 0x00;
    return n;
}

} // namespace

void uiTxTestInit(TFT_eSPI& t) {
    active = -1;
    (void)t;
    if (!bleReady) { bleReady = true; }   // NimBLE already init'd by the engine
}

void uiTxTestStop() {
    active = -1;
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (adv) adv->stop();
    Mesh::setAdvertiserBorrowed(false);
}

void uiTxTestTick(TFT_eSPI& t, uint32_t now) {
    (void)now;
    t.fillScreen(Theme::BG);

    // Status band: a solid dark bar so text is legible whatever is selected.
    t.fillRect(0, 0, t.width(), TOP, Theme::BLACK);
    t.drawFastHLine(0, TOP - 1, t.width(), Theme::CYAN);
    // BACK: a filled cyan button with black text -- unmistakably a button.
    backX = 6; backY = 8; backW = 96; backH = TOP - 16;
    t.fillRoundRect(backX, backY, backW, backH, 6, Theme::CYAN);
    centered(t, "BACK", backX + backW / 2, backY + backH / 2, 2, Theme::BLACK);
    // Status text, left-aligned after the button: cyan when idle, green
    // when sending, so the state reads at a glance without a loud fill.
    const int sx = backX + backW + 14;
    if (active < 0) {
        t.setTextSize(2); t.setTextColor(Theme::CYAN);
        t.setCursor(sx, TOP / 2 - 8); t.print("TX TEST");
    } else {
        char s[40];
        snprintf(s, sizeof s, "SENDING: %s", ITEMS[active].label);
        t.setTextSize(2); t.setTextColor(Theme::GREEN);
        t.setCursor(sx, TOP / 2 - 8); t.print(s);
    }

    // Grid.
    for (int i = 0; i < N_ITEMS; i++) {
        int x, y, w, h; cellRect(i, t, x, y, w, h);
        const bool on = (i == active);
        t.fillRoundRect(x, y, w, h, 8, on ? Theme::GREEN : Theme::WHITE);
        t.drawRoundRect(x, y, w, h, 8, Theme::BLACK);
        uint8_t size = 3;
        t.setTextSize(size);
        if (t.textWidth(ITEMS[i].label) > w - 10) size = 2;
        centered(t, ITEMS[i].label, x + w / 2, y + h / 2, size, Theme::BLACK);
    }
}

bool uiTxTestTouch(int x, int y) {
    if (x >= backX && x <= backX + backW && y >= backY && y <= backY + backH) {
        uiTxTestStop();
        return true;
    }
    if (y < TOP) return false;
    // Which cell.
    extern TFT_eSPI tft;
    int idx = -1;
    for (int i = 0; i < N_ITEMS; i++) {
        int cx, cy, cw, ch; cellRect(i, tft, cx, cy, cw, ch);
        if (x >= cx && x <= cx + cw && y >= cy && y <= cy + ch) { idx = i; break; }
    }
    if (idx < 0) return false;

    if (idx == active) { uiTxTestStop(); return false; }

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (adv) adv->stop();
    active = idx;
    const Item& it = ITEMS[idx];
    Mesh::setAdvertiserBorrowed(true);      // keep mesh off the advertiser
    if (isWifi(it.radio)) {
        if (adv) { /* leave BLE idle */ }
    } else {
        bleStart(it);
    }
    Serial.printf("[txtest] %s\n", it.label);
    return false;
}

void uiTxTestRadioTick() {
    if (active < 0) return;
    const Item& it = ITEMS[active];
    if (!isWifi(it.radio)) return;
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (now - last < 100) return;
    last = now;
    s_ch = (s_ch == 1) ? 6 : (s_ch == 6) ? 11 : 1;
    esp_wifi_set_channel(s_ch, WIFI_SECOND_CHAN_NONE);
    if (it.radio == R_WIFI_OUI) {
        uint8_t b[6] = { it.oui[0], it.oui[1], it.oui[2], 0x11, 0x22, 0x33 };
        int len = buildBeacon(b, it.name, true, s_ch);
        esp_wifi_80211_tx(WIFI_IF_STA, s_frame, len, false);
    } else if (it.radio == R_WIFI_EVILTWIN) {
        uint8_t a[6] = { 0xDE,0xAD,0x01,0x11,0x22,0x33 };
        uint8_t c[6] = { 0xBE,0xEF,0x02,0x44,0x55,0x66 };
        int l1 = buildBeacon(a, it.name, true, s_ch);
        esp_wifi_80211_tx(WIFI_IF_STA, s_frame, l1, false);
        int l2 = buildBeacon(c, it.name, false, s_ch);
        esp_wifi_80211_tx(WIFI_IF_STA, s_frame, l2, false);
    } else {   // R_WIFI_DEAUTH
        uint8_t b[6] = { 0xDE,0xAD,0xBE,0xEF,0x00,0x01 };
        int len = buildDeauth(b);
        for (int i = 0; i < 10; i++) esp_wifi_80211_tx(WIFI_IF_STA, s_frame, len, false);
    }
}
