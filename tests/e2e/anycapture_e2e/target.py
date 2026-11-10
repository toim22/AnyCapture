"""A known window to capture: solid color, known size and title, at a known position (tkinter subprocess)."""
import subprocess
import sys
import textwrap

from . import win

TITLE = "AnyCapture E2E Target"

_SCRIPT = textwrap.dedent(r"""
    import ctypes, sys, tkinter as tk
    ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    x, y, w, h, color, title = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], sys.argv[6]
    r = tk.Tk(); r.title(title); r.configure(bg=color); r.geometry(f"{w}x{h}+{x}+{y}"); r.attributes("-topmost", True)
    tk.Label(r, text=title, bg=color, fg="white", font=("Segoe UI", 20)).place(relx=0.5, rely=0.5, anchor="center")
    r.mainloop()
""")


class TargetWindow:
    def __init__(self, x=200, y=200, w=600, h=400, color="#C81E1E", title=TITLE):
        self.args = [str(x), str(y), str(w), str(h), color, title]
        self.title = title
        self.proc = subprocess.Popen([sys.executable, "-c", _SCRIPT, *self.args])
        self.hwnd = win.wait_until(lambda: self._find(), 15, what="target window")
        win.foreground(self.hwnd)

    def _find(self):
        for h in win.top_windows(visible_only=True, pid=self.proc.pid):
            if win.title(h) == self.title:
                return h
        return 0

    @property
    def rect(self):
        return win.rect(self.hwnd)

    def client_center(self):
        l, t, r, b = self.rect
        return (l + r) // 2, (t + b) // 2

    def close(self):
        self.proc.kill()
        self.proc.wait(5)
