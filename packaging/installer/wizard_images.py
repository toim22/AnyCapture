#!/usr/bin/env python3
"""Draws the setup wizard's pictures from the app icon (rust/resources/AnyCapture.ico), one file per display scale:

    python packaging/installer/wizard_images.py

- WizardLarge-<percent>.png: the side picture of the Welcome and Finish pages (164x314 at 100%), a panel in the
  icon's own dark slate with the icon and the name; WizardLarge-dark-<percent>.png is its dark-mode version.
- WizardSmall-<percent>.png: the icon at the top right of the other pages (55x55 at 100%), for both modes.

AnyCapture.iss lists the files; Setup picks the one that fits the screen's scale. Needs Pillow and Windows' Segoe UI.
"""

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ICON = HERE.parent.parent / "rust" / "resources" / "AnyCapture.ico"
FONT = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts" / "seguisb.ttf"  # Segoe UI Semibold
SCALES = [100, 125, 150, 175, 200, 250]
SS = 4  # supersampling

# the panel: the slate of the icon's background (lighter on the light page, deeper in dark mode)
LIGHT_TOP, LIGHT_BOTTOM = (43, 50, 66), (21, 26, 38)
DARK_TOP, DARK_BOTTOM = (32, 36, 46), (11, 13, 22)


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


def large(scale: int, dark: bool) -> Image.Image:
    w, h = round(164 * scale / 100), round(314 * scale / 100)
    W, H = w * SS, h * SS
    img = gradient(W, H, *(DARK_TOP, DARK_BOTTOM) if dark else (LIGHT_TOP, LIGHT_BOTTOM))
    # the icon with a soft shadow
    s = round(W * 0.46)
    x, y = (W - s) // 2, round(H * 0.25)
    mark = icon(s)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 110), (x, y + round(s * 0.06)), mark)
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(s * 0.09)))
    img.alpha_composite(mark, (x, y))
    # the name
    font = ImageFont.truetype(str(FONT), round(W * 0.155))
    d = ImageDraw.Draw(img)
    d.text((W / 2, y + s + round(H * 0.075)), "AnyCapture", font=font, fill=(255, 255, 255, 255), anchor="mt")
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
