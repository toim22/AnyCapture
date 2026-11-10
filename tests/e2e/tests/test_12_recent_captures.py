"""The Editor's Recent captures (rust/snapedit/src/tray.rs): selecting as in Explorer (Shift+click, a rectangle dragged
from an empty part of the list), the keyboard following the last click (Ctrl+A and Ctrl+C act on the list after a click
on it, on the picture after a click on the picture), dragging items out to another program and Export as ZIP.

Oracles: the files a Ctrl+C puts on the clipboard (CF_HDROP), the files a drop hands to the target and the ZIP."""
import time
import zipfile

import win32con
from pywinauto import mouse
import win32gui

from anycapture_e2e import apps, filedialog, uia, win
from anycapture_e2e.editor import EditorSession, make_picture
from anycapture_e2e.droptarget import DropTarget
from anycapture_e2e.library import LibraryWatcher

TRAY = "AnyCaptureTray"
MENU = "#32768"


def recent(editor, tmp_path, n=4):
    """Opens n pictures; each comes to the front of Recent captures. Returns the Editor's window."""
    sess = EditorSession(editor)
    sess.open_picture(make_picture(tmp_path / "e2e-recent-0.png"))
    for k in range(1, n):
        lib = LibraryWatcher()
        editor.open(make_picture(tmp_path / f"e2e-recent-{k}.png"))
        lib.wait_new("*.snap")
    hwnd = editor.hwnd
    win.wait_until(lambda: len(uia.items(hwnd, TRAY)) >= n, what=f"{n} items in Recent captures")
    apps.wait_quiet(0.5)
    return sess, hwnd


def pick_folder(pid, path):
    """The folder picker: types the full path in "Folder" and confirms (the first OK may only open the folder)."""
    dlg = filedialog.wait_dialog(pid)
    edits = win.children(dlg, "Edit")
    assert edits, "no folder box in the dialog"
    win32gui.SendMessage(edits[0], win32con.WM_SETTEXT, 0, str(path))
    for _ in range(3):
        win.post_command(dlg, 1)                      # Select Folder (IDOK)
        try:
            win.wait_until(lambda: not win32gui.IsWindow(dlg) or not win32gui.IsWindowVisible(dlg), 3, what="the folder picker to close")
            return
        except win.Timeout:
            pass
    raise AssertionError("the folder picker did not take the folder")


def copied(keys="^c"):
    """Presses Ctrl+C (or `keys`) and returns the files it put on the clipboard."""
    win.clear_clipboard()
    win.keys(keys)
    return win.wait_until(win.clipboard_files, what="files on the clipboard")


def rect(hwnd, aid):
    r = uia.find(hwnd, aid).rectangle()
    return r.left, r.top, r.right, r.bottom


def center(r):
    return (r[0] + r[2]) // 2, (r[1] + r[3]) // 2


def test_shift_click_ctrl_a_and_ctrl_c_follow_the_list(editor, tmp_path):
    _, hwnd = recent(editor, tmp_path)
    uia.click(hwnd, f"{TRAY}.0")                     # opens it; the keyboard now belongs to the list
    apps.wait_quiet(0.5)
    with win.held(win32con.VK_SHIFT):
        win.click(*center(rect(hwnd, f"{TRAY}.2")))   # Shift: from the first to the third, as in Explorer
    apps.wait_quiet(0.3)
    assert len(copied()) == 3, "Shift+click selects the items between the two"
    win.keys("^a")
    apps.wait_quiet(0.3)
    assert len(copied()) >= 4, "Ctrl+A after a click on the list selects all of Recent captures"


def test_ctrl_a_after_a_click_on_the_picture_selects_the_picture(editor, tmp_path):
    sess, hwnd = recent(editor, tmp_path, n=2)
    uia.click(hwnd, f"{TRAY}.0")
    apps.wait_quiet(0.5)
    sess.click(800, 500)                              # the keyboard goes back to the picture
    win.clear_clipboard()
    win.keys("^a")
    apps.wait_quiet(0.3)
    win.keys("^c")
    win.wait_until(win.clipboard_image, what="the picture on the clipboard")
    assert not win.clipboard_files(), "Ctrl+A / Ctrl+C acted on the list instead of the picture"


