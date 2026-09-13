#!/usr/bin/env python3
"""Renders the release clip for over-the-air updates -- from the UPDATE
FIRMWARE screen, over WiFi, to UPDATE INSTALLED.

    python3 make_ota_demo.py --render-only   # under WSL, after `make live`
    python3 make_ota_demo.py --encode-only   # wherever Pillow is installed

Same machinery as make_menu_demo.py: squachsim-live is the firmware's real
main.cpp, ui_update.cpp and ui_wifipass.cpp, driven by a script of taps, each
one ringed. The radio is sim/ota_sim.cpp's scripted transfer, so the network
names are made up and no real password is ever typed.

Coordinates are landscape, 320x240, taken from the layout code rather than
nudged until they looked right:
  ui_update.cpp   BTN_H 28, back at h-34, buttons stacked 36 apart above it;
                  WiFi list rows from y 36, 24 tall plus a 3px gap.
  ui_wifipass.cpp five key rows from y 60, 32 tall, 4 apart; ten keys 29 wide
                  from x 6, row two from x 21, SHIFT at x 4-50, OK at x 252.
"""
import json, os, shutil, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "otademo")
GIF  = os.path.join(HERE, "..", "docs", "wifi-update.gif")

ZOOM = 2          # integer only: nearest-neighbour keeps device pixels square
MS   = 60         # per captured frame
BG   = 8          # the background the other release clips used
RING_FRAMES = 6

GEAR       = (12, 8)
SYSTEM_ROW = (100, 104)    # on the main list, after two scroll drags
UPDATE_ROW = (100, 130)    # fourth row of the SYSTEM page
WIFI_BTN   = (160, 112)    # UPDATE OVER WIFI, with a version in the other slot
NETWORK    = (100, 75)     # second row: a secured network, so the keyboard opens
INSTALL    = (160, 184)

# The password keyboard. Types "Squ!7": capitals, a symbol, a digit.
SHIFT, K_S, K_Q, K_U = (27, 184), (66, 148), (20, 112), (206, 112)
SYM, K_BANG, K_7, OK = (34, 220), (20, 76), (206, 76), (282, 220)


def png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


