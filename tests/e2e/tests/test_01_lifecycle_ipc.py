"""Process model and IPC between Snapnab.exe and SnapnabEditor.exe (ARCHITECTURE.md: single instance via mutex +
window, WM_COPYDATA forwarding of the command line, shared-memory notification channel)."""
import pytest
from PIL import Image

from snapnab_e2e import apps, library, presets, regs, win
from snapnab_e2e.apps import CaptureCmd, EditorCmd
from snapnab_e2e.logs import (EDITOR_CLOSED, EDITOR_START_CAPTURE, LIBRARY_CHANGED, LIBRARY_DOC_OPENED,
                              NEW_IMAGE_FROM_CLIPBOARD, SHOW_EDITOR, ipc_received, ipc_sent, state)


@pytest.fixture
def png(tmp_path):
    p = tmp_path / "e2e-input.png"
    Image.new("RGB", (640, 360), (30, 120, 200)).save(p)
    return p


# ---------- capturer: single instance and command line ----------

@pytest.mark.smoke
def test_capture_starts_hidden_in_tray(capture):
    capture.start("/hide")
    assert capture.hwnd, "SnapnabUI window must exist (it owns the tray icon and the hotkeys)"
    assert not win.is_visible(capture.hwnd), "/hide must keep the capture window hidden"


def test_capture_shows_window_on_normal_start(capture):
    regs.set_dword("ShowCaptureWindow", 1)
    capture.start(hidden=False)
    win.wait_until(lambda: win.is_visible(capture.hwnd), what="capture window visible")


def test_second_instance_forwards_and_exits(capture):
    capture.start("/hide")
    pid = capture.pid
    rc = capture.send()                          # no arguments: shows the existing window
    assert rc == 0
    assert [p.pid for p in apps.processes("Snapnab.exe")] == [pid], "a second process must not stay running"
    win.wait_until(lambda: win.is_visible(capture.hwnd), what="first instance to show its window")


def test_help_shows_command_line_usage(capture):
    capture.start("/hide")
    p = apps.run(apps.CAPTURE_EXE, "/help", wait=False)
    box = win.wait_until(lambda: next((h for h in capture.popups("#32770")), 0), what="help message box")
    assert "/preset" in "".join(win.title(c) for c in win.children(box)), "usage text should list the switches"
    win.post_command(box, 1)
    p.wait(10)


def test_exit_command_closes_capturer(capture, logs):
    capture.start("/hide")
    capture.exit()
    assert not capture.running
    logs.capture.wait(r"Snapnab exited|exited")


# ---------- Editor ↔ capturer ----------

@pytest.mark.smoke
def test_editor_starts_capturer_in_tray(editor):
    editor.start()
    cap = apps.Capture()
    win.wait_until(lambda: cap.running and cap.hwnd, 15, what="Editor to start Snapnab.exe")
    assert not win.is_visible(cap.hwnd), "the capturer started by the Editor stays in the tray (/hide)"


def test_open_file_imports_to_library_and_notifies_capturer(editor, logs, lib, png):
    editor.start()
    win.wait_until(lambda: apps.Capture().running, 15, what="capturer")
    logs.mark()
    editor.open(png)
    new = lib.wait_new("*.snap")
    doc = library.dump(new[-1])
    assert library.size(doc) == (640, 360)
    logs.editor.wait(ipc_sent(LIBRARY_DOC_OPENED, 0))
    logs.capture.wait(ipc_received(LIBRARY_DOC_OPENED))
    assert "e2e-input" in editor.title()


def test_second_editor_instance_forwards_open(editor, png, tmp_path):
    editor.start()
    pid = editor.pid
    other = tmp_path / "second.png"
    Image.new("RGB", (300, 200), (200, 60, 60)).save(other)
    editor.open(other)
    win.wait_until(lambda: "second" in editor.title(), what="document opened in the running Editor")
    assert [p.pid for p in apps.processes("SnapnabEditor.exe")] == [pid]


