#!/usr/bin/env python3
"""Generate the action-bar icon PNGs for the Tesla Control watchapp.

Icons are white foreground on a transparent background. Everything is drawn
inside a SAFE content box that leaves a margin on all sides, then supersampled
and downscaled once to the output footprint for smooth edges. The glyphs are
drawn on a 25px design grid and emitted at a per-icon footprint: the action-bar
glyphs at OUT_AB (larger, since the custom action bar in main.c sets its own
column width) and the controls-menu glyphs at OUT_MENU. Run from the repo root:

    python3 tools/gen_icons.py

This regenerates resources/images/*.png. Keeping the generator in-tree means
the icons are reproducible and easy to tweak without a binary asset editor.
"""

import math
import os

from PIL import Image, ImageDraw

ICON = 25                          # design grid (drawing coordinates)
OUT_MENU = 18                      # output footprint (px) for controls-menu glyphs
OUT_AB   = 24                      # output footprint (px) for action-bar glyphs;
                                   # larger because the custom action bar draws
                                   # them itself (no SDK clip) in a wider column.
MARGIN = 2                         # keep glyphs this far from every edge.
SS = 12                            # supersample factor for smooth curves
B = ICON * SS                      # big working-canvas size
WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "resources", "images")

# Safe drawing region in design-grid px: [MARGIN, ICON - MARGIN].
LO, HI = MARGIN, ICON - MARGIN     # 2 .. 23
CX = CY = ICON / 2                 # center (12.5)
RMAX = (ICON / 2) - MARGIN         # max radius from center that stays in bounds (10.5)


def s(v):
    """Scale an output-px coordinate to the big working canvas."""
    return v * SS


def new_big():
    img = Image.new("RGBA", (B, B), CLEAR)
    return img, ImageDraw.Draw(img)


def finish(img, out):
    return img.resize((out, out), Image.LANCZOS)


def save(img, name):
    os.makedirs(OUT_DIR, exist_ok=True)
    img.save(os.path.join(OUT_DIR, name))


def darken(img):
    """Recolor a white-on-transparent glyph to black, preserving its alpha.

    The watchapp themes the action bar / menu highlight to the car's paint
    color; on light themes (white / silver cars) the default white glyphs would
    vanish, so a black-glyph copy of every icon is emitted alongside the white
    one and selected at runtime by luminance (see s_dark_fg in main.c)."""
    img = img.copy()
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            px[x, y] = (0, 0, 0, a)
    return img


def save_both(img, stem):
    """Save the white glyph as <stem>.png and its black variant as <stem>_black.png."""
    save(img, stem + ".png")
    save(darken(img), stem + "_black.png")


def padlock(locked, out):
    """Padlock: body + shackle. Shackle lifts/detaches on one side when unlocked.

    Everything is laid out relative to CX so the glyph grows with the safe box
    instead of sitting in a fixed (and narrow) pixel rectangle.
    """
    img, d = new_big()
    body_hw = RMAX - 2.5       # body half-width: wide, just inside the safe box
    body_top = CY - 0.5        # body occupies the lower half
    sh_hw = body_hw - 3.0      # shackle is narrower than the body
    w = int(2.6 * SS)          # shackle stroke
    # Body (a wide rounded block filling most of the safe width)
    d.rounded_rectangle((s(CX - body_hw), s(body_top), s(CX + body_hw), s(HI)),
                        radius=s(3), fill=WHITE)
    # Keyhole punched out of the white body (circle + tapered slot)
    kc = body_top + 3.6
    d.ellipse((s(CX - 2.4), s(kc - 2.3), s(CX + 2.4), s(kc + 2.3)), fill=CLEAR)
    d.polygon([(s(CX - 0.9), s(kc)), (s(CX - 1.9), s(HI - 1.5)),
               (s(CX + 1.9), s(HI - 1.5)), (s(CX + 0.9), s(kc))], fill=CLEAR)
    if locked:
        # Closed arch seated on the body, both legs down.
        d.arc((s(CX - sh_hw), s(LO + 1), s(CX + sh_hw), s(body_top + 2)),
              start=180, end=360, fill=WHITE, width=w)
        d.line((s(CX - sh_hw), s(CY - 4), s(CX - sh_hw), s(body_top + 0.5)), fill=WHITE, width=w)
        d.line((s(CX + sh_hw), s(CY - 4), s(CX + sh_hw), s(body_top + 0.5)), fill=WHITE, width=w)
    else:
        # Open: shackle rotated, right leg lifted clear of the body.
        d.arc((s(CX - sh_hw - 0.5), s(LO), s(CX + sh_hw - 0.5), s(body_top + 1)),
              start=170, end=350, fill=WHITE, width=w)
        d.line((s(CX - sh_hw - 0.5), s(CY - 5), s(CX - sh_hw - 0.5), s(body_top + 0.5)),
               fill=WHITE, width=w)                                  # left leg seated
        d.line((s(CX + sh_hw - 0.5), s(CY - 5), s(CX + sh_hw - 0.5), s(CY - 2.5)),
               fill=WHITE, width=w)                                  # right leg lifted
    return finish(img, out)


def gear(out):
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
    return finish(img, out)


