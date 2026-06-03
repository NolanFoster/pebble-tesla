#!/usr/bin/env python3
"""Generate the action-bar icon PNGs for the Tesla Control watchapp.

Icons are white foreground on a transparent background, sized for Pebble's
30px ActionBarLayer column (the SDK centers them). Run from the repo root:

    python3 tools/gen_icons.py

This regenerates resources/images/*.png. Keeping the generator in-tree means
the icons are reproducible and easy to tweak without a binary asset editor.
"""

import os
from PIL import Image, ImageDraw

SIZE = 25                      # icon footprint (px); fits the 30px action bar
WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "resources", "images")


def new_canvas():
    img = Image.new("RGBA", (SIZE, SIZE), CLEAR)
    return img, ImageDraw.Draw(img)


def save(img, name):
    os.makedirs(OUT_DIR, exist_ok=True)
    img.save(os.path.join(OUT_DIR, name))


def padlock(locked):
    """Padlock: body rectangle + shackle. Open shackle lifts/rotates when unlocked."""
    img, d = new_canvas()
    # Body
    body = (5, 12, 19, 23)
    d.rounded_rectangle(body, radius=2, fill=WHITE)
    # Keyhole (punched out so it reads on the white body)
    d.ellipse((10, 15, 14, 19), fill=CLEAR)
    d.rectangle((11, 17, 13, 21), fill=CLEAR)
    # Shackle (an arc drawn as a thick ring, lower half removed)
    if locked:
        # Closed: symmetric arch sitting on the body, both legs down to the body top
        d.arc((7, 4, 17, 14), start=180, end=360, fill=WHITE, width=2)
        d.line((7, 9, 7, 12), fill=WHITE, width=2)
        d.line((17, 9, 17, 12), fill=WHITE, width=2)
    else:
        # Open: shackle rotated/lifted, right leg detached from the body
        d.arc((6, 2, 16, 12), start=170, end=350, fill=WHITE, width=2)
        d.line((6, 7, 6, 12), fill=WHITE, width=2)   # left leg still seated
        d.line((16, 7, 16, 9), fill=WHITE, width=2)  # right leg lifted, not seated
    return img


def gear():
    """Settings gear: outer toothed ring + hollow center."""
    img, d = new_canvas()
    cx = cy = SIZE / 2
    # Teeth: short thick spokes around the circle
    import math
    for i in range(8):
        a = math.radians(i * 45)
        x0 = cx + math.cos(a) * 7
        y0 = cy + math.sin(a) * 7
        x1 = cx + math.cos(a) * 12
        y1 = cy + math.sin(a) * 12
        d.line((x0, y0, x1, y1), fill=WHITE, width=4)
    # Body ring
    d.ellipse((cx - 9, cy - 9, cx + 9, cy + 9), fill=WHITE)
    # Hollow center
    d.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), fill=CLEAR)
    return img


def fan(on):
    """Climate fan: three teardrop blades swept around a hub. 'off' adds a slash."""
    # Supersample for smooth rotated blades, then downscale.
    ss = 8
    big = Image.new("RGBA", (SIZE * ss, SIZE * ss), CLEAR)
    cx = cy = SIZE * ss / 2
    for i in range(3):
        # One blade on its own layer so it can be rotated about the hub.
        blade = Image.new("RGBA", big.size, CLEAR)
        bd = ImageDraw.Draw(blade)
        # Teardrop: wide at the rim, narrowing toward the hub.
        bd.ellipse((cx - 4.5 * ss, 2 * ss, cx + 4.5 * ss, cy - 0.5 * ss), fill=WHITE)
        blade = blade.rotate(i * 120, center=(cx, cy), resample=Image.BICUBIC)
        big = Image.alpha_composite(big, blade)
    bd = ImageDraw.Draw(big)
    # Hub
    bd.ellipse((cx - 3 * ss, cy - 3 * ss, cx + 3 * ss, cy + 3 * ss), fill=WHITE)
    bd.ellipse((cx - 1 * ss, cy - 1 * ss, cx + 1 * ss, cy + 1 * ss), fill=CLEAR)
    img = big.resize((SIZE, SIZE), Image.LANCZOS)
    if not on:
        # Diagonal slash to indicate off: clear cut with a thin white outline so
        # it reads on any background.
        d = ImageDraw.Draw(img)
        d.line((4, 21, 21, 4), fill=CLEAR, width=5)
        d.line((4, 21, 21, 4), fill=WHITE, width=2)
    return img


def main():
    save(padlock(locked=True), "locked.png")
    save(padlock(locked=False), "unlocked.png")
    save(gear(), "settings.png")
    save(fan(on=True), "climate_on.png")
    save(fan(on=False), "climate_off.png")
    print("Wrote 5 icons to", os.path.normpath(OUT_DIR))


if __name__ == "__main__":
    main()
