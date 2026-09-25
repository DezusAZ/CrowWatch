# CrowWatch

> A surveillance-device detector for the 4" ESP32-4848S040 (480×480 touch panel), with a crow mascot.

CrowWatch listens on 2.4 GHz for the wireless signatures of Flock Safety
cameras, Axon body cameras, recording glasses, card skimmers, AirTags and
other trackers, drones, ALPRs, Ring/generic cameras, proximity beacons and
pentest hardware. It runs standalone on the board — no PC, no extras, just
USB power.

This is my own build for the **ESP32-4848S040C** (the 4-inch 480×480 ST7701
panel with GT911 capacitive touch). It is a fork of
[SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD) by
skizzophrenic — the original project, and the credit for the detection engine,
the concept and the UI belongs to them. My work here is the port to the 4"
display and the additions below.

## What's different in this version

- **New display layer** for the 480×480 ST7701 RGB panel and GT911 touch.
- **Crow mascot** and re-themed costumes in place of the original character.
- **Four-way screen rotation** on the square panel.
- **TX test page** (Settings → TX): broadcasts each detection signature so you
  can confirm another unit is detecting correctly.

## Flash it

Download `CrowWatch-4848.bin` from the latest
[Release](https://github.com/DezusAZ/CrowWatch/releases) and write it to the
board at offset `0x0`:

```
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 write_flash 0x0 CrowWatch-4848.bin
```

On Windows, the [ESP Flash Download Tool](https://www.espressif.com/en/support/download/other-tools)
or a web flasher work too — same file, same `0x0`.

## Build from source

```
pio run -e esp32-4848 -t upload
```

## Hardware

ESP32-4848S040C — 4" 480×480 ST7701 RGB panel, GT911 I2C touch, ESP32-S3.

**Where to buy:** [AliExpress](https://www.aliexpress.us/item/3256808028364930.html)
— Model **ESP32-4848S040C**, SKU **10100010**, 480×480. It should run about
**$20–25**; don't pay more than that.

## Author

DezusAZ
