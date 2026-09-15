#!/usr/bin/env python3
"""Renders the release clip for v1.9.0 -- the real clock and desk mode.

    python3 make_desk_demo.py --render-only   # under WSL, after `make`
    python  make_desk_demo.py --encode-only   # wherever Pillow is installed

Six scenes, each a run of the one-shot emulator with --sequence, so every
frame is the shipping code deciding what to draw: the desk at rest with
Squachy talking; a catch, as the small card; a squad message dropping out
from behind the clock with the sender's polaroid; the focus timer; the LOG
with real times; and the once-only time zone card on the main screen.
The clock is pinned to one moment (SQUACH_EPOCH) so the clip renders the
same on any day.
"""
import json, os, shutil, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "deskdemo")
GIF  = os.path.join(HERE, "..", "docs", "desk-mode.gif")

ZOOM  = 2
MS    = 66            # per captured frame: two 33 ms steps, about real time
EPOCH = "1789396740"  # Mon 14 Sep 2026, 14:39 UTC
BG    = "6"           # the fire scene, with the owl
MSG   = "MEET AT NORTH GATE AT SIX AND BRING THE CABLE OK"

W, H = 320, 240

# (screen, warm-up frames, captured frames, extra args, env, hold-last-ms)
SCENES = [
    ("desk",     240, 80, ["--bg", BG],                          {},                                   600),
    ("desk",       2, 55, ["--bg", BG],                          {"SQUACH_ALERT": "1"},                800),
    ("desk",       0, 90, ["--bg", BG, "--inboxtext", MSG],      {},                                  1400),
    ("desk",       4, 45, ["--bg", BG],                          {"SQUACH_TIMER": "1"},               1000),
    ("log",       30,  1, [],                                    {},                                  1800),
    ("zonecard",  30, 30, [],                                    {},                                  1600),
]


def render():
    if not os.path.exists(os.path.join(HERE, "squachsim")):
        sys.exit("build the emulator first: make -j8 squachsim")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    man = []
    for i, (screen, warm, n, extra, env, hold) in enumerate(SCENES):
        raw = os.path.join(OUT, "scene%d.raw" % i)
        cmd = [os.path.join(HERE, "squachsim"), screen, os.path.join(OUT, "x.png"),
               "--frames", str(warm), "--sequence", str(n), "--raw", raw] + extra
        e = dict(os.environ, SQUACH_EPOCH=EPOCH)
        e.update(env)
        if subprocess.call(cmd, cwd=HERE, env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
            sys.exit("render failed: " + " ".join(cmd))
        size = os.path.getsize(raw)
        if size != W * H * 3 * n:
            sys.exit("scene %d: %d bytes, expected %d" % (i, size, W * H * 3 * n))
        for k in range(n):
            man.append({"raw": os.path.basename(raw), "index": k, "ms": MS if k < n - 1 else MS + hold})
    json.dump(man, open(os.path.join(OUT, "manifest.json"), "w"))
    print("%d frames rendered into %s" % (len(man), OUT))


def encode():
    from PIL import Image
    mp = os.path.join(OUT, "manifest.json")
    if not os.path.exists(mp):
        sys.exit("no frames -- run --render-only under WSL first")
    man = json.load(open(mp))
    raws = {}
    ims = []
    for f in man:
        if f["raw"] not in raws:
            raws[f["raw"]] = open(os.path.join(OUT, f["raw"]), "rb").read()
        b = raws[f["raw"]]
        off = f["index"] * W * H * 3
        ims.append(Image.frombytes("RGB", (W, H), b[off:off + W * H * 3]))

    # One global palette: the firmware draws RGB332 at most, so this stays
    # lossless as long as the count fits.
    cols = set()
    for im in ims:
        cols |= set(im.getdata())
    cols = sorted(cols)
    if len(cols) > 256:
        # Fire has more shades than the rest; quantize to a shared palette
        # made from every frame at once, so nothing flickers between scenes.
        pal_img = Image.new("RGB", (W, H * len(ims)))
        for i, im in enumerate(ims):
            pal_img.paste(im, (0, H * i))
        ref = pal_img.quantize(colors=256, dither=Image.NONE)
    else:
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
            q = q.resize((W * ZOOM, H * ZOOM), Image.NEAREST)
        frames.append(q)
    os.makedirs(os.path.dirname(GIF), exist_ok=True)
    frames[0].save(GIF, save_all=True, append_images=frames[1:],
                   duration=[f["ms"] for f in man], loop=0, optimize=True, disposal=1)
    total = sum(f["ms"] for f in man) / 1000.0
    print("%d frames, %.1fs, %d colours, %dx%d -> %s (%d KB)"
          % (len(frames), total, len(cols), frames[0].width, frames[0].height,
             os.path.normpath(GIF), os.path.getsize(GIF) // 1024))


if __name__ == "__main__":
    if "--render-only" in sys.argv:   render()
    elif "--encode-only" in sys.argv: encode()
    else:                             render(); encode()
