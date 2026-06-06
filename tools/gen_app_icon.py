#!/usr/bin/env python3
"""Generate the Tesla Control app icons.

Two outputs, both drawn from one supersampled sedan-silhouette path so they stay
visually identical:

  resources/images/menu_icon.png   25x25  white glyph, transparent bg
                                          (the on-watch launcher icon; Pebble
                                          recolors it, so it must be 1-color +
                                          alpha)
  store/icon.png                   144x144 white glyph on the Tesla-red accent,
                                          rounded square (app store listing icon)

Keeping the generator in-tree mirrors tools/gen_icons.py: the icons are
reproducible and tweakable without a binary editor. Run from the repo root:

    python3 tools/gen_app_icon.py
"""

import os
from PIL import Image, ImageDraw

SS = 16                                    # supersample factor for smooth curves
RED = (227, 25, 55, 255)                   # #E31937 — the app's Tesla-red accent
WHITE = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)

ROOT = os.path.join(os.path.dirname(__file__), "..")
IMG_DIR = os.path.join(ROOT, "resources", "images")
STORE_DIR = os.path.join(ROOT, "store")


def draw_car(size, color):
    """Return an RGBA image of a centered sedan silhouette in `color`.

    Coordinates are fractions of `size` so the same path scales to any output.
    The body + greenhouse are one filled path; two wheel wells are punched out
    (transparent) and white wheels dropped in, so the car reads even at 25px.
    """
    B = size * SS
    img = Image.new("RGBA", (B, B), CLEAR)
    d = ImageDraw.Draw(img)

    def px(x, y):
        return (x * B, y * B)

    # Body + greenhouse silhouette (side profile), drawn as one polygon with a
    # rounded roofline. Points run clockwise from the lower-left.
    body = [
        px(0.06, 0.66),   # lower front
        px(0.10, 0.55),   # hood rise
        px(0.30, 0.52),   # base of windshield
        px(0.40, 0.36),   # roof front
        px(0.62, 0.36),   # roof rear
        px(0.72, 0.52),   # base of rear glass
        px(0.92, 0.55),   # trunk
        px(0.94, 0.66),   # lower rear
    ]
    d.polygon(body, fill=color)
    # Round the belt line a touch with a rectangle so the bottom edge is flat.
    d.rectangle([px(0.06, 0.62)[0], px(0, 0.62)[1],
                 px(0.94, 0)[0], px(0, 0.70)[1]], fill=color)

    # Wheel wells: punch transparent arches, then drop white wheels in.
    for cx in (0.30, 0.70):
        r = 0.115
        cy = 0.70
        well = [px(cx - r - 0.02, cy - r), px(cx + r + 0.02, cy + r + 0.04)]
        d.ellipse(well, fill=CLEAR)
    for cx in (0.30, 0.70):
        r = 0.085
        cy = 0.71
        d.ellipse([px(cx - r, cy - r), px(cx + r, cy + r)], fill=color)

    return img.resize((size, size), Image.LANCZOS)


def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


def main():
    # On-watch launcher icon: white car, transparent background.
    menu = draw_car(25, WHITE)
    save(menu, os.path.join(IMG_DIR, "menu_icon.png"))

    # Store listing icon: white car on a red rounded square.
    size = 144
    icon = Image.new("RGBA", (size, size), CLEAR)
    d = ImageDraw.Draw(icon)
    radius = int(size * 0.22)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=RED)
    car = draw_car(size, WHITE)
    icon.alpha_composite(car)
    save(icon, os.path.join(STORE_DIR, "icon.png"))

    print("wrote resources/images/menu_icon.png (25x25) and store/icon.png (144x144)")


if __name__ == "__main__":
    main()
