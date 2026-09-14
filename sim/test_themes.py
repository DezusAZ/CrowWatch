#!/usr/bin/env python3
"""Theme test: is every screen readable in every colour theme?

    python3 test_themes.py --render-only   # under WSL, after `make`
    python  test_themes.py --check-only    # wherever Pillow is

Two halves.

1. CONTRAST, measured. Every accent colour a screen writes text in, against
   the two surfaces text sits on (BG and TASKBAR), in each of the six themes.
   Measured AFTER the RGB332 conversion the frame buffer applies, because that
   is the colour the panel actually shows -- two colours that are distinct in
   the palette table can land on the same RGB332 value. Uses the WCAG contrast
   ratio: under 3:1 is flagged as hard to read, under 1.5:1 as close to
   invisible.

2. EVERY SCREEN, rendered in every theme by the firmware itself, laid out as
   sheets (one row per screen, one column per theme) for a person to look at.

Output (sim/out/themes/):
    contrast.txt          the measured table and everything flagged
    sheet_1.png, ...      screens x themes
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out", "themes")
THEME_SRC = os.path.join(HERE, "..", "src", "theme.cpp")

SCREENS = [
    ("clear", []), ("clear", ["--noseed"]), ("log", []), ("alert", []), ("watchalert", []),
    ("settings", []), ("settings", ["--scroll", "8"]), ("detfilter", []), ("power", []), ("light", []), ("security", []), ("ignorelist", []), ("nudge", []), ("squadupdate", []), ("invite", ["--pose", "1"]), ("invite", ["--pose", "2"]),
    ("diary", []), ("hunt", []), ("rawscan", []), ("diagnostics", []), ("colorcheck", []),
    ("boot", []), ("phone", []), ("phone", ["--qwerty"]), ("meshmenu", []), ("meshwarn", []),
    ("phrase", []), ("compose", []), ("update", []), ("wifipass", []),
]
FIELDS = ["bg", "taskbar", "purple", "cyan", "pink", "vaporPink", "vaporPurple",
          "vaporBlue", "vaporYellow", "green", "amber", "red"]
TEXT = ["purple", "cyan", "pink", "vaporPink", "vaporPurple", "vaporBlue",
        "vaporYellow", "green", "amber", "red"]


def palettes():
    txt = open(THEME_SRC, encoding="utf-8", errors="replace").read()
    body = txt[txt.index("const Palette kPalettes"):]
    body = body[:body.index("};")]
    out = []
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,([^}]*)\}', body):
        vals = [int(v.strip(), 16) for v in m.group(2).split(",") if v.strip()]
        out.append((m.group(1), dict(zip(FIELDS, vals))))
    return out


def rgb332(c565):
    """What an 8-bit TFT_eSprite stores for a 565 colour, expanded back to RGB888."""
    r3 = (c565 >> 13) & 0x7
    g3 = (c565 >> 8) & 0x7
    b2 = (c565 >> 3) & 0x3
    return (r3 * 255 // 7, g3 * 255 // 7, b2 * 255 // 3)


def luminance(rgb):
    def ch(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(v) for v in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def check_contrast(pals):
    lines, flags = [], []
    white = (255, 255, 255)
    for name, p in pals:
        bg, tb = rgb332(p["bg"]), rgb332(p["taskbar"])
        lines.append("%s   (BG %s, TASKBAR %s)" % (name, bg, tb))
        for field in ["white"] + TEXT:
            c = white if field == "white" else rgb332(p[field])
            on_bg, on_tb = contrast(c, bg), contrast(c, tb)
            mark = ""
            worst = min(on_bg, on_tb)
            if worst < 1.5:   mark = "  <-- NEARLY INVISIBLE"
            elif worst < 3.0: mark = "  <-- hard to read"
            if mark:
                flags.append("%-11s %-12s on %s: %.2f:1%s" %
                             (name, field, "BG" if on_bg <= on_tb else "TASKBAR", worst, mark))
            lines.append("    %-12s %-15s on BG %5.2f:1   on TASKBAR %5.2f:1%s" %
                         (field, c, on_bg, on_tb, mark))
        # A selected button or tab is filled with PURPLE and keeps its white
        # label -- the LOG button on the log screen, EMOTE and the active tab
        # on the message screen. A theme with a pale PURPLE makes that label
        # vanish, and nothing above measures it because the label is not
        # sitting on BG at all.
        # The firmware picks the label colour with Theme::labelOn(): black on a
        # pale fill, white on a dark one. Same arithmetic here, on the raw 565.
        c565 = p["purple"]
        r5, g6, b5 = (c565 >> 11) & 0x1F, (c565 >> 5) & 0x3F, c565 & 0x1F
        luma = (r5 * 8 * 54 + g6 * 4 * 183 + b5 * 8 * 19) >> 8
        label = (0, 0, 0) if luma > 120 else white
        sel = contrast(label, rgb332(p["purple"]))
        sel_mark = "  <-- NEARLY INVISIBLE" if sel < 1.5 else ("  <-- hard to read" if sel < 3.0 else "")
        who = "black label" if luma > 120 else "white label"
        if sel_mark:
            flags.append("%-11s %-12s on selected fill (PURPLE): %.2f:1%s" % (name, who, sel, sel_mark))
        lines.append("    %-12s on selected fill (PURPLE) %5.2f:1%s" % (who, sel, sel_mark))
        # Colours that stop being different once the buffer quantises them.
        seen = {}
        for field in TEXT:
            seen.setdefault(rgb332(p[field]), []).append(field)
        for c, fields in seen.items():
            if len(fields) > 1:
                lines.append("    same on screen: %s" % ", ".join(fields))
        lines.append("")
    report = "Theme contrast (WCAG ratio, after RGB332)\n\n"
    report += ("Flagged:\n  " + "\n  ".join(flags) + "\n\n") if flags else "Nothing flagged.\n\n"
    report += "\n".join(lines)
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, "contrast.txt"), "w").write(report)
    print(report.split("\n\n")[0] + "\n" + (report.split("\n\n")[1] if flags else ""))


def render(npal):
    os.makedirs(OUT, exist_ok=True)
    for s, (screen, extra) in enumerate(SCREENS):
        for t in range(npal):
            png = os.path.join(OUT, "s%02d_t%d.png" % (s, t))
            cmd = ["./squachsim", screen, png, "--theme", str(t)] + extra
            if subprocess.call(cmd, cwd=HERE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
                print("FAILED: %s" % " ".join(cmd))
        print("rendered %-12s %s" % (screen, " ".join(extra)))


def sheets(pals):
    from PIL import Image, ImageDraw
    per_sheet = 8
    tw, th, pad, lab = 320, 240, 6, 16
    for sheet_i in range((len(SCREENS) + per_sheet - 1) // per_sheet):
        chunk = list(enumerate(SCREENS))[sheet_i * per_sheet:(sheet_i + 1) * per_sheet]
        W = 110 + len(pals) * (tw + pad)
        H = lab + len(chunk) * (th + pad)
        im = Image.new("RGB", (W, H), (12, 10, 18))
        d = ImageDraw.Draw(im)
        for t, (name, _) in enumerate(pals):
            d.text((110 + t * (tw + pad) + 4, 2), name, fill=(230, 220, 250))
        for r, (s, (screen, extra)) in enumerate(chunk):
            y = lab + r * (th + pad)
            d.text((4, y + th // 2 - 6), screen + ("\n" + " ".join(extra) if extra else ""), fill=(230, 220, 250))
            for t in range(len(pals)):
                p = os.path.join(OUT, "s%02d_t%d.png" % (s, t))
                if os.path.exists(p):
                    im.paste(Image.open(p).convert("RGB"), (110 + t * (tw + pad), y))
        path = os.path.join(OUT, "sheet_%d.png" % (sheet_i + 1))
        im.save(path, optimize=True)
        print("sheet -> %s" % path)


if __name__ == "__main__":
    pals = palettes()
    if "--check-only" not in sys.argv:
        render(len(pals))
    if "--render-only" not in sys.argv:
        check_contrast(pals)
        sheets(pals)
