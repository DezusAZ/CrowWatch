// SquachWatch-CYD — wall-clock time. See clock.h.
#include "clock.h"
#include "security.h"   // a locked device takes no console commands
#include "settings.h"
#include "crowd_bench.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   // strncasecmp
#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WiFiUdp.h>
#endif

namespace Clock {

// 2025-01-01. An ESP32 that has never been told the time reports 1970, and
// a mistyped command is usually either tiny or enormous, so one threshold
// screens out both the unset case and the fat-fingered one.
static const uint32_t kPlausible = 1735689600u;

static Preferences s_prefs;
static bool        s_begun    = false;
static bool        s_synced   = false;
static uint32_t    s_born     = 0;
static uint32_t    s_greeted  = 0;
static uint32_t    s_mileSaid = 0;
static bool        s_guess    = false;   // running from the note to self
static uint32_t    s_lastNote = 0;       // millis() of the last note written
static const uint32_t NOTE_EVERY_MS = 10u * 60u * 1000u;

#if !defined(ARDUINO_ARCH_ESP32)
// The emulator: SQUACH_EPOCH in the environment pins the clock to a moment
// (moving forward with millis() from there), so a one-shot screen renders
// the same at any hour. Without it the host's real clock is used.
static uint32_t s_simEpoch = 0, s_simMs0 = 0;
static bool     s_simInit  = false;
static void simInit() {
    if (s_simInit) return;
    s_simInit = true;
    const char* e = getenv("SQUACH_EPOCH");
    if (e && *e) { s_simEpoch = (uint32_t)strtoul(e, nullptr, 10); s_simMs0 = millis(); }
}
#endif

static uint32_t rawNow() {
#if !defined(ARDUINO_ARCH_ESP32)
    simInit();
    if (s_simEpoch) return s_simEpoch + (millis() - s_simMs0) / 1000u;
#endif
    return (uint32_t)time(nullptr);
}

// Deliberately backed by the system clock rather than by a variable of our
// own. settimeofday puts it in the RTC domain, which keeps counting across
// a software reset -- so a watchdog reboot, a panic, or the SD-card boot
// loop does not take the time with it. A private static would.
bool isSet() {
    return rawNow() > kPlausible;
}
bool trusted() { return isSet() && !s_guess; }
bool guessed() { return isSet() && s_guess; }

uint32_t nowEpoch() {
    const uint32_t t = rawNow();
    return (t > kPlausible) ? t : 0u;
}

// The first time this board knows the date, that date is kept: it is the
// day Squachy counts from.
static void noteKnown() {
    if (!s_begun || s_born) return;
    s_born = nowEpoch();
    if (s_born) s_prefs.putUInt("born", s_born);
}

static void writeSystemClock(uint32_t epoch) {
#if defined(ARDUINO_ARCH_ESP32)
    struct timeval tv;
    tv.tv_sec  = (time_t)epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
#else
    // The emulator keeps its own epoch (see rawNow): the host's clock is not
    // ours to set, and the browser build has no settimeofday to link at all.
    simInit();
    s_simEpoch = epoch; s_simMs0 = millis();
#endif
}

bool setEpoch(uint32_t epoch) {
    if (epoch <= kPlausible) return false;
    // A real answer, from wherever: the guess is over, and the note is
    // brought up to date at once rather than ten minutes from now.
    s_guess = false;
    writeSystemClock(epoch);
    noteKnown();
    if (s_begun) { s_prefs.putUInt("last", epoch); s_lastNote = millis(); }
    return true;
}

uint32_t uptimeSec() { return millis() / 1000u; }

void formatUptime(char* out, size_t n) {
    const uint32_t s = uptimeSec();
    const uint32_t d = s / 86400u;
    const uint32_t h = (s % 86400u) / 3600u;
    const uint32_t m = (s % 3600u) / 60u;
    const uint32_t sec = s % 60u;
    if (d) snprintf(out, n, "%lud %02lu:%02lu:%02lu",
                    (unsigned long)d, (unsigned long)h,
                    (unsigned long)m, (unsigned long)sec);
    else   snprintf(out, n, "%02lu:%02lu:%02lu",
                    (unsigned long)h, (unsigned long)m, (unsigned long)sec);
}

static bool localNow(struct tm& tmv, uint32_t epoch = 0) {
    if (!isSet()) return false;
    const time_t t = (time_t)(epoch ? epoch : nowEpoch());
    localtime_r(&t, &tmv);
    return true;
}

void formatClock(char* out, size_t n) {
    struct tm tmv;
    if (!localNow(tmv)) { snprintf(out, n, "not set"); return; }
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d%s",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, s_guess ? " at least" : "");
}

void formatStamp(uint32_t ms, char* out, size_t n) {
    if (trusted()) {
        // Wind the wall clock back by however long ago the stamp was
        // taken. The stamp itself is a millis() value, so the arithmetic
        // has to happen in uptime and only then convert.
        const uint32_t nowMs  = millis();
        const uint32_t agoSec = (nowMs > ms) ? (nowMs - ms) / 1000u : 0u;
        struct tm then, today;
        localNow(then, nowEpoch() - agoSec);
        localNow(today);
        if (then.tm_yday == today.tm_yday && then.tm_year == today.tm_year)
            snprintf(out, n, "%02d:%02d", then.tm_hour, then.tm_min);
        else
            // Another day: the date says more than the hour would.
            snprintf(out, n, "%d/%d", then.tm_mon + 1, then.tm_mday);
        return;
    }
    // Unchanged from before the clock existed: minutes and seconds since
    // boot. Not useful for telling the time, but it still orders events,
    // which is all it ever did.
    const uint32_t sec = ms / 1000u;
    snprintf(out, n, "%02lu:%02lu",
             (unsigned long)(sec / 60u), (unsigned long)(sec % 60u));
}

// ---- the calendar ------------------------------------------------------
uint8_t  hour()    { struct tm t; return localNow(t) ? (uint8_t)t.tm_hour : 0; }
uint8_t  minute()  { struct tm t; return localNow(t) ? (uint8_t)t.tm_min  : 0; }
uint8_t  weekday() { struct tm t; return localNow(t) ? (uint8_t)t.tm_wday : 0; }
bool     weekend() { const uint8_t d = weekday(); return trusted() && (d == 0 || d == 6); }
bool     night()   { const uint8_t h = hour(); return trusted() && (h >= 23 || h < 5); }

uint32_t localDay() {
    struct tm t;
    if (!localNow(t)) return 0;
    // Days since the epoch in LOCAL time: the UTC day number shifted by the
    // zone, so midnight here is where the count ticks.
    const time_t e = (time_t)nowEpoch();
    struct tm u; gmtime_r(&e, &u);
    int32_t day = (int32_t)(e / 86400);
    if (t.tm_yday != u.tm_yday || t.tm_year != u.tm_year) {
        // Local is on the other side of a UTC midnight, one way or the other.
        const bool ahead = (t.tm_year > u.tm_year) || (t.tm_year == u.tm_year && t.tm_yday > u.tm_yday);
        day += ahead ? 1 : -1;
    }
    return (uint32_t)day;
}

static const char* const DAY_NAMES[]   = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
static const char* const MONTH_NAMES[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

void formatDate(char* out, size_t n) {
    struct tm t;
    if (!localNow(t)) { snprintf(out, n, "NO DATE YET"); return; }
    snprintf(out, n, "%s %s %d", DAY_NAMES[t.tm_wday], MONTH_NAMES[t.tm_mon], t.tm_mday);
}

void formatTime(char* out, size_t n, bool twelveHour, bool* pm) {
    struct tm t;
    if (!localNow(t)) { snprintf(out, n, "--:--"); if (pm) *pm = false; return; }
    int h = t.tm_hour;
    if (pm) *pm = h >= 12;
    if (twelveHour) { h %= 12; if (h == 0) h = 12; }
    snprintf(out, n, twelveHour ? "%d:%02d" : "%02d:%02d", h, t.tm_min);
}

// ---- the zone ------------------------------------------------------------
// POSIX rules rather than fixed offsets so daylight saving takes care of
// itself. Names are what fits a settings row.
struct Zone { const char* name; const char* rule; };
static const Zone ZONES[] = {
    { "US EASTERN",  "EST5EDT,M3.2.0,M11.1.0" },
    { "US CENTRAL",  "CST6CDT,M3.2.0,M11.1.0" },
    { "US MOUNTAIN", "MST7MDT,M3.2.0,M11.1.0" },
    { "ARIZONA",     "MST7" },
    { "US PACIFIC",  "PST8PDT,M3.2.0,M11.1.0" },
    { "ALASKA",      "AKST9AKDT,M3.2.0,M11.1.0" },
    { "HAWAII",      "HST10" },
    { "ATLANTIC",    "AST4ADT,M3.2.0,M11.1.0" },
    { "NEWFOUNDLAND","NST3:30NDT,M3.2.0,M11.1.0" },
    { "BRAZIL",      "<-03>3" },
    { "UTC",         "UTC0" },
    { "UK",          "GMT0BST,M3.5.0/1,M10.5.0" },
    { "EU CENTRAL",  "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "EU EASTERN",  "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "MOSCOW",      "MSK-3" },
    { "INDIA",       "IST-5:30" },
    { "CHINA",       "CST-8" },
    { "JAPAN",       "JST-9" },
    { "AUS EASTERN", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "AUS WESTERN", "AWST-8" },
    { "NEW ZEALAND", "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
static const uint8_t ZONES_N = sizeof(ZONES) / sizeof(ZONES[0]);

uint8_t     zoneCount()        { return ZONES_N; }
const char* zoneName(uint8_t i){ return ZONES[i < ZONES_N ? i : 0].name; }
void applyZone(uint8_t i) {
    setenv("TZ", ZONES[i < ZONES_N ? i : 0].rule, 1);
    tzset();
}

// ---- network time ----------------------------------------------------------
// One NTP request, by hand, rather than LWIP's SNTP client. That client
// waits a random zero to five seconds before its first request (its
// SNTP_STARTUP_DELAY), which is longer than the whole boot check; measured
// as "no answer in 2500 ms" on the soak board. A single UDP packet gets
// an answer in the time a DNS lookup takes.
void syncStart() {}
void syncStop()  {}

#if defined(ARDUINO_ARCH_ESP32)
static bool ntpOnce(const char* host, uint32_t ms) {
    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) return false;
    WiFiUDP udp;
    if (!udp.begin(2390)) return false;
    uint8_t pkt[48] = {0};
    pkt[0] = 0x1B;                       // LI 0, version 3, mode 3 (client)
    udp.beginPacket(ip, 123);
    udp.write(pkt, sizeof pkt);
    udp.endPacket();
    const uint32_t t0 = millis();
    bool got = false;
    while (millis() - t0 < ms) {
        if (udp.parsePacket() >= 48) {
            udp.read(pkt, sizeof pkt);
            // Transmit timestamp, seconds since 1900.
            const uint32_t secs1900 = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                                      ((uint32_t)pkt[42] << 8)  |  (uint32_t)pkt[43];
            const uint32_t epoch = secs1900 - 2208988800UL;
            got = setEpoch(epoch);
            break;
        }
        delay(10);
    }
    udp.stop();
    return got;
}
#endif

bool syncWait(uint32_t ms) {
#if defined(ARDUINO_ARCH_ESP32)
    const uint32_t t0 = millis();
    static const char* const HOSTS[] = { "pool.ntp.org", "time.google.com" };
    for (const char* h : HOSTS) {
        const uint32_t used = millis() - t0;
        if (used >= ms) break;
        if (ntpOnce(h, (ms - used) / 2 < 400 ? ms - used : (ms - used) / 2)) { s_synced = true; break; }
    }
    if (isSet()) noteKnown();
    return isSet();
#else
    (void)ms;
    return isSet();
#endif
}

bool synced() { return s_synced; }

// ---- the board's own history ----------------------------------------------
uint32_t bornEpoch() { return s_born; }
uint32_t daysTogether() {
    const uint32_t now = nowEpoch();
    if (!s_born || !now || now < s_born) return 0;
    return (now - s_born) / 86400u;
}
uint32_t greetedDay()               { return s_greeted; }
void     setGreetedDay(uint32_t d)  { s_greeted = d; if (s_begun) s_prefs.putUInt("greet", d); }
uint32_t milestoneSaid()            { return s_mileSaid; }
void     setMilestoneSaid(uint32_t d){ s_mileSaid = d; if (s_begun) s_prefs.putUInt("mile", d); }

void begin() {
    if (s_begun) return;
    s_prefs.begin("clock", false);
    s_begun    = true;
    s_born     = s_prefs.getUInt("born", 0);
    s_greeted  = s_prefs.getUInt("greet", 0);
    s_mileSaid = s_prefs.getUInt("mile", 0);
    // Carried across a soft reset with the date already known: the first
    // such boot is still the first day.
    if (isSet()) { noteKnown(); return; }
    // A cold boot: the note to self, if there is one, as a floor.
    const uint32_t last = s_prefs.getUInt("last", 0);
    if (last > kPlausible) {
        writeSystemClock(last);
        s_guess = true;
        char buf[40];
        formatClock(buf, sizeof buf);
        Serial.printf("[clock] no clock; starting from the last note: %s\n", buf);
    }
}

void tick(uint32_t now) {
    // The note: only a real time is worth writing, and not too often --
    // flash has a life, and ten minutes of doubt is nothing next to a
    // night with the power off.
    if (!s_begun || !trusted()) return;
    if (now - s_lastNote < NOTE_EVERY_MS) return;
    s_lastNote = now;
    s_prefs.putUInt("last", nowEpoch());
}

void pollSerial() {
    // A line buffer rather than a parser. Anything that is not the one
    // command is answered and dropped -- this is a debug port, and silence
    // in response to a typo is worse than a line of help.
    static char line[48];
    static uint8_t len = 0;

    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c != '\n') {
            if (len < sizeof(line) - 1) line[len++] = (char)c;
            continue;   // keep reading; an over-long line is truncated, not split
        }
        line[len] = '\0';
        len = 0;
        if (line[0] == '\0') continue;
        // Locked means locked here too -- otherwise the lock screen is a door
        // with a USB cable propped against it.
        if (Security::locked()) {
            Serial.println("[security] locked -- unlock it on the screen first.");
            continue;
        }

        if (strncasecmp(line, "ZONE ", 5) == 0) {
            // ZONE US EASTERN, or ZONE 4: the flasher sends the name it
            // worked out from the browser's own zone.
            const char* arg = line + 5;
            while (*arg == ' ') arg++;
            int found = -1;
            if (*arg >= '0' && *arg <= '9') {
                const long i = strtol(arg, nullptr, 10);
                if (i >= 0 && i < (long)zoneCount()) found = (int)i;
            } else {
                for (uint8_t i = 0; i < zoneCount(); i++)
                    if (strcasecmp(arg, zoneName(i)) == 0) { found = i; break; }
            }
            if (found >= 0) {
                Settings::setTimeZone((uint8_t)found);
                char buf[32];
                formatClock(buf, sizeof buf);
                Serial.printf("[zone] %s (%s)\n", zoneName((uint8_t)found), buf);
            } else {
                Serial.print("[zone] unknown. One of:");
                for (uint8_t i = 0; i < zoneCount(); i++) Serial.printf(" %s,", zoneName(i));
                Serial.println();
            }
        } else if (strncasecmp(line, "TIME ", 5) == 0) {
            const uint32_t e = (uint32_t)strtoul(line + 5, nullptr, 10);
            if (setEpoch(e)) {
                char buf[32];
                formatClock(buf, sizeof(buf));
                Serial.printf("[clock] set to %s\n", buf);
            } else {
                Serial.printf("[clock] refused %lu -- expected seconds since "
                              "the epoch, e.g. TIME %lu\n",
                              (unsigned long)e, (unsigned long)kPlausible + 1u);
            }
#if CROWD_BENCH
        } else if (strncasecmp(line, "CROWD", 5) == 0) {
            CrowdBench::command(line + 5);
#endif
        } else {
            Serial.printf("[clock] unknown command. TIME <epoch seconds> sets the clock, "
                          "ZONE <name> the zone.\n");
        }
    }
}

}  // namespace Clock
