#!/usr/bin/env python3
"""Flash a board by port, but only the build that belongs to it.

    python tools/flash_known.py COM33            # reads the MAC, picks the env, flashes
    python tools/flash_known.py COM33 --check    # just says which board and env

Boards move between ports, and a build for the wrong panel leaves a white
or torn screen, or a board that never comes up. Every flash goes through
the MAC first: an unknown board is refused, and the env is never a guess.
Fill BOARDS in from the fleet as it grows; a new board is added here before
it is ever flashed, not after.
"""
import os, subprocess, sys

# MAC -> (env, what it is). Ports are not in this table on purpose.
BOARDS = {
    "d4:8a:fc:c8:e5:e4": ("cyd-fast",    "soak, 2.8in ST7789"),
    "8c:94:df:4e:84:10": ("cyd-fast",    "2.8in ST7789, the newer one"),
    "78:42:1c:94:e7:5c": ("rlphantom-r", "RL Phantom 2.4in, resistive"),
    "78:42:1c:8e:df:f8": ("awok",        "AWOK, touch on the display bus"),
    "88:57:21:2e:ba:34": ("cyd-ili9341", "2.8in ILI9341, no 80 MHz"),
    "8c:94:df:4e:ee:dc": ("cyd-ili9341", "2.8in ILI9341, no 80 MHz"),
    # The 3.5in. It was kept out of this table while it was on hold -- it draws
    # in two bands, and that path corrupted the heap and crashed it every 30 s.
    # Back in on 2026-09-20 to see what the faster drawing did for it; if it
    # goes back on hold, comment it out again rather than leaving a board here
    # that nobody means to flash.
    "a4:f0:0f:8e:3a:88": ("cyd35-fast", "3.5in, 80MHz, two-band drawing"),
    "a0:f2:62:e1:29:10": ("twatch-s3",  "LilyGo T-Watch S3, native USB (COM13 is allowed for THIS MAC only)"),
    # 88:57:21:2e:e6:e0 runs SquachEmit, not this firmware. It is the only
    # CAPACITIVE 2.8in; to test capacitive touch, list it here as cyd-fast
    # for the test and comment it out again after (done 2026-09-21).
    # d4:d4:da:88:62:b8 is something else entirely (HoloCube?): never
}
NEVER_PORTS = {"COM10"}
# COM13 was the wrong device once (2026-08-30) and is never flashed blind. The
# T-Watch S3 enumerates there as native USB with its MAC as the serial, so
# COM13 is allowed only when the MAC read back is the watch's (2026-09-22).
COM13_ONLY_FOR = "a0:f2:62:e1:29:10"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PENV = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts")
PY   = os.path.join(PENV, "python.exe")
PIO  = os.path.join(PENV, "pio.exe")


def read_mac(port):
    out = subprocess.run([PY, "-m", "esptool", "--port", port, "--baud", "115200", "read_mac"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("MAC: "):
            return line[5:].strip().lower()
    return None


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    port = sys.argv[1].upper()
    if port in NEVER_PORTS:
        sys.exit("refusing %s: that port is never flashed" % port)
    mac = read_mac(port)
    if not mac:
        sys.exit("no ESP32 answered on %s" % port)
    if port == "COM13" and mac != COM13_ONLY_FOR:
        sys.exit("refusing COM13: %s is not the watch (%s)" % (mac, COM13_ONLY_FOR))
    if mac not in BOARDS:
        sys.exit("unknown board %s on %s: add it to BOARDS first, or leave it alone" % (mac, port))
    env, what = BOARDS[mac]
    print("%s: %s -> %s (%s)" % (port, mac, env, what))
    if "--check" in sys.argv:
        return
    if env == "twatch-s3":
        # Native USB: PlatformIO's bundled esptool 4.5 loses the link when its
        # stub changes speed, and fails "Unable to verify flash chip" even
        # without. The system esptool 5 programs it fine at the nominal speed.
        # The bootloader lives at 0x0 on an S3, not 0x1000.
        r = subprocess.run([PIO, "run", "-e", env], cwd=ROOT)
        if r.returncode != 0:
            sys.exit("build failed")
        build = os.path.join(ROOT, ".pio", "build", env)
        boot_app0 = os.path.join(os.path.expanduser("~"), ".platformio", "packages",
                                 "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
        r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port, "--baud", "115200",
                            "--before", "default-reset", "--after", "hard-reset", "write-flash", "-z",
                            "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB",
                            "0x0", os.path.join(build, "bootloader.bin"),
                            "0x8000", os.path.join(build, "partitions.bin"),
                            "0xe000", boot_app0,
                            "0x10000", os.path.join(build, "firmware.bin")], cwd=ROOT)
    else:
        r = subprocess.run([PIO, "run", "-e", env, "-t", "upload", "--upload-port", port], cwd=ROOT)
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
