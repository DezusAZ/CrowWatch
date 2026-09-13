"""TrueType -> Adafruit GFX font header, the format TFT_eSPI's free fonts use.

    python tools/ttf2gfx.py <font.ttf> <pixel size> <CName> <out.h> [--static] [--wght N] [--wdth N] [--thresh N]

--static writes file-local arrays (include the header from exactly one .cpp,
which is how include/fonts/ is used: theme.cpp owns the bubble face). The
emulator builds the same header, PROGMEM being empty there. Glyphs 32..126, 1-bit, rendered by FreeType
through Pillow and thresholded.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

args = sys.argv[1:]
path, size, cname, out = args[0], int(args[1]), args[2], args[3]
sim = '--sim' in args
static = '--static' in args or sim
thresh = int(args[args.index('--thresh') + 1]) if '--thresh' in args else 110
font = ImageFont.truetype(path, size)
axes = {}
if '--wght' in args: axes['wght'] = float(args[args.index('--wght') + 1])
if '--wdth' in args: axes['wdth'] = float(args[args.index('--wdth') + 1])
if axes:
    try:
        names = [a['name'].decode() if isinstance(a['name'], bytes) else a['name'] for a in font.get_variation_axes()]
        tags = [n.lower() for n in names]
        cur = [a['default'] for a in font.get_variation_axes()]
        for k, v in axes.items():
            for i, n in enumerate(tags):
                if n.startswith(k[:3]) or (k == 'wght' and 'weight' in n) or (k == 'wdth' and 'width' in n):
                    cur[i] = v
        font.set_variation_by_axes(cur)
    except Exception as e:
        print('variation not applied:', e)

X0, Y0 = 32, 64
bitmaps = bytearray()
glyphs = []
ascent = font.getmetrics()[0]
for code in range(32, 127):
    ch = chr(code)
    img = Image.new('L', (size * 4 + 64, size * 4 + 96), 0)
    ImageDraw.Draw(img).text((X0, Y0), ch, font=font, fill=255, anchor='ls')
    adv = int(round(font.getlength(ch)))
    bw = img.point(lambda p: 255 if p >= thresh else 0)
    box = bw.getbbox()
    off = len(bitmaps)
    if box is None:
        glyphs.append((off, 0, 0, adv, 0, 0))
        continue
    l, t, r, b = box
    w, h = r - l, b - t
    bits = []
    for y in range(t, b):
        for x in range(l, r):
            bits.append(1 if bw.getpixel((x, y)) else 0)
    while len(bits) % 8: bits.append(0)
    for i in range(0, len(bits), 8):
        bitmaps.append(int(''.join(str(v) for v in bits[i:i + 8]), 2))
    glyphs.append((off, w, h, adv, l - X0, t - Y0))

asc = max(-g[5] for g in glyphs if g[1])
desc = max(g[2] + g[5] for g in glyphs if g[1])
y_adv = asc + desc + 2
q = '' if sim else ' PROGMEM'
st = 'static ' if static else ''
lines = ['// %s at %dpx, converted from %s by ttf2gfx.py (ascent %d, descent %d, %d bytes of glyphs)' % (cname, size, path.split('\\')[-1].split('/')[-1], asc, desc, len(bitmaps))]
lines.append('#pragma once')
if sim: lines.append('#include "gfxfont.h"')
lines.append('%sconst uint8_t %sBitmaps[]%s = {' % (st, cname, q))
for i in range(0, len(bitmaps), 12):
    lines.append('  ' + ', '.join('0x%02X' % b for b in bitmaps[i:i + 12]) + ',')
lines.append('};')
lines.append('%sconst GFXglyph %sGlyphs[]%s = {' % (st, cname, q))
for i, g in enumerate(glyphs):
    lines.append('  { %5d, %3d, %3d, %3d, %4d, %4d },   // 0x%02X %s' % (g + (32 + i, repr(chr(32 + i)))))
lines.append('};')
lines.append('%sconst GFXfont %s%s = { (uint8_t*)%sBitmaps, (GFXglyph*)%sGlyphs, 0x20, 0x7E, %d };' % (st, cname, q, cname, cname, y_adv))
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print('%s: ascent %d descent %d, %d glyph bytes' % (cname, asc, desc, len(bitmaps)))
