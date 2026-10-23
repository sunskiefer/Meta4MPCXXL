#!/usr/bin/env python3
"""Finish the built skin: the plugin's name in Saira Stencil One, and every baked word in Titillium Web, large.

    python3 tools/post_skin.py "<skin dir>/Plugin Skins" "<plugin name>"

MPC draws live text only in Titillium Web or Roboto (mpc-vst-plugins docs/NOTES.md "Skin fonts"); shadow_art bakes
static text in a small bitmap font. So this script draws, into the skin's images:
  - the plugin name, top left of every page (sh_bg_<n>.png), in Saira Stencil One (only the name uses it);
  - the SETUP page's group names and help lines (TEXTS below), in Titillium Web Bold;
  - the option captions of every option row (sh_seg_<key>_<n>_<on|off>.png) and every button caption
    (sh_btn_<key>_<LABEL>_<on|off>.png), in Titillium Web Bold, as large as the box allows.
Both fonts are SIL OFL 1.1 (art/fonts/OFL-*.txt).
"""
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
STENCIL = os.path.join(ROOT, "art", "fonts", "SairaStencilOne-Regular.ttf")
SANS = os.path.join(ROOT, "art", "fonts", "TitilliumWeb-Bold.ttf")
NAME_BOX = (20, 8, 450, 90)   # skin pixels: left of the patch stepper (layout.conf: stepper cx=770 w=600)
INK = DIM = (255, 255, 255)   # all text pure white: readable on the Force screen
SS = 4

# page index -> [(x, y, anchor, size, colour, text)], skin pixels (layout.conf y - 86)
TEXTS = {
    1: [
        (640, 136, "mm", 34, DIM, "CV OUT"),
        (250, 360, "mm", 34, DIM, "WI-FI RECEIVE"),
        (640, 252, "mm", 34, DIM, "MIDI TO HUB INS"),
        (560, 536, "lm", 29, DIM, "Patches: a Meta4MPCXXL folder on internal"),
        (560, 572, "lm", 29, DIM, "storage, an SD card or USB, then RESCAN."),
        (560, 608, "lm", 29, DIM, "CV Out: patch outs 3-8 on the Meta4MPCXXL port."),
    ],
}


def font(path, px):
    return ImageFont.truetype(path, int(px * SS))


def draw_text(im, x, y, anchor, size, colour, text, path=SANS):
    """Text drawn 4x larger on a transparent layer, scaled down and composited (smooth edges). Shrinks to stay
    inside the page (a left-anchored line may not pass the right edge less 12 px)."""
    d0 = ImageDraw.Draw(im)
    room = (im.width - 12 - x) if anchor == "lm" else min(x, im.width - x) * 2 - 24
    while size > 14 and d0.textlength(text, font=font(path, size)) / SS > room:
        size -= 1
    f = font(path, size)
    w = int(d0.textlength(text, font=f) / SS) + 8
    h = int(size * 1.6) + 8
    layer = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((w * SS // 2, h * SS // 2), text, font=f, fill=colour + (255,), anchor="mm")
    layer = layer.resize((w, h), Image.LANCZOS)
    if anchor == "lm":
        ox = x
    else:
        ox = x - w // 2
    im.alpha_composite(layer, (int(ox - (4 if anchor == "lm" else 0)), int(y - h // 2)))


def name_on(im, name):
    x0, y0, x1, y1 = NAME_BOX
    size = 60
    d = ImageDraw.Draw(im)
    while d.textlength(name, font=font(STENCIL, size)) / SS > (x1 - x0) and size > 20:
        size -= 1
    layer = Image.new("RGBA", ((x1 - x0) * SS, (y1 - y0) * SS), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((0, (y1 - y0) * SS // 2), name, font=font(STENCIL, size), fill=INK + (255,), anchor="lm")
    im.alpha_composite(layer.resize((x1 - x0, y1 - y0), Image.LANCZOS), (x0, y0))


def caption(path, text, fill, ink):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    plate = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(plate)
    d.rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), radius=6 * SS, fill=fill + (255,))
    size = min(h * 0.58, 38)
    while d.textlength(text, font=font(SANS, size)) > (w - 16) * SS and size > 12:
        size -= 1
    d.text((w * SS // 2, h * SS // 2), text, font=font(SANS, size), fill=ink + (255,), anchor="mm")
    base = Image.new("RGBA", (w, h), im.getpixel((0, 0))[:3] + (255,))
    base.alpha_composite(plate.resize((w, h), Image.LANCZOS))
    base.convert("RGB").save(path)


def main():
    skin, name = sys.argv[1], sys.argv[2]
    params = {p["key"]: p for p in json.load(open(os.path.join(ROOT, "params.json")))["params"]}
    files = sorted(os.listdir(skin))

    pages = [f for f in files if re.fullmatch(r"sh_bg_\d+\.png", f)]
    if not pages:
        sys.exit("post_skin: no page backgrounds in %s" % skin)
    for f in pages:
        n = int(f[6:-4])
        p = os.path.join(skin, f)
        im = Image.open(p).convert("RGBA")
        name_on(im, name)
        for (x, y, anchor, size, colour, text) in TEXTS.get(n, []):
            draw_text(im, x, y, anchor, size, colour, text)
        im.convert("RGB").save(p)

    segs = btns = 0
    for f in files:
        m = re.fullmatch(r"sh_seg_(.+)_(\d+)_(on|off)\.png", f)
        if m and m.group(1) in params:
            opts = params[m.group(1)].get("options") or []
            i = int(m.group(2))
            if i < len(opts):
                on = m.group(3) == "on"
                caption(os.path.join(skin, f), opts[i].upper(), (255, 255, 255) if on else (28, 28, 28),
                        (17, 17, 17) if on else INK)
                segs += 1
            continue
        m = re.fullmatch(r"sh_btn_(.+?)_([A-Z0-9_]+)_(on|off)\.png", f)
        if m:
            p = os.path.join(skin, f)
            im = Image.open(p).convert("RGB")
            fill = im.getpixel((im.width // 2, 4))
            if m.group(3) == "on":
                fill = tuple(min(255, c + 50) for c in fill)
            caption(p, m.group(2).replace("_", " "), fill, INK)
            btns += 1
    print("post_skin: name on %s; %d option and %d button captions" % (", ".join(pages), segs, btns))


if __name__ == "__main__":
    main()
