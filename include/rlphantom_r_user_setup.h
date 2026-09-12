// SquachWatch-CYD — TFT_eSPI config for the RL Phantom, RESISTIVE variant
// (Sunton ESP32-2432S024R, 2.4 inch, ILI9341).
//
// Identical to rlphantom_user_setup.h in every display respect -- same panel,
// same pins, same backlight -- with one addition: TOUCH_CS.
//
// WHY THAT ONE LINE IS THE WHOLE DIFFERENCE. This board's XPT2046 sits on the
// DISPLAY's SPI bus, not a dedicated one, exactly like AWOK's. Arming TOUCH_CS
// here is what switches TFT_eSPI's own touch path on, and main.cpp's
// TOUCH_ON_DISPLAY_BUS branches then drive touch through that path
// (getTouch/getTouchRaw/calibrateTouch) instead of the XPT2046_Touchscreen
// library on a separate HSPI peripheral.
//
// The standard path would look for the touch chip on GPIO 25/32/39. On this
// board those are the capacitive controller's reset, the I2C clock, and an
// unrelated input -- so a resistive Phantom finds nothing at all there. That
// is the bug this variant exists to rule out.
//
// WHICH ONE DO I HAVE? Nobody can tell from the outside, which is why both
// builds are offered. Flash one; if touch is dead, flash the other. The same
// arrangement the 2.8" board has for its two display drivers.
//   ESP32-2432S024C -- capacitive CST820 -> use rlphantom_user_setup.h
//   ESP32-2432S024R -- resistive XPT2046 -> this file
#pragma once

#define USER_SETUP_INFO    "SquachWatch-CYD / RL Phantom 2.4 inch resistive / ILI9341"
#define ILI9341_DRIVER

#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// Display, on VSPI. Identical to the 2.8" board and to the capacitive Phantom.
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // NO software reset — rely on power-on reset.

#define TFT_BL    27
#define TFT_BACKLIGHT_ON HIGH

// The line that matters: touch chip select, on the display's own bus. Taken
// from the vendor's factory User_Setup.h for the resistive demo.
#define TOUCH_CS  33

// 40MHz, spec-compliant. No overclock variant for this board -- see the
// capacitive header for why.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000
#define SPI_TOUCH_FREQUENCY    2500000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_RGB
