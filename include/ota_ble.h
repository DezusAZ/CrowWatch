// SquachWatch-CYD — firmware updates over Bluetooth: the transport only.
//
// A GATT service the website talks to from a browser (Web Bluetooth). The
// bytes go straight into OtaCore, which owns everything that makes an update
// safe -- see ota_core.h. What this file adds is getting them there:
//
// WHY IT CANNOT RUN ALL THE TIME. Update mode stops BLE scanning, WiFi capture
// and mesh advertising for its duration. The scanner's allocations fragment
// the heap, a busy scanner steals radio time, and NimBLE refuses to register a
// GATT server while a scan is running. So it is a mode entered from the menu.
//
// WHO MAY SEND. The board must be unlocked, and the browser has to send the
// six-digit code shown on the screen before a single byte is accepted -- which
// is what stops a stranger in range pushing an image at a board in update mode.
//
// THE PROTOCOL. One service, three characteristics, little-endian throughout.
//   INFO  (read)            env=<build>;ver=<version>;slot=<label>;max=<bytes>;proto=1
//   CTRL  (write, notify)   commands in, replies out -- see Cmd/Reply below
//   DATA  (write-no-resp)   <u32 offset><bytes>, sent in windows; after each
//                           window the browser sends SYNC and resumes from the
//                           offset in the ACK. A lost chunk costs one window.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ota_core.h"

namespace OtaBle {

// Browser -> board, first byte of a CTRL write.
enum Cmd : uint8_t {
    CMD_HELLO = 0x01,   // u32 pairing code
    CMD_BEGIN = 0x02,   // u32 image size, u8 sig length, sig bytes (DER)
    CMD_SYNC  = 0x03,   // "where are you?" -> REPLY_ACK
    CMD_END   = 0x04,   // every byte sent; verify and install
    CMD_ABORT = 0x05,
};

// Board -> browser, first byte of a CTRL notification.
enum Reply : uint8_t {
    REPLY_HELLO_OK = 0x81,
    REPLY_BAD_CODE = 0x82,   // u8 tries left
    REPLY_READY    = 0x83,   // u16 largest chunk payload the link carries
    REPLY_ACK      = 0x84,   // u32 next offset the board expects
    REPLY_DONE     = 0x85,   // verified and staged; restarting
    REPLY_FAIL     = 0x86,   // u8 OtaCore::Fail, then the words the screen shows
};

enum class State : uint8_t {
    OFF = 0,        // not in update mode; the radio belongs to detection
    WAITING,        // advertising, nobody connected yet
    CONNECTED,      // a browser is attached but has not started sending
    RECEIVING,      // bytes arriving
    VERIFYING,      // last byte in, checking the signature
    DONE,           // verified and staged; the board restarts shortly
    FAILED,         // see failureText(); nothing was changed
};

// True on builds with a Bluetooth server compiled in and a slot to update.
bool available();

// Enter and leave update mode. The caller pauses detection first (see
// DetectionEngine::startUpdateRadio) and resumes it after end(). begin()
// refuses while the device is locked.
bool begin();
void end();
void tick(uint32_t now);      // every loop; cheap while OFF

State       state();
bool        codeAccepted();   // CONNECTED, and the browser sent the right code
uint8_t     percent();
uint32_t    bytesReceived();
uint32_t    bytesExpected();
const char* failureText();
const char* deviceName();     // what the browser's picker lists, e.g. "SquachWatch-E5E6"
uint32_t    pairingCode();    // regenerated every time update mode is entered

}  // namespace OtaBle
