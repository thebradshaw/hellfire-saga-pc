#!/usr/bin/env python3
"""Builds the app icon from the logo's blackletter "H".

    python3 tools/make_icon.py

Writes assets/icon.png (256px, used for the window) and assets/icon.ico
(16-256px, built into the Windows .exe).
"""
from PIL import Image, ImageDraw, ImageFilter

logo = Image.open("assets/logo.png").convert("RGBA")
w, h = logo.size
# The H, without the ornament line that runs through the whole logo.
H = logo.crop((36, 0, 151, h))
alpha = H.split()[3]
bbox = alpha.point(lambda a: 255 if a > 40 else 0).getbbox()
H = H.crop(bbox)
# Fade out the last few columns where the next letter starts.
px = H.load()
for x in range(H.width - 6, H.width):
    k = (H.width - x) / 7
    for y in range(H.height):
        r, g, b, a = px[x, y]
        px[x, y] = (r, g, b, int(a * k))

S = 256
icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))
# Rounded dark-crimson tile with an ember glow at the bottom.
base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(base)
for i in range(S):
    t = i / (S - 1)
    c = (int(26 + 70 * t * t), int(6 + 8 * t), int(14 + 6 * t), 255)
    d.line([(0, i), (S, i)], fill=c)
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=48, fill=255)
icon.paste(base, (0, 0), mask)
# Border.
ImageDraw.Draw(icon).rounded_rectangle([3, 3, S - 4, S - 4], radius=46, outline=(255, 140, 40, 150), width=4)

# The H, scaled to fit, with a dark drop shadow and a soft glow.
scale = min(200 / H.height, 190 / H.width)
Hs = H.resize((int(H.width * scale), int(H.height * scale)), Image.LANCZOS)
ox, oy = (S - Hs.width) // 2, (S - Hs.height) // 2 + 4
glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
glow.paste(Image.new("RGBA", Hs.size, (255, 110, 30, 255)), (ox, oy), Hs.split()[3])
glow = glow.filter(ImageFilter.GaussianBlur(10))
glow.putalpha(glow.split()[3].point(lambda a: a * 120 // 255))
icon = Image.alpha_composite(icon, glow)
shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
shadow.paste(Image.new("RGBA", Hs.size, (10, 0, 4, 220)), (ox + 5, oy + 6), Hs.split()[3])
icon = Image.alpha_composite(icon, shadow)
layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
layer.paste(Hs, (ox, oy), Hs)
icon = Image.alpha_composite(icon, layer)
# Keep the rounded corners clean.
icon.putalpha(Image.composite(icon.split()[3], Image.new("L", (S, S), 0), mask))

icon.save("assets/icon.png", optimize=True)
icon.save("assets/icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("assets/icon.png, assets/icon.ico")