def test_a_rectangle_from_the_empty_part_selects_the_items_it_touches(editor, tmp_path):
    _, hwnd = recent(editor, tmp_path, n=3)
    uia.click(hwnd, f"{TRAY}.0")
    apps.wait_quiet(0.5)
    first, second, third = rect(hwnd, f"{TRAY}.0"), rect(hwnd, f"{TRAY}.1"), rect(hwnd, f"{TRAY}.2")
    start = ((second[2] + third[0]) // 2, second[1] + 20)   # the gap between the second and third items: empty
    assert third[0] - second[2] >= 2, "no gap between the items"
    win.drag([start, center(first)], hold=0.2)        # touches the second and the first
    apps.wait_quiet(0.3)
    assert len(copied()) == 2, "the rectangle selects the two items it touches"


def test_dragging_items_out_drops_them_as_files(editor, tmp_path):
    """A drag from Recent captures to another program (a drop target of the test's own, standing in for Explorer or
    a chat) drops the capture as a PNG file."""
    _, hwnd = recent(editor, tmp_path, n=2)
    el, et, er, eb = win.rect(hwnd)
    target = DropTarget(el + 120, et + 160, 360, 240)   # over the picture, above everything
    try:
        item = center(rect(hwnd, f"{TRAY}.0"))
        drop = (el + 120 + 180, et + 160 + 120)
        # as a hand does it: the drag starts (the Editor writes the PNG first), then the pointer travels to the target
        # and moves a little over it before the button is let go
        mouse.move(coords=item)
        time.sleep(0.1)
        mouse.press(coords=item)
        for k in range(1, 6):
            mouse.move(coords=(item[0] + 6 * k, item[1] - 6 * k))
            time.sleep(0.03)
        time.sleep(1.5)
        a = (item[0] + 30, item[1] - 30)
        for k in range(1, 21):
            mouse.move(coords=(a[0] + (drop[0] - a[0]) * k // 20, a[1] + (drop[1] - a[1]) * k // 20))
            time.sleep(0.04)
        for d in (8, -8, 4, 0):
            mouse.move(coords=(drop[0] + d, drop[1] + d))
            time.sleep(0.15)
        mouse.release(coords=drop)
        files = win.wait_until(lambda: target.files or target.error, 15, what="the drop")
        assert not target.error, target.error
        assert len(files) == 1 and files[0].lower().endswith(".png"), files
    finally:
        target.close()


def test_export_as_zip_puts_the_selected_captures_in_one_archive(editor, tmp_path):
    _, hwnd = recent(editor, tmp_path, n=2)
    out = tmp_path / "out"
    out.mkdir()
    uia.click(hwnd, f"{TRAY}.0")
    apps.wait_quiet(0.5)
    with win.held(win32con.VK_SHIFT):
        win.click(*center(rect(hwnd, f"{TRAY}.1")))
    apps.wait_quiet(0.3)
    win.click(*center(rect(hwnd, f"{TRAY}.1")), button="right")
    win.wait_until(lambda: win.top_windows(MENU, visible_only=True), what="the context menu")
    win.keys("{DOWN}{DOWN}{ENTER}")                   # Copy, Export 2 items…
    form = win.wait_form(editor.pid)
    box = win.child_by_id(form, 1001)                  # Export as ZIP
    win.send(box, win32con.BM_SETCHECK, 1)
    win.form_ok(form)
    pick_folder(editor.pid, out)
    archive = win.wait_until(lambda: next(out.glob("*.zip"), None), 30, what="the ZIP in the folder")
    win.wait_until(lambda: not list(out.glob("*.png")), what="only the ZIP in the folder")
    for h in win.top_windows("CabinetWClass", visible_only=True):   # the Explorer window that shows it
        if win.title(h).startswith("out"):
            win.close_window(h)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        names = z.namelist()
    assert len(names) == 2 and all(n.lower().endswith(".png") for n in names), names
