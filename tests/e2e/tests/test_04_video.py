"""Video recording (Recorder + HUD, comp_3): MP4 H.264, pause without a gap, restart, cancel, snapshot during the
recording, audio track, hotkeys and Editor output."""
import re
import time

import pytest

from anycapture_e2e import apps, library, presets, win
from anycapture_e2e import video as v
from anycapture_e2e.logs import state

pytestmark = pytest.mark.video
TDM_CLICK_BUTTON = 0x0400 + 102


def start_with(capture, *xml):
    presets.write(*xml)
    capture.start("/hide")


@pytest.mark.smoke
def test_record_stop_produces_mp4(capture, logs, lib):
    start_with(capture, v.video_preset(fps=15))
    hud = v.arm(capture)
    logs.capture.wait(state(r"\w+", "InputSelected"))
    v.start(hud, logs)
    time.sleep(3)
    v.stop(hud, logs)
    mp4 = lib.wait_new("Video_*.mp4", timeout=20)[-1]
    win.wait_no_window(v.HUD)
    logs.capture.wait(r"CaptureEngine: Outputting → Idle")
    assert mp4.read_bytes()[4:8] == b"ftyp", "MP4 container"
    d = v.duration(mp4)
    assert 2.0 <= d <= 5.0, f"duration {d:.2f}s for ~3 s of recording"
    w, h = v.frame_size(mp4)
    sw, sh = win.virtual_screen_size()                              # the full-screen preset covers every monitor
    assert h <= 1080 and abs(w / h - sw / sh) < 0.02, (w, h)       # downscaled to 1080p at most, same aspect


def test_pause_is_not_in_the_duration(capture, logs, lib):
    start_with(capture, v.video_preset(fps=15))
    hud = v.arm(capture)
    v.start(hud, logs)
    time.sleep(2)
    v.click(hud, "record")                       # pause
    logs.capture.wait(state("Capturing", "Suspended"))
    time.sleep(3)
    v.click(hud, "record")                       # resume
    logs.capture.wait(state("Suspended", "Capturing"))
    time.sleep(2)
    v.stop(hud, logs)
    d = v.duration(lib.wait_new("Video_*.mp4", timeout=20)[-1])
    assert 3.0 <= d <= 5.5, f"{d:.2f}s: the 3 s pause must not be in the video"


def test_restart_discards_what_was_recorded(capture, logs, lib):
    start_with(capture, v.video_preset(fps=15))
    hud = v.arm(capture)
    v.start(hud, logs)
    time.sleep(3)
    v.click(hud, "restart")                      # asks first (C33), then cancels and counts down again
    td = win.wait_until(lambda: [h for h in capture.popups("#32770")], what="restart confirmation")[0]
    win.send(td, TDM_CLICK_BUTTON, 6)            # IDYES
    logs.capture.wait(r"Recorder: canceled")
    win.wait_window("AnyCaptureCountdown", 5)
    v.click(hud, "record")
    logs.capture.wait(r"Recorder: started", 15, count=2)
    time.sleep(1.5)
    v.stop(hud, logs)
    d = v.duration(lib.wait_new("Video_*.mp4", timeout=20)[-1])
    assert d < 3.0, f"{d:.2f}s: the part before the restart must be gone"


def test_cancel_button_discards_the_recording(capture, logs, lib):
    start_with(capture, v.video_preset(fps=15))
    hud = v.arm(capture)
    v.start(hud, logs)
    time.sleep(1.5)
    v.click(hud, "cancel")
    win.wait_no_window(v.HUD)
    logs.capture.wait(r"Recorder: canceled")
    apps.wait_quiet(1)
    assert not lib.new("Video_*.mp4")


def test_snapshot_during_recording(capture, logs, lib):
    start_with(capture, v.video_preset(fps=15))
    hud = v.arm(capture)
    v.start(hud, logs)
    time.sleep(1)
    v.click(hud, "snapshot")
    snap = lib.wait_new("*.snap")[-1]
    assert library.size(library.dump(snap)) != (0, 0)
    v.stop(hud, logs)
    lib.wait_new("Video_*.mp4", timeout=20)


def test_close_hud_before_start_cancels(capture, logs, lib):
    start_with(capture, v.video_preset())
    hud = v.arm(capture)
    v.click(hud, "cancel")
    win.wait_no_window(v.HUD)
    logs.capture.wait(r"→ Idle")
    apps.wait_quiet(1)
    assert not lib.new("Video_*.mp4")


def test_video_hotkey_opens_the_hud(capture, logs):
    start_with(capture, v.video_preset())
    logs.capture.wait(r"Hotkey registered: id 2")
    win.keys("+{PRTSC}")
    hud = win.wait_window(v.HUD, 10)
    v.click(hud, "cancel")
    win.wait_no_window(v.HUD)


def test_pause_and_stop_hotkeys(capture, logs, lib):
    """Shift+F9 starts/pauses and Shift+F10 stops (registered only while the HUD is open)."""
    start_with(capture, v.video_preset(fps=15))
    hud = v.arm(capture)
    # taken by another program is fine as long as the keyboard hook serves the key while the HUD is open
    failed = [l for l in logs.capture.find(r"Hotkey Shift\+F(9|10): RegisterHotKey failed") if "keyboard hook" not in l]
    if failed:
        v.click(hud, "cancel")
        raise AssertionError(
            "the recording hotkeys could not be registered (another program owns them) and AnyCapture has no fallback "
            "for them nor tells the user — only the HUD buttons work:\n" + "\n".join(failed))
    win.keys("+{F9}")
    win.wait_window("AnyCaptureCountdown", 5)
    win.keys("+{F9}")
    logs.capture.wait(r"Recorder: started", 15)
    time.sleep(1.5)
    win.keys("+{F10}")
    logs.capture.wait(r"Recorder: stopped \(ok\)", 30)
    lib.wait_new("Video_*.mp4", timeout=20)


def test_system_audio_track(capture, logs, lib):
    start_with(capture, v.video_preset(sys_audio=True, fps=15))
    hud = v.arm(capture)
    v.start(hud, logs)
    line = logs.capture.find(r"Recorder: started")[-1]
    if "sys=0" in line:
        v.click(hud, "cancel")
        pytest.skip("no audio output device for the loopback capture")
    import winsound
    winsound.Beep(880, 600)
    time.sleep(1.5)
    v.stop(hud, logs)
    mp4 = lib.wait_new("Video_*.mp4", timeout=20)[-1]
    rep = v.audio_report(mp4)
    m = re.search(r"EOS after (\d+) samples", rep)
    assert m and int(m.group(1)) > 0, f"no audio samples:\n{rep}"


def test_video_opens_in_editor(capture, logs):
    start_with(capture, v.video_preset(editor=True, fps=15))
    mp4 = v.record(capture, logs, 2)
    ed = apps.Editor()                           # not running: the capturer launches it with /Open "<mp4>"
    win.wait_until(lambda: win.is_visible(ed.hwnd), 15, what="Editor with the video")
    win.wait_until(lambda: mp4.stem in ed.title(), 10, what="video as the current document")
