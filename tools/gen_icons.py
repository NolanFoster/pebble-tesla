#!/usr/bin/env python3
"""Generate the action-bar icon PNGs for the Tesla Control watchapp.

Icons are white foreground on a transparent background, sized for Pebble's
ActionBarLayer column. Everything is drawn inside a SAFE content box that
leaves a margin on all sides so the action bar never clips the glyph, then
supersampled and downscaled once for smooth edges. Run from the repo root:

    python3 tools/gen_icons.py

This regenerates resources/images/*.png. Keeping the generator in-tree means
the icons are reproducible and easy to tweak without a binary asset editor.
"""

import math
import os

from PIL import Image, ImageDraw

ICON = 25                          # output footprint (px)
MARGIN = 3                         # keep glyphs this far from every edge
SS = 12                            # supersample factor for smooth curves
B = ICON * SS                      # big working-canvas size
WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "resources", "images")

# Safe drawing region in output px: [MARGIN, ICON - MARGIN].
LO, HI = MARGIN, ICON - MARGIN     # 3 .. 22
CX = CY = ICON / 2                 # center (12.5)
RMAX = (ICON / 2) - MARGIN         # max radius from center that stays in bounds (9.5)


def s(v):
    """Scale an output-px coordinate to the big working canvas."""
    return v * SS


def new_big():
    img = Image.new("RGBA", (B, B), CLEAR)
    return img, ImageDraw.Draw(img)


def finish(img):
    return img.resize((ICON, ICON), Image.LANCZOS)


def save(img, name):
    os.makedirs(OUT_DIR, exist_ok=True)
    img.save(os.path.join(OUT_DIR, name))


def padlock(locked):
    """Padlock: body + shackle. Shackle lifts/detaches on one side when unlocked."""
    img, d = new_big()
    # Body (kept within the safe box)
    d.rounded_rectangle((s(6), s(12), s(19), s(HI)), radius=s(2), fill=WHITE)
    # Keyhole punched out of the white body
    d.ellipse((s(10.5), s(14.5), s(14.5), s(18.5)), fill=CLEAR)
    d.polygon([(s(12), s(17)), (s(11), s(21)), (s(14), s(21)), (s(13), s(17))], fill=CLEAR)
    w = int(2.2 * SS)
    if locked:
        # Closed arch seated on the body, both legs down.
        d.arc((s(7.5), s(LO + 1), s(17.5), s(13)), start=180, end=360, fill=WHITE, width=w)
        d.line((s(7.5), s(9), s(7.5), s(12)), fill=WHITE, width=w)
        d.line((s(17.5), s(9), s(17.5), s(12)), fill=WHITE, width=w)
    else:
        # Open: shackle rotated, right leg lifted clear of the body.
        d.arc((s(7), s(LO), s(17), s(12)), start=170, end=350, fill=WHITE, width=w)
        d.line((s(7), s(8), s(7), s(12)), fill=WHITE, width=w)   # left leg seated
        d.line((s(17), s(8), s(17), s(10)), fill=WHITE, width=w)  # right leg lifted
    return finish(img)


def gear():
    """Settings gear: toothed ring + hollow center, all within the safe box."""
    img, d = new_big()
    r_tooth = RMAX            # tip of teeth (9.5) — just touches the safe edge
    r_body = RMAX - 2.5       # outer body ring
    r_hole = 3.2              # hollow center
    for i in range(8):
        a = math.radians(i * 45)
        x0, y0 = CX + math.cos(a) * (r_body - 1), CY + math.sin(a) * (r_body - 1)
        x1, y1 = CX + math.cos(a) * r_tooth, CY + math.sin(a) * r_tooth
        d.line((s(x0), s(y0), s(x1), s(y1)), fill=WHITE, width=int(3.4 * SS))
    d.ellipse((s(CX - r_body), s(CY - r_body), s(CX + r_body), s(CY + r_body)), fill=WHITE)
    d.ellipse((s(CX - r_hole), s(CY - r_hole), s(CX + r_hole), s(CY + r_hole)), fill=CLEAR)
    return finish(img)


def fan(on):
    """Climate fan: three teardrop blades swept around a hub. 'off' adds a slash."""
    img, _ = new_big()
    blade_len = RMAX           # blade tip reaches the safe edge (9.5 from center)
    half_w = 4.0               # blade half-width at the rim
    for i in range(3):
        layer = Image.new("RGBA", (B, B), CLEAR)
        ld = ImageDraw.Draw(layer)
        # Teardrop: wide at the rim (top), narrowing toward the hub.
        ld.ellipse((s(CX - half_w), s(CY - blade_len),
                    s(CX + half_w), s(CY - 0.5)), fill=WHITE)
        layer = layer.rotate(i * 120, center=(B / 2, B / 2), resample=Image.BICUBIC)
        img = Image.alpha_composite(img, layer)
    d = ImageDraw.Draw(img)
    d.ellipse((s(CX - 3), s(CY - 3), s(CX + 3), s(CY + 3)), fill=WHITE)   # hub
    d.ellipse((s(CX - 1), s(CY - 1), s(CX + 1), s(CY + 1)), fill=CLEAR)
    out = finish(img)
    if not on:
        # Diagonal slash: clear cut with a thin white outline so it reads anywhere.
        d2 = ImageDraw.Draw(out)
        d2.line((LO + 1, HI - 1, HI - 1, LO + 1), fill=CLEAR, width=5)
        d2.line((LO + 1, HI - 1, HI - 1, LO + 1), fill=WHITE, width=2)
    return out


def main():
    save(padlock(locked=True), "locked.png")
    save(padlock(locked=False), "unlocked.png")
    save(gear(), "settings.png")
    save(fan(on=True), "climate_on.png")
    save(fan(on=False), "climate_off.png")
    print("Wrote 5 icons to", os.path.normpath(OUT_DIR))


if __name__ == "__main__":
    main()
