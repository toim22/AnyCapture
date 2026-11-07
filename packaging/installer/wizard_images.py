#!/usr/bin/env python3
"""Draws the setup wizard's pictures from the app icon (rust/resources/Snapnab.ico), one file per display scale:

    python packaging/installer/wizard_images.py

- WizardLarge-<percent>.png: the side picture of the Welcome and Finish pages (164x314 at 100%), a blue panel with the
  icon in white and the name; WizardLarge-dark-<percent>.png is its dark-mode version.
- WizardSmall-<percent>.png: the icon at the top right of the other pages (55x55 at 100%), for both modes.

Snapnab.iss lists the files; Setup picks the one that fits the screen's scale. Needs Pillow and Windows' Segoe UI.
"""

import os
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ICON = HERE.parent.parent / "rust" / "resources" / "Snapnab.ico"
FONT = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts" / "seguisb.ttf"  # Segoe UI Semibold
SCALES = [100, 125, 150, 175, 200, 250]
SS = 4  # supersampling

# the icon's blue (top left → bottom right) and the panel's
BLUE_TOP, BLUE_BOTTOM = (40, 140, 255), (24, 66, 200)
DARK_TOP, DARK_BOTTOM = (44, 44, 52), (24, 24, 28)


def icon(size: int) -> Image.Image:
    ico = Image.open(ICON)
    ico.size = max(ico.info["sizes"])
    return ico.convert("RGBA").resize((size, size), Image.LANCZOS)


def gradient(w: int, h: int, top, bottom) -> Image.Image:
    """A gradient from the top left to the bottom right (mostly downwards)."""
    down = Image.linear_gradient("L").resize((w, h))
    across = Image.linear_gradient("L").rotate(90).transpose(Image.FLIP_LEFT_RIGHT).resize((w, h))
    g = Image.blend(across, down, 0.7)
    a, b = Image.new("RGB", (w, h), top), Image.new("RGB", (w, h), bottom)
    return Image.composite(b, a, g).convert("RGBA")


def inverted_icon(size: int) -> Image.Image:
    """The icon in white with its mark in blue (the white glyph of the icon becomes the blue one)."""
    src = icon(size)
    r, g, b, a = src.split()
    white = ImageChops.darker(ImageChops.darker(r, g), b)  # the glyph is the white part
    glyph = white.point(lambda v: max(0, min(255, (v - 70) * 255 // 185)))
    out = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    out.putalpha(a)
    blue = gradient(size, size, BLUE_TOP, BLUE_BOTTOM)
    out = Image.composite(blue, out, glyph)
    out.putalpha(a)
    return out


def rings(img: Image.Image, cx: float, cy: float, alpha: int) -> None:
    """The capture mark as a faint large background motif."""
    w, h = img.size
    layer = Image.new("RGBA", img.size, (255, 255, 255, 0))
    d = ImageDraw.Draw(layer)
    for r, t in [(0.72, 0.03), (0.44, 0.03)]:
        rr, tt = r * w, t * w
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=(255, 255, 255, alpha), width=max(1, round(tt)))
    img.alpha_composite(layer)


def large(scale: int, dark: bool) -> Image.Image:
    w, h = round(164 * scale / 100), round(314 * scale / 100)
    W, H = w * SS, h * SS
    img = gradient(W, H, *(DARK_TOP, DARK_BOTTOM) if dark else (BLUE_TOP, BLUE_BOTTOM))
    rings(img, W * 0.95, H * 0.97, 12 if dark else 22)
    # the icon with a soft shadow
    s = round(W * 0.46)
    x, y = (W - s) // 2, round(H * 0.25)
    mark = icon(s) if dark else inverted_icon(s)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 110 if dark else 70), (x, y + round(s * 0.06)), mark)
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(s * 0.09)))
    img.alpha_composite(mark, (x, y))
    # the name
    font = ImageFont.truetype(str(FONT), round(W * 0.155))
    d = ImageDraw.Draw(img)
    d.text((W / 2, y + s + round(H * 0.075)), "Snapnab", font=font, fill=(255, 255, 255, 255), anchor="mt")
    return img.resize((w, h), Image.LANCZOS).convert("RGB")


def small(scale: int) -> Image.Image:
    n = round(55 * scale / 100)
    return icon(n * SS).resize((n, n), Image.LANCZOS)


def main() -> None:
    for old in list(HERE.glob("WizardLarge*.png")) + list(HERE.glob("WizardSmall*.png")):
        old.unlink()
    for sc in SCALES:
        large(sc, False).save(HERE / f"WizardLarge-{sc}.png", optimize=True)
        large(sc, True).save(HERE / f"WizardLarge-dark-{sc}.png", optimize=True)
        small(sc).save(HERE / f"WizardSmall-{sc}.png", optimize=True)
    print(f"{len(SCALES) * 3} images in {HERE}")


if __name__ == "__main__":
    main()
