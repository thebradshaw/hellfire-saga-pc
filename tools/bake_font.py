#!/usr/bin/env python3
"""Bakes a TTF into a glyph atlas PNG + a plain-text metrics file.

The game draws text from these at runtime with no font library.

    python3 tools/bake_font.py <font.ttf> <pixel_size> <out_name> [variation]

[variation] picks a named style of a variable font, e.g. "SemiBold".

Writes assets/fonts/<out_name>.png (white glyphs, alpha = coverage) and
assets/fonts/<out_name>.txt (one line per glyph: code x y w h xoff yoff advance,
plus a first line: "font <line_height> <ascent>").
"""
import sys
from PIL import Image, ImageDraw, ImageFont

path, size, name = sys.argv[1], int(sys.argv[2]), sys.argv[3]
font = ImageFont.truetype(path, size)
if len(sys.argv) > 4:
    font.set_variation_by_name(sys.argv[4])
ascent, descent = font.getmetrics()
pad = 2
glyphs, x, y, row_h, atlas_w = [], pad, pad, 0, 512
for code in range(32, 127):
    ch = chr(code)
    l, t, r, b = font.getbbox(ch)
    w, h = max(r - l, 0), max(b - t, 0)
    adv = round(font.getlength(ch))
    if x + w + pad > atlas_w:
        x, y, row_h = pad, y + row_h + pad, 0
    glyphs.append((code, x, y, w, h, l, t, adv))
    x += w + pad
    row_h = max(row_h, h)
atlas_h = y + row_h + pad
img = Image.new("L", (atlas_w, atlas_h), 0)
draw = ImageDraw.Draw(img)
for code, gx, gy, w, h, l, t, adv in glyphs:
    if w and h:
        draw.text((gx - l, gy - t), chr(code), font=font, fill=255)
rgba = Image.merge("RGBA", (Image.new("L", img.size, 255),) * 3 + (img,))
rgba.save(f"assets/fonts/{name}.png", optimize=True)
with open(f"assets/fonts/{name}.txt", "w") as f:
    f.write(f"font {ascent + descent} {ascent}\n")
    for g in glyphs:
        f.write(" ".join(str(v) for v in g) + "\n")
print(name, img.size)
