"""Renders the README's demo clip: boot, the main screen on synthwave with
Squachy as himself, a quip, a FLOCK detection and his reaction to it, and a
visiting SquachWatch in VOID EYE walking on to say hello.

    python3 make_readme_demo.py --render-only   # under WSL, after `make`
    python  make_readme_demo.py --encode-only   # wherever Pillow is installed

Drives squachsim-live, so every screen and transition is the firmware's own
main.cpp deciding what to draw; only the detection and the visitor are
injected, and the one tap is staged. Seeds its own settings directory, so
it does not depend on the GUI having been run: a board past first boot,
SquachMesh consented, the outfit chosen and earned.
"""
import json, os, shutil, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "readmedemo")
GIF  = os.path.join(HERE, "..", "docs", "demo.gif")
ZOOM = 2
MS   = 60
BG   = 10            # SYNTHWAVE
OUTFIT_VOIDEYE = 12  # OutfitId::VOIDEYE
RING_FRAMES = 6


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

    def close(self):
        self.send("Q")
        self.p.wait(timeout=5)


def seed(nvs):
    os.makedirs(nvs, exist_ok=True)
    open(os.path.join(nvs, "settings.nvs"), "w").write(
        "b colorchk 1\nb meshok 1\nb meshrx 1\nb meshtx 1\nb infoprimer 1\nu bg %d\n" % BG)
    open(os.path.join(nvs, "squachy.nvs"), "w").write("b onboarded 1\n")



def render():
    for b in ("squachsim-live",):
        if not os.path.exists(os.path.join(HERE, b)):
            sys.exit(b + " not built -- run `make` first")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    nvs = os.path.join(OUT, "nvs")
    seed(nvs)

    frames = []

    def add(w, h, buf, ms, ring=None):
        name = "%04d.png" % len(frames)
        png(os.path.join(OUT, name), w, h, buf)
        frames.append({"file": name, "ms": ms, "ring": ring})

    def cap(count, every=2, ms=MS):
        for _ in range(count):
            add(*live.step(every), ms)

    def hold(ms):
        frames[-1]["ms"] = ms

    def tap(x, y):
        for f in frames[-(RING_FRAMES - 1):]:
            f["ring"] = [x, y]
        live.send("D %d %d" % (x, y))
        add(*live.step(1), MS, [x, y])
        live.send("U")
        add(*live.step(1), MS)

    live = Live(nvs)
    # The splash, from the first frame.
    cap(26)
    # The main screen: synthwave, Squachy as himself, the counters. Then his thirty-
    # second line, fast-forwarded to.
    cap(30)
    live.step(430)
    cap(40)
    hold(900)
    # A Flock camera. The card, held; a tap to dismiss; what he makes of it.
    live.send("T FLOCK")
    cap(30)
    hold(1400)
    tap(160, 40)
    cap(50)
    hold(700)
    # Another SquachWatch in range: its owner's Squachy, in VOID EYE, walks on and says hello.
    live.send("P outfit %d" % OUTFIT_VOIDEYE, "P shade 2", "P name BIGFOOT", "P setup")
    cap(70)
    hold(1800)
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
