// SquachWatch-CYD — the black box
//
// What the board saw and how it last went down, kept across restarts without
// an SD card. It lives in the 128 KB the partition table leaves unassigned
// between app1 and the core dump (0x3D0000-0x3F0000): no table change, so no
// board needs a USB reinstall to have it. See partitions_ota.csv.
//
// Two rings of 4 KB sectors, each sector a header and 63 records of 64 bytes:
//   sectors 0-1   boots: one record a boot, with the crash details when the
//                 boot was a crash -- about a hundred boots back
//   sectors 2-31  detections: one record a sighting, first sight or back
//                 after going quiet -- about 1,800 back
// A full ring erases its oldest sector. Nothing here is trusted until its
// header and checksum say it is ours: before v1.7.0 this space was SPIFFS,
// so an old board arrives with somebody else's bytes in it.
//
// Called from loop() and setup() only, never from the Bluetooth host task: a
// flash write stalls both cores, and a sector erase stalls them for tens of
// milliseconds.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "state.h"

namespace BlackBox {

// One sighting, as the flash keeps it. 64 bytes on flash, checksum included.
struct __attribute__((packed)) DetRecord {
    uint8_t  kind;          // internal: a sighting, or the mark a CLR leaves
    uint8_t  type;          // DetectionType
    uint8_t  conf;          // Confidence
    uint8_t  flags;         // DET_AGAIN
    uint8_t  mac[6];
    int8_t   rssi;
    uint8_t  channel;
    uint16_t hits;
    uint16_t boot;          // which boot saw it -- see bootNumber()
    uint32_t epoch;         // wall-clock seconds; 0 when the clock was not set
    uint32_t upSec;         // seconds since that boot
    char     vendor[16];
    char     name[20];
    uint8_t  pad[3];
    uint8_t  crc;
};
static const uint8_t DET_AGAIN = 0x01;   // came back after going quiet

// One boot. When it followed a crash, the crash is in here too, from the
// breadcrumb and the core dump summary (see crashReportInit() in main.cpp).
struct __attribute__((packed)) BootRecord {
    uint8_t  kind;
    uint8_t  reason;        // esp_reset_reason_t of THIS boot: how the last one ended
    uint8_t  screen;        // AppState the crashed boot was on
    uint8_t  flags;         // BOOT_CRUMB / BOOT_DUMP / BOOT_DUMP_OLDER
    uint16_t boot;
    uint16_t pad0;
    uint32_t epoch;         // when this boot wrote it; 0 when the clock was not set
    uint32_t upSec;         // how long the crashed boot had been up
    uint32_t heapFree;
    uint32_t heapBlock;
    uint32_t pc;            // where it died, from the core dump
    uint32_t cause;
    char     version[12];   // the firmware that wrote this record
    char     task[16];
    uint8_t  pad1[3];
    uint8_t  crc;
};
static const uint8_t BOOT_CRUMB      = 0x01;
static const uint8_t BOOT_DUMP       = 0x02;
static const uint8_t BOOT_DUMP_OLDER = 0x04;

// Finds the rings and counts what is in them. False when the space is not
// safe to use -- a partition table that has given it to something, or a
// flash chip too small -- and then every call below does nothing.
bool begin();
bool ready();

// The number this boot writes on its records, one past the last boot kept.
uint16_t bootNumber();
// Writes this boot's record; fills in the boot number, version and checksum.
void noteBoot(BootRecord& r);

void noteDetection(const Detection& d, bool again);
// LOG's CLR: a mark, not an erase. What came before it is not shown again,
// and the ring wears it away like anything else.
void markCleared();

// Newest first, stopping at the last CLR. Return false to stop early.
void forEachDetection(bool (*fn)(const DetRecord& r, void* ctx), void* ctx);
void forEachBoot(bool (*fn)(const BootRecord& r, void* ctx), void* ctx);

uint16_t detectionsKept();              // since the last CLR
uint8_t  crashesKept();
bool     lastCrash(BootRecord& out);    // the newest crash kept, if any
bool     isCrash(uint8_t reason);
const char* reasonName(uint8_t reason); // "PANIC", "TASK WDT", ...

// The security wipe: every sector, really erased.
void wipe();

// BLACKBOX on the console: both rings, as text.
void dump();

#ifdef BLACKBOX_TEST
// test/blackbox_test.cpp only: forget what is in RAM and scan again, as a
// restart would; the emulator's flash; where the next sighting will land.
void     testReopen();
uint8_t* testFlash();
bool     testNextDetectionSlot(uint32_t& offset);
#endif

}  // namespace BlackBox
