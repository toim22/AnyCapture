"""The 12 effects applied on capture (CaptureEffects, comp_7 §2.1), checked on the saved file's pixels and size.

Each test captures the same fixed region of the solid red target window (#C81E1E) with one effect turned on."""
import pytest
from PIL import Image

from anycapture_e2e import presets, win

RED = (200, 30, 30)
W, H = 240, 120


def shoot(capture, tmp_path, target, effects=None, region=None, name="fx"):
    l, t, _, _ = target.rect
    x, y, w, h = region or (l + 20, t + 50, W, H)
    presets.write(presets.preset(name, "Region", editor=False, save_to=tmp_path, prefix=name, fixed=(w, h),
                                 region_fixed=True, region_pos=(x, y), effects=effects))
    capture.start("/hide")
    assert capture.send("/preset", name) == 0
    f = win.wait_until(lambda: list(tmp_path.glob(f"{name}*.png")), 15, what="saved capture")[0]
    win.wait_until(lambda: f.stat().st_size > 0, what="file written")
    img = Image.open(f)
    img.load()
    return img.convert("RGB"), img.info


def close(c, ref, tol=12):
    return all(abs(a - b) <= tol for a, b in zip(c, ref))


def center(img):
    return img.getpixel((img.width // 2, img.height // 2))


def test_baseline_is_the_red_window(capture, tmp_path, target):
    img, _ = shoot(capture, tmp_path, target)
    assert img.size == (W, H)
    assert close(center(img), RED)


def test_border(capture, tmp_path, target):
    img, _ = shoot(capture, tmp_path, target, dict(Border=1, BorderWidth=10, BorderColor="#0000FF"))
    assert img.size == (W + 20, H + 20)
    assert close(img.getpixel((2, 2)), (0, 0, 255)) and close(center(img), RED)


@pytest.mark.parametrize("kind,check", [
    (0, lambda p: max(p) - min(p) <= 3),                                  # grayscale
    (1, lambda p: p[0] >= p[1] >= p[2] and p[0] - p[2] > 10),             # sepia
    (2, lambda p: close(p, (55, 225, 225), 15)),                          # invert
    (3, lambda p: all(c in (0, 255) for c in p) and len(set(p)) == 1),    # black and white
], ids=["grayscale", "sepia", "invert", "black-white"])
def test_filter(capture, tmp_path, target, kind, check):
    img, _ = shoot(capture, tmp_path, target, dict(Filter=1, FilterKind=kind))
    assert check(center(img)), center(img)


def test_shadow_grows_the_image(capture, tmp_path, target):
    img, _ = shoot(capture, tmp_path, target, dict(Shadow=1, ShadowOffset=10, ShadowBlur=6))
    assert img.width > W and img.height > H


@pytest.mark.parametrize("top", [0, 1])
def test_caption(capture, tmp_path, target, top):
    img, _ = shoot(capture, tmp_path, target, dict(Caption=1, CaptionText="E2E {computer}", CaptionTop=top))
    assert img.width == W and img.height > H
    strip_y = 3 if top else img.height - 3
    assert not close(img.getpixel((W // 2, strip_y)), RED, 40), "the caption strip is not the image"


def test_replace_color(capture, tmp_path, target):
    img, _ = shoot(capture, tmp_path, target, dict(ReplaceColor=1, ReplaceFrom="#C81E1E", ReplaceTo="#0000FF",
                                                    ReplaceTolerance=20))
    assert close(center(img), (0, 0, 255), 20), center(img)


def test_resize_50_percent(capture, tmp_path, target):
    img, _ = shoot(capture, tmp_path, target, dict(ScaleEnabled=1, Scale=50))
    assert img.size == (W // 2, H // 2)


def test_edge_effect_changes_the_edges(capture, tmp_path, target):
    img, _ = shoot(capture, tmp_path, target, dict(Edge=1, EdgeStyle=0, EdgeSize=20, EdgeSides=6))
    assert close(center(img), RED)
    w, h = img.size
    edge = img.getpixel((w - 3, h - 3))
    assert not close(edge, RED), f"bottom-right edge should be torn/faded, got {edge}"


def test_trim_edges(capture, tmp_path, target):
    l, t, r, b = target.rect
    cx, cy = (l + r) // 2, (t + b) // 2
    # the label in the middle is the only thing that is not red: trimming keeps about the label
    img, _ = shoot(capture, tmp_path, target, dict(Trim=1, TrimTolerance=20), region=(cx - 200, cy - 60, 400, 120))
    assert img.width < 400 and img.height < 120, img.size


def test_color_adjustment_brightness(capture, tmp_path, target):
    img, _ = shoot(capture, tmp_path, target, dict(Adjust=1, Brightness=60))
    p = center(img)
    assert sum(p) > sum(RED) + 60, p


@pytest.mark.parametrize("kind,size", [(0, (H, W)), (1, (H, W)), (2, (W, H)), (3, (W, H)), (4, (W, H))],
                         ids=["cw", "ccw", "180", "flip-h", "flip-v"])
def test_rotate(capture, tmp_path, target, kind, size):
    img, _ = shoot(capture, tmp_path, target, dict(Rotate=1, RotateKind=kind))
    assert img.size == size


def test_watermark(capture, tmp_path, target):
    from anycapture_e2e import apps
    base, _ = shoot(capture, tmp_path, target, name="base")
    apps.kill_all()
    img, _ = shoot(capture, tmp_path, target, dict(Watermark=1, WatermarkText="E2E", WatermarkOpacity=80,
                                                    WatermarkTiled=1), name="wm")
    diff = sum(1 for a, b in zip(base.getdata(), img.getdata()) if not close(a, b, 20))
    assert diff > W * H * 0.02, f"only {diff} pixels changed"


def test_resolution_is_written_to_the_file(capture, tmp_path, target):
    _, info = shoot(capture, tmp_path, target, dict(Resolution=1, Dpi=300))
    dpi = info.get("dpi")
    assert dpi and round(dpi[0]) == 300, info


def test_all_effects_together(capture, tmp_path, target):
    # trim is left out: on a solid region it would cut almost everything (test_trim_edges covers it)
    fx = dict(Border=1, Filter=1, Shadow=1, Caption=1, ReplaceColor=1, ScaleEnabled=1, Scale=80, Edge=1, Adjust=1,
              Brightness=10, Rotate=1, RotateKind=0, Watermark=1, Resolution=1, Dpi=300)
    img, info = shoot(capture, tmp_path, target, fx)
    # 240x120 scaled to 192x96 and turned 90°: taller than wide, plus border/shadow/caption around it
    assert img.width >= 96 and img.height >= 192 and img.height > img.width, img.size
    assert round(info["dpi"][0]) == 300
    p = center(img)
    assert max(p) - min(p) <= 3, "the grayscale filter applies together with the others"
