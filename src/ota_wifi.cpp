// SquachWatch-CYD — firmware updates over WiFi. See include/ota_wifi.h.
#include "ota_wifi.h"
#include "ota_roots.h"
#include "security.h"
#include "clock.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef OTA_WIFI_BASE
#define OTA_WIFI_BASE "https://squachwatch.com/"
#endif

using OtaCore::Fail;

namespace OtaWifi {
namespace {

const char* NVS_NS = "otawifi";
const uint32_t JOIN_TIMEOUT_MS  = 20000;
const uint32_t STALL_TIMEOUT_MS = 15000;
const uint32_t TASK_STACK       = 12288;   // the TLS handshake is the deep part

volatile State s_state   = State::OFF;
volatile Fail  s_fail    = Fail::NONE;
volatile bool  s_install = false;
volatile bool  s_cancel  = false;
volatile bool  s_downloadStarted = false;
volatile uint32_t s_rx   = 0;
volatile uint32_t s_size = 0;

Net     s_nets[NET_MAX];
uint8_t s_netN = 0;

char    s_ssid[33]  = "";
char    s_pass[65]  = "";
bool    s_save      = false;
char    s_saved[33] = "";
bool    s_savedRead = false;
char    s_latest[24] = "";
uint8_t s_sig[80];
uint8_t s_sigLen = 0;

TaskHandle_t s_task       = nullptr;
bool         s_btReleased = false;

void readSaved() {
    if (s_savedRead) return;
    s_savedRead = true;
    Preferences p;
    if (p.begin(NVS_NS, true)) {
        strncpy(s_saved, p.getString("ssid", "").c_str(), sizeof s_saved - 1);
        s_saved[sizeof s_saved - 1] = '\0';
        p.end();
    }
}

void fail(Fail f) {
    if (s_state == State::FAILED) return;
    s_fail  = f;
    s_state = State::FAILED;
    Serial.printf("[ota] wifi update stopped: %s\n", OtaCore::failWords(f));
}

void startScan() {
    s_netN = 0;
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* async */, false /* no hidden */);
    s_state = State::SCANNING;
}

void collectScan(int n) {
    s_netN = 0;
    for (int i = 0; i < n; i++) {
        String name = WiFi.SSID(i);
        if (!name.length()) continue;
        const int8_t rssi = (int8_t)WiFi.RSSI(i);
        const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        // One row per name: the same network from two access points is one
        // choice, shown at its strongest.
        int found = -1;
        for (uint8_t k = 0; k < s_netN; k++) if (name == s_nets[k].ssid) { found = k; break; }
        if (found >= 0) {
            if (rssi > s_nets[found].rssi) s_nets[found].rssi = rssi;
            continue;
        }
        Net cand;
        strncpy(cand.ssid, name.c_str(), sizeof cand.ssid - 1);
        cand.ssid[sizeof cand.ssid - 1] = '\0';
        cand.rssi = rssi;
        cand.open = open;
        if (s_netN < NET_MAX) {
            s_nets[s_netN++] = cand;
        } else {
            // Full: replace the weakest if this one is stronger.
            uint8_t weakest = 0;
            for (uint8_t k = 1; k < s_netN; k++) if (s_nets[k].rssi < s_nets[weakest].rssi) weakest = k;
            if (rssi > s_nets[weakest].rssi) s_nets[weakest] = cand;
        }
    }
    // Strongest first.
    for (uint8_t i = 1; i < s_netN; i++)
        for (uint8_t j = i; j > 0 && s_nets[j].rssi > s_nets[j - 1].rssi; j--) {
            Net t = s_nets[j]; s_nets[j] = s_nets[j - 1]; s_nets[j - 1] = t;
        }
    WiFi.scanDelete();
}

// Everything Bluetooth holds, handed back for the TLS handshake. Cannot be
// undone without a restart -- see the header.
void releaseBluetooth() {
    if (s_btReleased) return;
    const uint32_t before = ESP.getFreeHeap();
    NimBLEDevice::deinit(true);
    esp_bt_mem_release(ESP_BT_MODE_BTDM);
    s_btReleased = true;
    Serial.printf("[ota] bluetooth released: heap %lu -> %lu, largest block %lu\n",
                  (unsigned long)before, (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static WiFiClientSecure* s_tls   = nullptr;
static WiFiClient*       s_plain = nullptr;
WiFiClient* client() {
    WiFiClientSecure*& tls   = s_tls;
    WiFiClient*&       plain = s_plain;
    if (strncmp(OTA_WIFI_BASE, "https://", 8) == 0) {
        if (!tls) {
            tls = new WiFiClientSecure();
            tls->setCACert(OTA_ROOTS_PEM);
            tls->setHandshakeTimeout(20);
        }
        return tls;
    }
    if (!plain) plain = new WiFiClient();
    return plain;
}

// A small file into `out`. Returns the HTTP status, or a negative number when
// nothing came back at all.
int getSmall(const String& file, uint8_t* out, size_t cap, size_t& len) {
    len = 0;
    HTTPClient http;
    if (!http.begin(*client(), String(OTA_WIFI_BASE) + file)) return -1;
    http.setTimeout(STALL_TIMEOUT_MS);
    const int code = http.GET();
    if (code == 200) {
        WiFiClient* s = http.getStreamPtr();
        const int total = http.getSize();
        uint32_t t0 = millis();
        while (len < cap && (total < 0 || (int)len < total) && millis() - t0 < STALL_TIMEOUT_MS) {
            const int a = s->available();
            if (a > 0) {
                const int r = s->read(out + len, (size_t)a < cap - len ? (size_t)a : cap - len);
                if (r > 0) { len += (size_t)r; t0 = millis(); }
            } else if (!http.connected()) {
                break;
            } else {
                delay(5);
            }
        }
    }
    http.end();
    return code;
}

// Pulls "version": "1.7.2" out of a flasher manifest without a JSON library.
bool parseVersion(const char* body, char* out, size_t cap) {
    const char* k = strstr(body, "\"version\"");
    if (!k) return false;
    const char* q = strchr(k + 9, '"');
    if (!q) return false;
    const char* e = strchr(q + 1, '"');
    if (!e || e == q + 1) return false;
    size_t n = (size_t)(e - (q + 1));
    const bool addV = q[1] != 'v';
    if (n + (addV ? 1 : 0) >= cap) return false;
    size_t o = 0;
    if (addV) out[o++] = 'v';
    memcpy(out + o, q + 1, n);
    out[o + n] = '\0';
    return true;
}

bool join() {
    s_state = State::CONNECTING;
    Serial.printf("[ota] joining %s\n", s_ssid);
    WiFi.begin(s_ssid, s_pass[0] ? s_pass : nullptr);
    const uint32_t t0 = millis();
    wl_status_t st = WiFi.status();
    while (st != WL_CONNECTED && millis() - t0 < JOIN_TIMEOUT_MS && !s_cancel) {
        if (st == WL_CONNECT_FAILED) break;
        delay(100);
        st = WiFi.status();
    }
    if (s_cancel) return false;
    if (st != WL_CONNECTED) {
        fail(st == WL_NO_SSID_AVAIL ? Fail::WIFI_NOT_FOUND : Fail::WIFI_PASSWORD);
        return false;
    }
    Serial.printf("[ota] joined %s as %s\n", s_ssid, WiFi.localIP().toString().c_str());
    // The clock rides along: one NTP round trip while the radio is up anyway.
    Clock::syncWait(1500);
    if (s_save) {
        Preferences p;
        p.begin(NVS_NS, false);
        p.putString("ssid", s_ssid);
        p.putString("pass", s_pass);
        p.end();
        strncpy(s_saved, s_ssid, sizeof s_saved - 1);
        s_saved[sizeof s_saved - 1] = '\0';
        s_savedRead = true;
    }
    return true;
}

bool check() {
    s_state = State::CHECKING;
    Serial.printf("[ota] checking %s, largest block %lu\n", OTA_WIFI_BASE,
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    uint8_t body[1024];
    size_t  len = 0;
    int code = getSmall(String("manifest-") + OtaCore::buildName() + ".json", body, sizeof body - 1, len);
    if (code != 200) {
        Serial.printf("[ota] manifest: HTTP %d\n", code);
        fail(Fail::NO_SITE);
        return false;
    }
    body[len] = '\0';
    if (!parseVersion((const char*)body, s_latest, sizeof s_latest)) { fail(Fail::NO_SITE); return false; }

    code = getSmall(String(OtaCore::buildName()) + "-firmware.sig", s_sig, sizeof s_sig, len);
    if (code == 404) { fail(Fail::NOT_SIGNED); return false; }
    if (code != 200 || len < 8 || len >= sizeof s_sig) {
        Serial.printf("[ota] signature: HTTP %d, %u bytes\n", code, (unsigned)len);
        fail(code == 200 ? Fail::NOT_SIGNED : Fail::NO_SITE);
        return false;
    }
    s_sigLen = (uint8_t)len;
    Serial.printf("[ota] latest is %s, running %s\n", s_latest, FIRMWARE_VERSION);
    return true;
}

void download() {
    s_state = State::DOWNLOADING;
    s_downloadStarted = true;
    s_rx = 0;
    HTTPClient http;
    if (!http.begin(*client(), String(OTA_WIFI_BASE) + OtaCore::buildName() + "-firmware.bin")) {
        fail(Fail::NO_SITE);
        return;
    }
    http.setTimeout(STALL_TIMEOUT_MS);
    const int code = http.GET();
    const int size = http.getSize();
    if (code != 200 || size <= 0) {
        Serial.printf("[ota] firmware: HTTP %d, size %d\n", code, size);
        http.end();
        fail(Fail::NO_SITE);
        return;
    }
    Fail f = OtaCore::begin((uint32_t)size, s_sig, s_sigLen);
    if (f != Fail::NONE) { http.end(); fail(f); return; }
    s_size = (uint32_t)size;

    uint8_t* buf = (uint8_t*)malloc(4096);
    if (!buf) { http.end(); OtaCore::abort(); fail(Fail::LOW_MEMORY); return; }
    WiFiClient* s = http.getStreamPtr();
    uint32_t last = millis();
    while (s_rx < s_size && !s_cancel && s_state == State::DOWNLOADING) {
        const int a = s->available();
        if (a <= 0) {
            if (!http.connected() || millis() - last > STALL_TIMEOUT_MS) break;
            delay(5);
            continue;
        }
        size_t want = (size_t)a;
        if (want > 4096) want = 4096;
        if (want > s_size - s_rx) want = s_size - s_rx;
        const int r = s->read(buf, want);
        if (r <= 0) continue;
        if (!OtaCore::write(buf, (size_t)r)) {
            fail(s_rx == 0 ? Fail::NOT_FIRMWARE : Fail::WRITE_ERROR);
            break;
        }
        s_rx += (uint32_t)r;
        last = millis();
    }
    free(buf);
    http.end();

    if (s_cancel || s_state == State::FAILED) { OtaCore::abort(); return; }
    if (s_rx != s_size) { OtaCore::abort(); fail(Fail::TIMEOUT); return; }

    s_state = State::VERIFYING;
    f = OtaCore::finish();
    if (f != Fail::NONE) { fail(f); return; }
    s_state = State::DONE;
    OtaCore::restartSoon(4000);
}

void run(void*) {
    WiFi.scanDelete();
    Clock::syncStop();
    WiFi.disconnect(false, false);
    delay(100);
    releaseBluetooth();
    if (join()) {
        // The password has done its job; it only lives on in NVS if saved.
        memset(s_pass, 0, sizeof s_pass);
        if (check()) {
            s_state = State::READY;
            while (!s_install && !s_cancel) delay(50);
            if (!s_cancel) download();
        }
    }
    memset(s_pass, 0, sizeof s_pass);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool begin() {
    if (s_state != State::OFF) return true;
    if (Security::locked() || !OtaCore::available()) return false;
    readSaved();
    s_fail = Fail::NONE;
    s_cancel = s_install = s_downloadStarted = false;
    s_rx = s_size = 0;
    WiFi.mode(WIFI_STA);
    // A saved network is used straight away. The list only appears when there
    // is nothing saved, or through TRY AGAIN when the saved one cannot be
    // joined -- which is also how somebody who has moved picks a new one.
    if (s_saved[0]) {
        Serial.printf("[ota] wifi update mode: using saved network %s\n", s_saved);
        s_state = State::PICK;
        connectSaved();
        return true;
    }
    startScan();
    Serial.println("[ota] wifi update mode: scanning");
    return true;
}

bool end() {
    if (s_state == State::OFF) return false;
    s_cancel = true;
    if (s_btReleased) {
        // Bluetooth's memory is gone until a restart, and so is detection.
        OtaCore::restartSoon(1500);
        return true;
    }
    WiFi.scanDelete();
    s_state = State::OFF;
    Serial.println("[ota] wifi update mode off");
    return false;
}

void tick(uint32_t) {
    if (s_state != State::SCANNING) return;
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n > 0) collectScan(n);
    else       s_netN = 0;
    s_state = State::PICK;
}

void rescan() {
    if (s_state == State::PICK || s_state == State::FAILED) startScan();
}

State      state()    { return s_state; }
uint8_t    netCount() { return s_netN; }
const Net* net(uint8_t i) { return i < s_netN ? &s_nets[i] : nullptr; }

bool hasSaved() { readSaved(); return s_saved[0] != '\0'; }
bool savedPass(char* out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!hasSaved()) return false;
    Preferences p;
    if (!p.begin(NVS_NS, true)) return false;
    strncpy(out, p.getString("pass", "").c_str(), cap - 1);
    out[cap - 1] = '\0';
    p.end();
    return true;
}
const char* savedSsid() { readSaved(); return s_saved; }

void forget() {
    Preferences p;
    if (p.begin(NVS_NS, false)) { p.clear(); p.end(); }
    s_saved[0] = '\0';
    s_savedRead = true;
}

void connect(const char* ssid, const char* pass, bool save) {
    if (s_task || (s_state != State::PICK && s_state != State::FAILED)) return;
    strncpy(s_ssid, ssid ? ssid : "", sizeof s_ssid - 1);
    s_ssid[sizeof s_ssid - 1] = '\0';
    strncpy(s_pass, pass ? pass : "", sizeof s_pass - 1);
    s_pass[sizeof s_pass - 1] = '\0';
    s_save    = save;
    s_fail    = Fail::NONE;
    s_cancel  = s_install = s_downloadStarted = false;
    s_state   = State::CONNECTING;
    if (xTaskCreatePinnedToCore(run, "otawifi", TASK_STACK, nullptr, 1, &s_task, 1) != pdPASS) {
        s_task = nullptr;
        memset(s_pass, 0, sizeof s_pass);
        fail(Fail::LOW_MEMORY);
    }
}

bool bootCheck(uint32_t budgetMs) {
    readSaved();
    if (!s_saved[0]) return false;
    char pass[65] = "";
    {
        Preferences p;
        if (p.begin(NVS_NS, true)) {
            strncpy(pass, p.getString("pass", "").c_str(), sizeof pass - 1);
            p.end();
        }
    }
    const uint32_t t0 = millis();
    Serial.printf("[ota] boot check: joining %s (heap %lu, largest %lu)\n", s_saved,
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    WiFi.mode(WIFI_STA);
    WiFi.begin(s_saved, pass[0] ? pass : nullptr);
    wl_status_t st = WiFi.status();
    // Two thirds of the budget for the join, the rest for the fetch.
    while (st != WL_CONNECTED && millis() - t0 < budgetMs * 2 / 3) {
        if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) break;
        delay(50);
        st = WiFi.status();
    }
    memset(pass, 0, sizeof pass);
    bool found = false;
    if (st == WL_CONNECTED) {
        Serial.printf("[ota] boot check: joined in %lu ms\n", (unsigned long)(millis() - t0));
        // The clock, asked at the same time as the manifest so the two
        // answers overlap; waited for below, briefly, once the manifest is in.
        Clock::syncStart();
        uint8_t body[1024];
        size_t  len = 0;
        const uint32_t left = budgetMs - (millis() - t0);
        // Plain HTTP, on purpose. A TLS handshake wants 40 KB in one piece
        // and five to ten seconds, and one that timed out left a dead
        // connection in the middle of the heap that cost the frame buffer
        // its block -- measured, twice. The site answers the manifest over
        // plain HTTP, and nothing rides on this answer but a notice: the
        // install itself goes over HTTPS and checks the signature.
        String base = OTA_WIFI_BASE;
        if (base.startsWith("https://")) base = "http://" + base.substring(8);
        WiFiClient plain;
        HTTPClient http;
        if (http.begin(plain, base + "manifest-" + OtaCore::buildName() + ".json")) {
            http.setConnectTimeout((int32_t)left);
            http.setTimeout((uint16_t)(left > 60000 ? 60000 : left));
            const int code = http.GET();
            if (code == 200) {
                WiFiClient* s = http.getStreamPtr();
                const int total = http.getSize();     // the server keeps the connection open, so the
                const uint32_t t1 = millis();         // content length is what says "that is all of it"
                while (len < sizeof body - 1 && (total < 0 || (int)len < total) && millis() - t1 < left) {
                    const int a = s->available();
                    if (a > 0) { const int r = s->read(body + len, (size_t)a < sizeof body - 1 - len ? (size_t)a : sizeof body - 1 - len); if (r > 0) len += (size_t)r; }
                    else if (!http.connected()) break;
                    else delay(5);
                }
                Serial.printf("[ota] boot check: manifest %u bytes in %lu ms\n", (unsigned)len, (unsigned long)(millis() - t1));
                body[len] = '\0';
                char latest[16];
                if (parseVersion((const char*)body, latest, sizeof latest)) {
                    Serial.printf("[ota] boot check: site has %s, running %s\n", latest, FIRMWARE_VERSION);
                    OtaCore::noteAvailable(latest, "");
                    found = true;
                }
            } else {
                Serial.printf("[ota] boot check: manifest HTTP %d\n", code);
            }
            http.end();
        }
    } else {
        Serial.printf("[ota] boot check: no join (%d) in %lu ms\n", (int)st, (unsigned long)(millis() - t0));
    }
    if (st == WL_CONNECTED) {
        // A time server answers in well under a second; this is the cap on
        // a bad day, not the usual cost. Skipped once the clock is fresh.
        const uint32_t t2 = millis();
        const uint32_t used = t2 - t0;
        uint32_t left = used < budgetMs ? budgetMs - used : 0;
        if (left > 2500) left = 2500;
        const bool ok = Clock::syncWait(left);
        char clk[24];
        Clock::formatClock(clk, sizeof clk);
        Serial.printf("[clock] %s in %lu ms (%s)\n", ok ? "set" : "no answer",
                      (unsigned long)(millis() - t2), clk);
    }
    Clock::syncStop();
    // Everything back the way it was: the driver torn down, so Bluetooth
    // starts into the heap it always had.
    WiFi.disconnect(true, true);
    Serial.printf("[ota] boot check: disconnected (heap %lu, largest %lu)\n",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    WiFi.mode(WIFI_OFF);
    Serial.printf("[ota] boot check done in %lu ms (heap %lu, largest %lu)\n", (unsigned long)(millis() - t0),
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return found;
}

void connectSaved() {
    readSaved();
    if (!s_saved[0]) return;
    char pass[65] = "";
    Preferences p;
    if (p.begin(NVS_NS, true)) {
        strncpy(pass, p.getString("pass", "").c_str(), sizeof pass - 1);
        p.end();
    }
    connect(s_saved, pass, false);
    memset(pass, 0, sizeof pass);
}

const char* network()       { return s_ssid; }
const char* latestVersion() { return s_latest; }
bool        upToDate()      { return s_latest[0] && strcmp(s_latest, FIRMWARE_VERSION) == 0; }
void        install()       { if (s_state == State::READY) s_install = true; }

bool canTryAgain() { return s_state == State::FAILED && !s_downloadStarted && !s_task; }
void tryAgain() {
    if (!canTryAgain()) return;
    Clock::syncStop();
    WiFi.disconnect(false, false);
    startScan();
}

uint8_t     percent()       { return s_size ? (uint8_t)((uint64_t)s_rx * 100 / s_size) : 0; }
uint32_t    bytesReceived() { return s_rx; }
uint32_t    bytesExpected() { return s_size; }
const char* failureText()   { return OtaCore::failWords(s_fail); }

}  // namespace OtaWifi
