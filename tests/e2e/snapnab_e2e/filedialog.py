"""Common file dialogs (Open / Save as) of both programs: type the full path in "File name" and confirm."""
import time

import win32con
import win32gui

from . import win


def wait_dialog(pid, timeout=10):
    return win.wait_until(lambda: next(iter(win.owned_popups(pid, "#32770")), 0), timeout, what="file dialog")


def fill(pid, path, timeout=10):
    dlg = wait_dialog(pid, timeout)
    # "File name": the Edit inside the ComboBoxEx 1148 (Open) or 1001 (Save); falls back to the first Edit
    edit = 0
    for cb_id in (1148, 1001):
        cb = win.child_by_id(dlg, cb_id)
        if cb:
            edits = win.children(cb, "Edit")
            edit = edits[0] if edits else (cb if win.class_name(cb) == "Edit" else 0)
            if edit:
                break
    if not edit:
        edits = win.children(dlg, "Edit")
        assert edits, "no file name box in the dialog"
        edit = edits[0]
    win32gui.SendMessage(edit, win32con.WM_SETTEXT, 0, str(path))
    time.sleep(0.2)
    win.post_command(dlg, 1)   # IDOK
    win.wait_until(lambda: not win32gui.IsWindow(dlg) or not win32gui.IsWindowVisible(dlg), timeout, what="dialog to close")
    # "replace existing file?" confirmation
    time.sleep(0.3)
    for h in win.owned_popups(pid, "#32770"):
        yes = win.child_by_id(h, 6)   # IDYES
        if yes:
            win.post_command(h, 6)