class Live:
    """squachsim-live over its pipe: commands in, one RGB888 frame per S."""
    def __init__(self, nvs):
        self.p = subprocess.Popen([os.path.join(HERE, "squachsim-live")], cwd=HERE,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL,
                                  env=dict(os.environ, SQUACHSIM_NVS=nvs))
        self.state = "?"

    def send(self, *cmds):
        self.p.stdin.write("".join(c + "\n" for c in cmds).encode())
        self.p.stdin.flush()

    def step(self, n):
        self.send("S %d" % n)
        hdr = self.p.stdout.readline().decode("ascii", "replace").rstrip("\n").split(None, 5)
        if len(hdr) < 5 or hdr[0] != "FRM":
            sys.exit("bad frame header: %r" % hdr)
        w, h, nb = int(hdr[1]), int(hdr[2]), int(hdr[3])
        self.state = hdr[4]
        buf = b""
        while len(buf) < nb:
            chunk = self.p.stdout.read(nb - len(buf))
            if not chunk:
                sys.exit("emulator exited mid-frame")
            buf += chunk
        return w, h, buf

    def drag(self, x1, y1, x2, y2):
        self.send("D %d %d" % (x1, y1)); self.step(1)
        for i in range(1, 7):
            self.send("M %d %d" % (x1 + (x2 - x1) * i // 6, y1 + (y2 - y1) * i // 6)); self.step(1)
        self.send("U"); self.step(2)

    def close(self):
        self.send("Q")
        self.p.wait(timeout=5)


def seed(name):
    nvs = os.path.join(OUT, name)
    shutil.copytree(os.path.join(HERE, ".nvs"), nvs)
    sp = os.path.join(nvs, "settings.nvs")
    lines = [l for l in open(sp).read().splitlines() if not l.startswith("u bg ")]
    open(sp, "w").write("\n".join(lines + ["u bg %d" % BG]) + "\n")
    return nvs


def render():
    if not os.path.exists(os.path.join(HERE, "squachsim-live")):
        sys.exit("squachsim-live not built -- run `make live` first")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)

    frames = []

    def add(w, h, buf, ms, ring=None):
        name = "%04d.png" % len(frames)
        png(os.path.join(OUT, name), w, h, buf)
        frames.append({"file": name, "ms": ms, "ring": ring})

    def cap(live, count, every=2, ms=MS):
        for _ in range(count):
            add(*live.step(every), ms)

    def tap(live, xy, expect=None):
        x, y = xy
        for f in frames[-(RING_FRAMES - 1):]:
            f["ring"] = [x, y]
        live.send("D %d %d" % (x, y))
        add(*live.step(1), MS, [x, y])
        live.send("U")
        add(*live.step(1), MS)
        if expect and live.state != expect:
            sys.exit("tap at %s left the emulator in %s, not %s" % (xy, live.state, expect))

    live = Live(seed("nvs_ota"))
    live.step(150)                                   # boot, off camera
    if live.state != "CLEAR":
        sys.exit("booted to %s, not CLEAR -- is sim/.nvs past first boot?" % live.state)
    # Off camera: Settings, scrolled to the bottom, into SYSTEM.
    live.send("D %d %d" % GEAR); live.step(1); live.send("U"); live.step(10)
    live.drag(160, 180, 160, 100); live.step(10)
    live.drag(160, 180, 160, 100); live.step(10)
    live.send("D %d %d" % SYSTEM_ROW); live.step(1); live.send("U"); live.step(10)

    cap(live, 10)                                    # the SYSTEM page, a beat
    tap(live, UPDATE_ROW, "UPDATE"); cap(live, 16)   # UPDATE FIRMWARE
    tap(live, WIFI_BTN);   cap(live, 30)             # looking, then the list
    cap(live, 8)
    tap(live, NETWORK, "WIFI_PASS"); cap(live, 8)    # the keyboard
    for key in (SHIFT, K_S, SHIFT, K_Q, K_U, SYM, K_BANG, SYM, K_7):
        tap(live, key); cap(live, 3)
    cap(live, 8)
    tap(live, OK, "UPDATE"); cap(live, 30)           # joining
    cap(live, 30)                                    # checking
    cap(live, 14)                                    # UPDATE AVAILABLE
    tap(live, INSTALL); cap(live, 42, every=6)       # eight seconds of download, faster
    cap(live, 12, every=4)                           # checking the signature
    cap(live, 26)                                    # UPDATE INSTALLED, and a beat on it

    live.close()
    json.dump(frames, open(os.path.join(OUT, "manifest.json"), "w"), indent=0)
    print("%d frames -> %s" % (len(frames), OUT))


def encode():
    from PIL import Image, ImageDraw
    mp = os.path.join(OUT, "manifest.json")
    if not os.path.exists(mp):
        sys.exit("no frames -- run --render-only under WSL first")
    man = json.load(open(mp))

    ims = []
    for f in man:
        im = Image.open(os.path.join(OUT, f["file"])).convert("RGB")
        if f["ring"]:
            x, y = f["ring"]
            d = ImageDraw.Draw(im)
            d.ellipse((x - 10, y - 10, x + 10, y + 10), outline=(255, 255, 255), width=2)
            d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(255, 255, 255))
        ims.append(im)

    cols = set()
    for im in ims:
        cols |= set(im.getdata())
    cols = sorted(cols)
    if len(cols) > 256:
        sys.exit("%d colours -- that is not an RGB332 frame buffer" % len(cols))
    pal = []
    for c in cols:
        pal += list(c)
    pal += [0, 0, 0] * (256 - len(cols))
    ref = Image.new("P", (1, 1))
    ref.putpalette(pal)

    frames = []
    for im in ims:
        q = im.quantize(palette=ref, dither=Image.NONE)
        if ZOOM > 1:
            q = q.resize((im.width * ZOOM, im.height * ZOOM), Image.NEAREST)
        frames.append(q)

    os.makedirs(os.path.dirname(GIF), exist_ok=True)
    frames[0].save(GIF, save_all=True, append_images=frames[1:],
                   duration=[f["ms"] for f in man], loop=0, optimize=True, disposal=1)
    total = sum(f["ms"] for f in man) / 1000.0
    print("%d frames, %.1fs, %d colours, %dx%d -> %s (%d KB)"
          % (len(frames), total, len(cols), frames[0].width, frames[0].height,
             os.path.normpath(GIF), os.path.getsize(GIF) // 1024))


if __name__ == "__main__":
    if "--encode-only" not in sys.argv:
        render()
    if "--render-only" not in sys.argv:
        encode()