def fan(on, out):
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
    res = finish(img, out)
    if not on:
        # Diagonal slash: clear cut with a thin white outline so it reads
        # anywhere. Drawn in output-px space, so scale the design coords by kout.
        kout = out / ICON
        d2 = ImageDraw.Draw(res)
        p = ((LO + 1) * kout, (HI - 1) * kout, (HI - 1) * kout, (LO + 1) * kout)
        d2.line(p, fill=CLEAR, width=round(5 * kout))
        d2.line(p, fill=WHITE, width=round(2 * kout))
    return res


def thermometer(up, out):
    """Thermometer (tube + bulb) with an up/down arrow for the temp ± rows."""
    img, d = new_big()
    tx = 9.0                                   # tube on the left
    w = 3.0
    d.rounded_rectangle((s(tx - w / 2), s(LO + 1), s(tx + w / 2), s(17)),
                        radius=s(w / 2), fill=WHITE)
    br = 3.3                                    # bulb
    d.ellipse((s(tx - br), s(17 - br + 1), s(tx + br), s(17 + br + 1)), fill=WHITE)
    ax = 17                                     # arrow on the right
    aw = int(2.0 * SS)
    if up:
        d.line((s(ax), s(HI), s(ax), s(LO + 2)), fill=WHITE, width=aw)
        d.polygon([(s(ax), s(LO - 1)), (s(ax - 3), s(LO + 4)), (s(ax + 3), s(LO + 4))], fill=WHITE)
    else:
        d.line((s(ax), s(LO + 1), s(ax), s(HI - 1)), fill=WHITE, width=aw)
        d.polygon([(s(ax), s(HI + 1)), (s(ax - 3), s(HI - 4)), (s(ax + 3), s(HI - 4))], fill=WHITE)
    return finish(img, out)


def car(front, out):
    """Car side profile; the front (frunk) or rear (trunk) lid is raised open.

    A proper silhouette — lower body + sloped cabin/roofline + two wheels — so
    the glyph reads as a car rather than a slab, with one end's lid lifted to
    show which compartment opens (front = frunk, rear = trunk).
    """
    img, d = new_big()
    # Wheels first, sitting on the baseline just inside the safe box.
    wr = 2.4
    wy = 19.0
    for wx in (8.0, 17.0):
        d.ellipse((s(wx - wr), s(wy - wr), s(wx + wr), s(wy + wr)), fill=WHITE)
    # Body + cabin as one filled silhouette (front at left, rear at right).
    body = [
        (4.0, 18.0), (4.0, 13.5), (8.5, 13.5), (10.5, 9.5),
        (15.5, 9.5), (17.5, 13.5), (21.0, 13.5), (21.0, 18.0),
    ]
    d.polygon([(s(x), s(y)) for x, y in body], fill=WHITE)
    # Raised lid: a thick angled panel hinged at the cabin, lifting at one end.
    lw = int(2.4 * SS)
    if front:
        d.line((s(8.5), s(13.5), s(4.0), s(7.0)), fill=WHITE, width=lw)
    else:  # rear hatch
        d.line((s(17.5), s(13.5), s(21.0), s(7.0)), fill=WHITE, width=lw)
    return finish(img, out)


def bolt(out):
    """Charge port: a lightning bolt."""
    img, d = new_big()
    pts = [(14, LO), (8, 13), (12, 13), (11, HI), (17, 10), (13, 10)]
    d.polygon([(s(x), s(y)) for x, y in pts], fill=WHITE)
    return finish(img, out)


def refresh(out):
    """Refresh: a ~300° circular arrow with an arrowhead at the open end."""
    img, d = new_big()
    r = RMAX - 1
    bbox = (s(CX - r), s(CY - r), s(CX + r), s(CY + r))
    d.arc(bbox, start=300, end=210, fill=WHITE, width=int(2.6 * SS))
    ax = CX + r * math.cos(math.radians(300))
    ay = CY + r * math.sin(math.radians(300))
    d.polygon([(s(ax - 3), s(ay)), (s(ax + 3), s(ay)), (s(ax), s(ay - 4))], fill=WHITE)
    return finish(img, out)


def main():
    # Each icon is emitted in both polarities (white + *_black) so the UI can
    # keep contrast against any car-color theme.
    # Action-bar glyphs at the larger OUT_AB footprint.
    save_both(padlock(locked=True,  out=OUT_AB), "locked")
    save_both(padlock(locked=False, out=OUT_AB), "unlocked")
    save_both(gear(out=OUT_AB),                  "settings")
    save_both(fan(on=True,  out=OUT_AB),         "climate_on")
    save_both(fan(on=False, out=OUT_AB),         "climate_off")
    # Controls-menu glyphs stay at OUT_MENU.
    save_both(thermometer(up=True,  out=OUT_MENU), "temp_up")
    save_both(thermometer(up=False, out=OUT_MENU), "temp_down")
    save_both(car(front=True,  out=OUT_MENU), "frunk")
    save_both(car(front=False, out=OUT_MENU), "trunk")
    save_both(bolt(out=OUT_MENU),    "charge")
    save_both(refresh(out=OUT_MENU), "refresh")
    print("Wrote 22 icons (11 white + 11 black) to", os.path.normpath(OUT_DIR))


if __name__ == "__main__":
    main()
