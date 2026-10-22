#!/usr/bin/env python3
"""Draw Meta4msXXL's Instruments-browser tiles (270 x 110): art/tile.png and art/tile_fx.png.

    python3 tools/make_art.py

Black, the plugin name in Saira Stencil One (art/fonts, SIL OFL 1.1), and a row of the eight knob colours.
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FONT = os.path.join(ROOT, "art", "fonts", "SairaStencilOne-Regular.ttf")
COLOURS = ["#e5352b", "#f5843a", "#f2d11b", "#3fb04a", "#2f7de1", "#8a4fd8", "#e05aa0", "#e8e8e8"]


def tile(name, sub, out):
    s = 4
    im = Image.new("RGB", (270 * s, 110 * s), "#0e0e0e")
    d = ImageDraw.Draw(im)
    size = 46 * s
    f = ImageFont.truetype(FONT, size)
    while d.textlength(name, font=f) > 250 * s:
        size -= s
        f = ImageFont.truetype(FONT, size)
    d.text((135 * s, 44 * s), name, font=f, fill="#ffffff", anchor="mm")
    if sub:
        fs = ImageFont.truetype(FONT, 20 * s)
        d.text((135 * s, 76 * s), sub, font=fs, fill="#bdbdbd", anchor="mm")
    y = 96 * s
    for i, c in enumerate(COLOURS):
        cx = (135 - 3.5 * 22 + i * 22) * s
        d.ellipse((cx - 7 * s, y - 7 * s, cx + 7 * s, y + 7 * s), fill=c)
    im.resize((270, 110), Image.LANCZOS).save(out)


if __name__ == "__main__":
    tile("Meta4msXXL", "", os.path.join(ROOT, "art", "tile.png"))
    tile("Meta4msXXL", "FX", os.path.join(ROOT, "art", "tile_fx.png"))
    print("art/tile.png, art/tile_fx.png")
