#!/usr/bin/env python3
"""Replace the skin's knob filmstrips with Meta4MPCXXL's knob: the Davies 1900H seen from above.

    python3 tools/knob_art.py "<skin dir>/Plugin Skins"

gen_vst.py writes one filmstrip per knob radius (sh_knob_r<R>.png: 128 square frames of 2R+10 px, stacked down,
minimum first). layout.conf gives every Q-Link slot its own radius, and this redraws each strip in that slot's colour:
a round face with ten narrow notches cut into its edge (the 1900H's flutes as seen from above), a soft shadow below
it, a white pointer line (with a thin dark edge so it shows on the light knobs too), and the 11-tick scale around it,
on the panel's black. The pointer sweeps 300 degrees, the scale's range. Keep R <= 58 (a 59 strip is 16384 px tall,
which MPC draws misaligned).
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

FRAMES = 128
SS = 4                      # supersampling
SWEEP = math.radians(300)   # -150 .. +150 degrees
PANEL = (14, 14, 14)
TICK = (138, 138, 138)
NOTCHES = 10

# radius in layout.conf -> (face colour, rim/notch shade, face radius px)
KNOBS = {
    58: ("#e5352b", "#a8221a", 45),   # Q-Link 1 red
    57: ("#f5843a", "#b8561a", 45),   # 2 orange
    56: ("#f2d11b", "#ad940c", 45),   # 3 yellow
    55: ("#3fb04a", "#267a2f", 45),   # 4 green
    54: ("#2f7de1", "#1d5aa8", 45),   # 5 blue
    53: ("#8a4fd8", "#6236a3", 45),   # 6 purple
    52: ("#e05aa0", "#a83a73", 45),   # 7 pink
    51: ("#e8e8e8", "#a8a8a8", 45),   # 8 light grey
    50: ("#ffffff", "#bdbdbd", 44),   # SETUP page
}


def notched(cx, cy, r, rot):
    """The face outline: a circle with NOTCHES narrow, steep-sided cuts (a super-Gaussian profile)."""
    pts = []
    sp = 2 * math.pi / NOTCHES
    w, dep = 0.075, 0.085 * r
    for i in range(720):
        t = i / 720 * 2 * math.pi
        x = ((t - rot) % sp + sp) % sp
        if x > sp / 2:
            x -= sp
        q = x / w
        rr = r - dep * math.exp(-(q ** 6))
        pts.append((cx + math.sin(t) * rr, cy - math.cos(t) * rr))
    return pts


def frame(size, face, rim, fr, value):
    S = size * SS
    c = S / 2
    r = fr * SS
    ang = -SWEEP / 2 + SWEEP * value
    im = Image.new("RGB", (S, S), PANEL)
    d = ImageDraw.Draw(im)
    # scale: 11 ticks, the 1st, 6th and 11th longer
    for j in range(11):
        a = -SWEEP / 2 + SWEEP * j / 10
        r0 = (fr + 4) * SS
        r1 = (fr + (10 if j % 5 == 0 else 8)) * SS
        r1 = min(r1, c - SS)
        d.line((c + math.sin(a) * r0, c - math.cos(a) * r0, c + math.sin(a) * r1, c - math.cos(a) * r1),
               fill=TICK, width=int((3 if j % 5 == 0 else 2) * SS))
    # shadow: the face's outline, offset down-right, blurred
    sh = Image.new("L", (S, S), 0)
    ImageDraw.Draw(sh).polygon(notched(c + 3 * SS, c + 4 * SS, r, ang + math.pi / NOTCHES), fill=150)
    sh = sh.filter(ImageFilter.GaussianBlur(2.5 * SS))
    im = Image.composite(Image.new("RGB", (S, S), (0, 0, 0)), im, sh)
    d = ImageDraw.Draw(im)
    # face, with a darker rim
    d.polygon(notched(c, c, r, ang + math.pi / NOTCHES), fill=rim)
    d.polygon(notched(c, c, r - 2.5 * SS, ang + math.pi / NOTCHES), fill=face)
    # pointer: dark edge, then white
    s, k = math.sin(ang), -math.cos(ang)
    x0, y0 = c + s * 4 * SS, c + k * 4 * SS
    x1, y1 = c + s * (r - 5 * SS), c + k * (r - 5 * SS)
    d.line((x0, y0, x1, y1), fill=(17, 17, 17), width=int(9 * SS))
    for (px, py) in ((x0, y0), (x1, y1)):
        d.ellipse((px - 4.5 * SS, py - 4.5 * SS, px + 4.5 * SS, py + 4.5 * SS), fill=(17, 17, 17))
    d.line((x0, y0, x1, y1), fill=(255, 255, 255), width=int(6 * SS))
    for (px, py) in ((x0, y0), (x1, y1)):
        d.ellipse((px - 3 * SS, py - 3 * SS, px + 3 * SS, py + 3 * SS), fill=(255, 255, 255))
    return im.resize((size, size), Image.LANCZOS)


def build(path, radius):
    face, rim, fr = KNOBS[radius]
    old = Image.open(path)
    size = old.width
    if old.height != size * FRAMES:
        sys.exit("%s: expected %d frames of %d px" % (path, FRAMES, size))
    strip = Image.new("RGB", (size, size * FRAMES), PANEL)
    for i in range(FRAMES):
        strip.paste(frame(size, face, rim, fr, i / (FRAMES - 1)), (0, i * size))
    strip.save(path)


def main():
    skin = sys.argv[1]
    done = []
    for name in sorted(os.listdir(skin)):
        if name.startswith("sh_knob_r") and name.endswith(".png"):
            r = int(name[len("sh_knob_r"):-4])
            if r not in KNOBS:
                sys.exit("knob_art: no knob for radius %d (layout.conf radii pick the colour)" % r)
            build(os.path.join(skin, name), r)
            done.append(r)
    print("knob_art: radii", done)


if __name__ == "__main__":
    main()
