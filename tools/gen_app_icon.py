#!/usr/bin/env python3
"""Generate the Tesla Control app icons.

Two outputs, both drawn from one supersampled sedan-silhouette path so they stay
visually identical:

  resources/images/menu_icon.png   25x25  white car with a black outline,
                                          transparent bg (the on-watch launcher
                                          icon). The outline keeps the white glyph
                                          visible on light launcher backgrounds.
  store/icon.png                   144x144 same glyph on the Tesla-red accent,
                                          rounded square (app store listing icon)

Keeping the generator in-tree mirrors tools/gen_icons.py: the icons are
reproducible and tweakable without a binary editor. Run from the repo root:

    python3 tools/gen_app_icon.py     # needs Pillow (uv run --with Pillow ...)
"""

import math
import os
from PIL import Image, ImageDraw

SS = 16                                    # supersample factor for smooth curves
RED = (227, 25, 55, 255)                   # #E31937 — the app's Tesla-red accent
WHITE = (255, 255, 255, 255)
BLACK = (0, 0, 0, 255)
CLEAR = (0, 0, 0, 0)

ROOT = os.path.join(os.path.dirname(__file__), "..")
IMG_DIR = os.path.join(ROOT, "resources", "images")
STORE_DIR = os.path.join(ROOT, "store")


def _render_car(B, color):
    """Draw the sedan silhouette in `color` on a BxB transparent canvas."""
    img = Image.new("RGBA", (B, B), CLEAR)
    d = ImageDraw.Draw(img)

    def px(x, y):
        return (x * B, y * B)

    # Body + greenhouse silhouette (side profile), one polygon with a rounded
    # roofline. Points run clockwise from the lower-left.
    body = [
        px(0.06, 0.66), px(0.10, 0.55), px(0.30, 0.52), px(0.40, 0.36),
        px(0.62, 0.36), px(0.72, 0.52), px(0.92, 0.55), px(0.94, 0.66),
    ]
    d.polygon(body, fill=color)
    d.rectangle([px(0.06, 0.62)[0], px(0, 0.62)[1],
                 px(0.94, 0)[0], px(0, 0.70)[1]], fill=color)

    # Wheel wells punched transparent, then white wheels dropped in.
    for cx in (0.30, 0.70):
        r = 0.115
        d.ellipse([px(cx - r - 0.02, 0.70 - r), px(cx + r + 0.02, 0.70 + r + 0.04)],
                  fill=CLEAR)
    for cx in (0.30, 0.70):
        r = 0.085
        d.ellipse([px(cx - r, 0.71 - r), px(cx + r, 0.71 + r)], fill=color)

    return img


def _tint(layer, color):
    """Recolor a layer to `color`, keeping its alpha (used for the outline)."""
    out = Image.new("RGBA", layer.size, color)
    out.putalpha(layer.split()[3])
    return out


def draw_car(size, fill, border_frac=0.0, border_color=BLACK):
    """Sedan silhouette in `fill`, optionally ringed by a `border_frac` outline.

    The outline is built by compositing black-tinted copies of the glyph offset
    in a circle (a cheap, smooth stroke) and drawing the fill on top.
    """
    B = size * SS
    base = _render_car(B, fill)
    if border_frac > 0:
        bw = border_frac * B
        ring = Image.new("RGBA", (B, B), CLEAR)
        edge = _tint(base, border_color)
        for ang in range(0, 360, 15):
            dx = int(round(bw * math.cos(math.radians(ang))))
            dy = int(round(bw * math.sin(math.radians(ang))))
            ring.alpha_composite(edge, (dx, dy))
        ring.alpha_composite(base)   # fill on top of the outline
        base = ring
    return base.resize((size, size), Image.LANCZOS)


def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


def main():
    # On-watch launcher icon: white car + black outline, transparent background.
    save(draw_car(25, WHITE, border_frac=0.07),
         os.path.join(IMG_DIR, "menu_icon.png"))

    # Store listing icon: same glyph on a red rounded square.
    size = 144
    icon = Image.new("RGBA", (size, size), CLEAR)
    d = ImageDraw.Draw(icon)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=int(size * 0.22), fill=RED)
    icon.alpha_composite(draw_car(size, WHITE, border_frac=0.05))
    save(icon, os.path.join(STORE_DIR, "icon.png"))

    print("wrote resources/images/menu_icon.png (25x25) and store/icon.png (144x144)")


if __name__ == "__main__":
    main()
