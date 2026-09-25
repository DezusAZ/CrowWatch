// SquachWatch-CYD — GT911 raw touch for the ESP32-4848S040.
// I2C core taken from the working display firmware; positioning goes
// through the app's own TouchFit calibration, so no on-chip config or
// saved-coordinate handling lives here — begin() and rawRead() only.
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace Gt911 {

static const uint16_t ADDR1 = 0x5D, ADDR2 = 0x14;
static uint8_t s_addr = 0;
static int16_t s_maxX = 480, s_maxY = 480;

static uint8_t readBuf(uint16_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(s_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission() != 0) return 0;
    uint8_t got = Wire.requestFrom(s_addr, len);
    uint8_t i = 0;
    while (Wire.available() && i < len) buf[i++] = Wire.read();
    return i;
}

static uint8_t read(uint16_t reg) {
    uint8_t v = 0;
    readBuf(reg, &v, 1);
    return v;
}

static void write(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(s_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(val);
    Wire.endTransmission();
}

static bool begin(uint8_t sda, uint8_t scl) {
    Wire.begin(sda, scl, 400000);
    delay(50);
    Wire.beginTransmission(ADDR1);
    if (Wire.endTransmission() == 0) s_addr = ADDR1;
    else {
        Wire.beginTransmission(ADDR2);
        if (Wire.endTransmission() != 0) return false;
        s_addr = ADDR2;
    }
    const int mx = (read(0x8049) << 8) | read(0x8048);
    const int my = (read(0x804B) << 8) | read(0x804A);
    if (mx > 0 && mx <= 1024) s_maxX = mx;
    if (my > 0 && my <= 1024) s_maxY = my;
    return true;
}

// True only while a finger is down; x/y are raw chip coordinates
// rescaled to the 480x480 panel. The app's TouchFit maps from here.
static bool rawRead(int16_t& x, int16_t& y) {
    if (!s_addr) return false;
    static bool contact = false;
    static uint8_t misses = 0;
    static int16_t lastX = 0, lastY = 0;
    uint8_t status = 0;
    if (readBuf(0x814E, &status, 1) == 0) {
        // The chip NAKs while idle, and can go idle before the release
        // report is polled: a few unanswered reads in a row IS a release.
        if (contact && ++misses >= 3) { contact = false; misses = 0; }
    } else {
        misses = 0;
        if (status & 0x80) {
            if (status & 0x0F) {
                uint8_t buf[8] = {};
                if (readBuf(0x8150, buf, sizeof(buf)) >= 4) {
                    const int px = (buf[1] << 8) | buf[0];
                    const int py = (buf[3] << 8) | buf[2];
                    lastX = constrain((int32_t)px * 479 / max(1, s_maxX - 1), 0, 479);
                    lastY = constrain((int32_t)py * 479 / max(1, s_maxY - 1), 0, 479);
                    contact = true;
                }
            } else {
                contact = false;
            }
            write(0x814E, 0);
        }
    }
    x = lastX;
    y = lastY;
    return contact;
}

}  // namespace Gt911
