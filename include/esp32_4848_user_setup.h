// SquachWatch-CYD — ESP32-4848S040 board header
// 4" 480x480 ST7701-driven RGB565 panel, GT911 capacitive touch on I2C.
// Not a TFT_eSPI panel: the display layer for this board is Arduino_GFX
// behind the TFT_eSPI-shaped shim in gfx_shim.h (see main.cpp), so the
// SPI pin/driver defines TFT_eSPI headers normally carry do not apply.
#pragma once

#define USER_SETUP_INFO "SquachWatch-CYD / 4 inch / ST7701 RGB"

// Square native panel, used at rotation 0 only.
#define TFT_WIDTH   480
#define TFT_HEIGHT  480

// Backlight on GPIO38, PWM'd by main.cpp's LEDC backlight code.
#define TFT_BL    38

// GT911 touch: I2C pins and the two possible addresses (the chip's
// address is strapped to one of them; probed at boot).
#define TOUCH_SDA 19
#define TOUCH_SCL 45
#define GT911_ADDR1 0x5D
#define GT911_ADDR2 0x14