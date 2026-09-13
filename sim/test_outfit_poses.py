#!/usr/bin/env python3
"""Costume test: does every outfit keep its shape while his arms move?

    python3 test_outfit_poses.py --render-only   # under WSL, after `make`
    python  test_outfit_poses.py --check-only    # wherever numpy + Pillow are

Renders every outfit through every arm movement he has -- IDLE, WAVE and each
VisitPose, at eight moments 125 ms apart -- on a flat key colour, using the
firmware's own drawWaving() (the `poses` screen in main_sim.cpp).

Then, for each frame, it compares the costume against the same frame with no
costume. Pixels the costume adds outside his bare silhouette are "extra". A
hat or a pelt always adds some; what it should not do is add a lot MORE when
his arms move than when he stands still -- that is a piece left behind where
the arm used to be, or a sleeve that does not follow the arm. So each outfit's
score in each pose is its extra-pixel count divided by its own resting count,
and anything well above 1 is flagged.

Output (sim/out/poses/):
    report.txt           every flagged frame, worst first
    sheet_<outfit>.png   the outfit beside no-outfit, every pose, two moments
    worst.png            the twelve worst frames, zoomed, bare vs costume
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "poses")
SRC  = os.path.join(HERE, "..", "src", "squachy.cpp")

POSES = ["IDLE", "WAVE", "HIGH_FIVE", "LOW_FIVE", "FIST", "STARTLED", "DANCE", "PUMP",
         "SLEEPY", "STRETCH", "LAUGH", "SALUTE", "BOW", "HUG", "SAD", "GRR", "CROUCH",
         "PULL", "WIGGLE", "CHEER", "SELFIE", "HOWL", "POINT", "STRAIN", "COVER",
         "LOOK_AROUND", "HANDS_UP"]
PHASES = 8
W, H = 320, 240
FLAG_RATIO = 1.8      # extra pixels vs the outfit's own resting frames
FLAG_MIN   = 120      # ...and at least this many, so a 3-pixel hat wobble is not news


def outfit_names():
    txt = open(SRC, encoding="utf-8", errors="replace").read()
    start = txt.index("static const OutfitDef OUTFITS[]")
    body = txt[start:txt.index("};", start)]
    return [l.strip().split('"')[1] for l in body.splitlines() if l.strip().startswith('{ "')]


def render(names):
    os.makedirs(OUT, exist_ok=True)
    n = len(POSES) * PHASES
    for i, name in enumerate(names):
        raw = os.path.join(OUT, "o%02d.raw" % i)
        cmd = ["./squachsim", "poses", "--outfit", str(i), "--frames", "0",
               "--sequence", str(n), "--raw", raw]
        if subprocess.call(cmd, cwd=HERE, stdout=subprocess.DEVNULL) != 0:
            sys.exit("render failed for %s" % name)
        print("rendered %-12s %d frames" % (name, n))


def check(names):
    import numpy as np
    from PIL import Image, ImageDraw

    n = len(POSES) * PHASES
    frames = []
    for i in range(len(names)):
        a = np.fromfile(os.path.join(OUT, "o%02d.raw" % i), dtype=np.uint8)
        if a.size != n * W * H * 3:
            sys.exit("o%02d.raw is the wrong size -- render again" % i)
        frames.append(a.reshape(n, H, W, 3))
    key = frames[0][0, 0, 0].copy()
    masks = [np.any(f != key, axis=3) for f in frames]          # [outfit][frame] -> bool HxW

    def dilate(m, r):
        out = m.copy()
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                out |= np.roll(np.roll(m, dy, axis=-2), dx, axis=-1)
        return out

    bare = dilate(masks[0], 2)                                   # no-outfit silhouette, grown 2px
    rows, flagged = [], []
    for o in range(1, len(names)):
        extra = (masks[o] & ~bare).reshape(n, -1).sum(axis=1).reshape(len(POSES), PHASES)
        rest = max(1.0, extra[0].mean())                         # IDLE is the reference
        for p in range(len(POSES)):
            for k in range(PHASES):
                ratio = extra[p, k] / rest
                rows.append((ratio, extra[p, k], names[o], POSES[p], k))
                if p > 0 and ratio >= FLAG_RATIO and extra[p, k] >= FLAG_MIN:
                    flagged.append((ratio, extra[p, k], o, p, k))

    flagged.sort(reverse=True)
    with open(os.path.join(OUT, "report.txt"), "w") as f:
        f.write("Costume shape test: %d outfits x %d movements x %d moments\n" %
                (len(names) - 1, len(POSES), PHASES))
        f.write("Flagged when a costume adds >= %.1fx its resting extra pixels (and >= %d px)\n\n" %
                (FLAG_RATIO, FLAG_MIN))
        if not flagged:
            f.write("Nothing flagged.\n")
        by_outfit = {}
        for ratio, px, o, p, k in flagged:
            by_outfit.setdefault(names[o], []).append((ratio, px, POSES[p], k))
        for name, items in sorted(by_outfit.items(), key=lambda kv: -max(i[0] for i in kv[1])):
            poses = sorted({i[2] for i in items})
            f.write("%-12s %2d frames flagged, worst %.1fx  in: %s\n" %
                    (name, len(items), max(i[0] for i in items), ", ".join(poses)))
    print(open(os.path.join(OUT, "report.txt")).read())

    # His footprint at scale 2, centred at the bottom of the frame.
    crop = (90, 70, 230, 240)
    cw, ch = crop[2] - crop[0], crop[3] - crop[1]

    def tile(o, fi):
        return Image.fromarray(frames[o][fi]).crop(crop)

    # One sheet per outfit: rows of poses, columns = bare/costume at two moments.
    for o in range(1, len(names)):
        cols, pad, lab = 4, 4, 14
        sheet = Image.new("RGB", (cols * (cw + pad) + 90, len(POSES) * (ch + pad) + lab), (12, 10, 18))
        d = ImageDraw.Draw(sheet)
        d.text((92, 2), "no outfit      %s      no outfit      %s" % (names[o], names[o]), fill=(220, 210, 240))
        for p in range(len(POSES)):
            y = lab + p * (ch + pad)
            d.text((4, y + ch // 2), POSES[p].lower(), fill=(220, 210, 240))
            for c, (src, k) in enumerate([(0, 1), (o, 1), (0, 5), (o, 5)]):
                sheet.paste(tile(src, p * PHASES + k), (90 + c * (cw + pad), y))
                if src == o and any(f[2] == o and f[3] == p for f in flagged):
                    d.rectangle((90 + c * (cw + pad), y, 90 + c * (cw + pad) + cw - 1, y + ch - 1),
                                outline=(255, 60, 60), width=2)
        sheet.save(os.path.join(OUT, "sheet_%s.png" % names[o].lower().replace(" ", "_")))

    # The worst twelve, zoomed, bare beside costume.
    worst = flagged[:12]
    if worst:
        z, pad = 2, 6
        sheet = Image.new("RGB", (2 * cw * z + 3 * pad, len(worst) * (ch * z + 18 + pad)), (12, 10, 18))
        d = ImageDraw.Draw(sheet)
        for r, (ratio, px, o, p, k) in enumerate(worst):
            y = r * (ch * z + 18 + pad)
            d.text((pad, y + 2), "%s  %s  moment %d   %.1fx resting (%d px)" %
                   (names[o], POSES[p].lower(), k, ratio, px), fill=(255, 200, 120))
            sheet.paste(tile(0, p * PHASES + k).resize((cw * z, ch * z), Image.NEAREST), (pad, y + 18))
            sheet.paste(tile(o, p * PHASES + k).resize((cw * z, ch * z), Image.NEAREST), (2 * pad + cw * z, y + 18))
        sheet.save(os.path.join(OUT, "worst.png"))
    print("sheets -> %s" % OUT)


if __name__ == "__main__":
    names = outfit_names()
    if "--check-only" not in sys.argv:
        render(names)
    if "--render-only" not in sys.argv:
        check(names)
