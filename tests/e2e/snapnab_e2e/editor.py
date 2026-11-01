"""Editor driver: opens a known image, draws on the canvas with the real mouse, saves and reads the result back.

Oracles: the .snap written by File › Save (objects as XML through `SnapnabTests.exe dump`) and the composed image that
Copy all (Ctrl+Shift+C) puts on the clipboard (pixels)."""
import time

from PIL import Image, ImageDraw, ImageFont

from . import apps, library, regs, win
from .apps import EditorCmd as M
from .library import LibraryWatcher

KMARGIN = 40          # Canvas.cpp kMargin

# the test picture: white, a blue block, a green block and dark text (OCR / magic wand / fill targets)
W, H = 1600, 1000
BLUE_BOX = (200, 200, 700, 600)       # x0, y0, x1, y1
GREEN_BOX = (900, 250, 1400, 550)
BLUE, GREEN, WHITE = (30, 80, 220), (40, 170, 70), (255, 255, 255)


def make_picture(path, size=(W, H)):
    img = Image.new("RGB", size, WHITE)
    d = ImageDraw.Draw(img)
    d.rectangle(BLUE_BOX, fill=BLUE)
    d.rectangle(GREEN_BOX, fill=GREEN)
    try:
        font = ImageFont.truetype("segoeui.ttf", 72)
    except OSError:
        font = ImageFont.load_default()
    d.text((220, 760), "Snapnab OCR 2026", fill=(20, 20, 20), font=font)
    img.save(path)
    return path


class EditorSession:
    def __init__(self, editor: apps.Editor):
        self.ed = editor
        self.path = None

    # ---------- documents ----------
    def open_picture(self, picture):
        """Opens a picture through /Open; it is imported into the library as a .snap (the document under test)."""
        regs.set_dword("Rulers", 0, "Editor")              # image ↔ screen mapping without the ruler offset
        regs.set_dword("EdgeSnap", 0, "Editor")            # selections exactly where the mouse is
        regs.set_dword("MagnifyIntroShown", 1, "Editor")
        lib = LibraryWatcher()
        self.ed.start("/Open", str(picture))
        self.path = lib.wait_new("*.snap")[-1]
        win.wait_until(lambda: picture.stem in self.ed.title(), what="document shown")
        self.focus()
        apps.wait_quiet(0.5)
        return self.path

    def focus(self):
        win.foreground(self.ed.hwnd)

    def cmd(self, cmd_id, wait=0.3):
        self.ed.send_command(cmd_id)
        apps.wait_quiet(wait)

    def post(self, cmd_id):
        self.ed.command(cmd_id)

    def save(self):
        before = self.path.stat().st_mtime
        self.cmd(M.M_SAVE)
        win.wait_until(lambda: self.path.stat().st_mtime != before, what="document saved")
        return library.dump(self.path)

    def doc(self):
        return self.save()

    def objects(self):
        return library.objects(self.save())

    def tags(self):
        return [o.tag for o in self.objects()]

    def size(self):
        return library.size(self.save())

    def composed(self):
        """Composed image (objects merged) through Copy all."""
        seq = win.clipboard_sequence()
        self.cmd(M.M_COPYALL)
        win.wait_until(lambda: win.clipboard_sequence() != seq, what="Copy all")
        return win.wait_until(win.clipboard_image, what="image on the clipboard").convert("RGB")

    # ---------- canvas geometry ----------
    def to_screen(self, ix, iy, doc_size=None):
        """Image coordinates → screen, for the fit-to-window zoom the Editor uses for a new document."""
        c = self.ed.canvas()
        l, t, r, b = win.rect(c)
        cl = win.win32gui.ClientToScreen(c, (0, 0))
        cw, ch = win.win32gui.GetClientRect(c)[2:]
        w, h = doc_size or self.size_cached
        z = min(1.0, min((cw - 2 * KMARGIN) / w, (ch - 2 * KMARGIN) / h))
        iw, ih = round(w * z), round(h * z)
        ox = (cw - iw) // 2 if iw + 2 * KMARGIN <= cw else KMARGIN
        oy = (ch - ih) // 2 if ih + 2 * KMARGIN <= ch else KMARGIN
        return int(cl[0] + ox + ix * z), int(cl[1] + oy + iy * z)

    size_cached = (W, H)

    def fit(self):
        self.cmd(M.M_ZOOM_FIT)

    def drag(self, *img_points, hold=0.0):
        self.focus()
        win.drag([self.to_screen(x, y) for x, y in img_points], hold=hold)
        apps.wait_quiet(0.3)

    def click(self, ix, iy):
        self.focus()
        win.click(*self.to_screen(ix, iy))
        apps.wait_quiet(0.3)

    def tool(self, name):
        self.ed.tool(name)
        apps.wait_quiet(0.2)

    def keys(self, spec):
        self.focus()
        win.keys(spec)
        apps.wait_quiet(0.3)

    # ---------- side panels ----------
    def effects_panel(self):
        p = win.children(self.ed.hwnd, "SnapnabEffects")
        assert p, "effects panel not found"
        return p[0]

    def apply_effect(self, cmd_id):
        """Image › Effects › <effect>: opens its settings in the panel, then Apply (one undo step)."""
        self.cmd(cmd_id, 0.6)
        if not self.ed.running:
            raise AssertionError("SnapnabEditor crashed when the effect settings opened (see the Application event log)")
        panel = self.effects_panel()
        apply_btn = win.wait_until(lambda: win.child_by_id(panel, 90) if win.is_visible(win.child_by_id(panel, 90)) else 0,
                                   what="Apply button")
        win.click_control(apply_btn)
        apps.wait_quiet(0.8)

    def form(self):
        return win.wait_form(self.ed.pid)

    def popups(self, cls="#32770"):
        return self.ed.popups(cls)


def pixel(img, x, y):
    return img.getpixel((x, y))


def close(c, ref, tol=20):
    return all(abs(a - b) <= tol for a, b in zip(c[:3], ref[:3]))


def wait(seconds):
    time.sleep(seconds)