def test_editor_close_notifies_capturer_and_autosaves(editor, logs):
    editor.start()
    win.wait_until(lambda: apps.Capture().running, 15, what="capturer")
    logs.mark()
    editor.exit()
    logs.editor.wait(ipc_sent(EDITOR_CLOSED, 0))
    logs.capture.wait(ipc_received(EDITOR_CLOSED))
    logs.capture.wait("Editor closed")


def test_capture_window_opens_editor(capture, logs):
    capture.start("/hide")
    capture.command(CaptureCmd.IDM_OPEN_EDITOR)
    ed = apps.Editor()
    win.wait_until(lambda: win.is_visible(ed.hwnd), 15, what="Editor launched by the capture window")
    # with the Editor already open, it is activated through IPC 0x06
    logs.mark()
    capture.command(CaptureCmd.IDM_OPEN_EDITOR)
    logs.capture.wait(ipc_sent(SHOW_EDITOR, 1))
    logs.editor.wait(ipc_received(SHOW_EDITOR))


def test_editor_new_capture_goes_through_capturer(editor, logs):
    editor.start()
    win.wait_until(lambda: apps.Capture().running, 15, what="capturer")
    logs.mark()
    editor.command(EditorCmd.M_NEW_CAPTURE)
    logs.editor.wait(ipc_sent(EDITOR_START_CAPTURE, 0))
    logs.capture.wait(ipc_received(EDITOR_START_CAPTURE))
    logs.capture.wait(state("Idle", "Activated"))
    overlay = win.wait_window("SnapnabSelectWindowClass")
    win.foreground(overlay)
    win.keys("{ESC}")
    logs.capture.wait(r"CaptureEngine: \w+ → Idle")
    win.wait_no_window("SnapnabSelectWindowClass")


def test_capture_from_clipboard_opens_in_editor(capture, logs, lib):
    presets.write(presets.preset("e2e clipboard", "Region"))      # no effects: the size must stay the clipboard's
    capture.start("/hide")
    win.set_clipboard_image(Image.new("RGB", (333, 222), (10, 200, 90)))
    logs.mark()
    capture.send("/clipboard")
    new = lib.wait_new("*.snap")
    assert library.size(library.dump(new[-1])) == (333, 222)
    logs.capture.wait(state("HaveInput", "Outputting"))
    ed = apps.Editor()
    win.wait_until(lambda: win.is_visible(ed.hwnd), 15, what="Editor showing the capture")


def test_clipboard_monitor_sends_new_image_to_editor(editor, logs, lib):
    regs.set_dword("ClipboardMonitor", 2)        # 2 = images copied by any program
    editor.start()
    win.wait_until(lambda: apps.Capture().running, 15, what="capturer")
    apps.wait_quiet(1)
    logs.mark()
    win.set_clipboard_image(Image.new("RGB", (250, 180), (240, 200, 20)))
    logs.capture.wait(r"Clipboard: image detected")
    logs.capture.wait(ipc_sent(NEW_IMAGE_FROM_CLIPBOARD, 1))
    logs.editor.wait(ipc_received(NEW_IMAGE_FROM_CLIPBOARD))
    new = lib.wait_new("*.snap")
    assert library.size(library.dump(new[-1])) == (250, 180)


def test_clipboard_monitor_default_ignores_other_programs(editor, logs, lib):
    regs.set_dword("ClipboardMonitor", 1)        # 1 = only the Snipping Tool
    editor.start()
    win.wait_until(lambda: apps.Capture().running, 15, what="capturer")
    apps.wait_quiet(1)
    logs.mark()
    win.set_clipboard_image(Image.new("RGB", (120, 90), (0, 0, 0)))
    apps.wait_quiet(2)
    logs.capture.assert_absent(r"Clipboard: image detected")
    assert not lib.new("*.snap")


def test_capture_notifies_editor_library_changed(editor, logs):
    editor.start()
    win.wait_until(lambda: apps.Capture().running, 15, what="capturer")
    win.set_clipboard_image(Image.new("RGB", (100, 80), (90, 90, 90)))
    logs.mark()
    apps.Capture().send("/clipboard")
    logs.capture.wait(ipc_sent(LIBRARY_CHANGED, 1))
    logs.editor.wait(ipc_received(LIBRARY_CHANGED))
