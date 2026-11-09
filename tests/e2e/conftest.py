import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
# the programs started by the tests delete library items for real instead of sending them to the Recycle Bin
os.environ["ANYCAPTURE_NO_RECYCLE"] = "1"
# and leave alone what the program's former name left on this computer (snapcore/src/legacy.rs): the backup and
# restore below only cover the AnyCapture data, so a test run must never move the user's older data into it
os.environ["ANYCAPTURE_NO_MIGRATE"] = "1"

from anycapture_e2e import apps, win  # noqa: E402  (win sets the DPI awareness first)
from anycapture_e2e.config import ARTIFACTS, CAPTURE_EXE, EDITOR_EXE, LEGACY_EXES  # noqa: E402
from anycapture_e2e.library import LibraryWatcher  # noqa: E402
from anycapture_e2e.logs import Logs  # noqa: E402
from anycapture_e2e.state import UserState  # noqa: E402
from anycapture_e2e.target import TargetWindow  # noqa: E402


def _package_identity():
    """Name of the MSIX package this process runs in, or None. The Python Install Manager's `py` is packaged, and the
    programs it starts inherit the package: their %LOCALAPPDATA% and HKCU writes go to a private virtual copy, so the
    tests would not see (nor test) the real data and logs."""
    import ctypes
    n = ctypes.c_uint32(0)
    rc = ctypes.windll.kernel32.GetCurrentPackageFullName(ctypes.byref(n), None)
    if rc == 15700:   # APPMODEL_ERROR_NO_PACKAGE
        return None
    buf = ctypes.create_unicode_buffer(n.value)
    ctypes.windll.kernel32.GetCurrentPackageFullName(ctypes.byref(n), buf)
    return buf.value


@pytest.fixture(scope="session", autouse=True)
def user_state():
    """Backs up the user's captures, presets and settings and puts them back after the run."""
    pkg = _package_identity()
    if pkg:
        pytest.exit(f"This Python runs inside the {pkg} package (the 'py' of the Python Install Manager): AnyCapture would "
                    f"see a virtualized copy of %LOCALAPPDATA% and of the registry. Use run.ps1 or call python.exe "
                    f"directly: {sys.base_prefix}\\python.exe -m pytest", returncode=4)
    assert CAPTURE_EXE.exists() and EDITOR_EXE.exists(), f"AnyCapture not found in {CAPTURE_EXE.parent} (set ANYCAPTURE_DIR)"
    if apps.processes("AnyCapture.exe") or apps.processes("AnyCaptureEditor.exe"):
        pytest.exit("Close AnyCapture and the AnyCapture Editor before running the tests (the tests close and reopen them "
                    "as they go).", returncode=3)
    if any(apps.processes(n) for n in LEGACY_EXES):
        # an install made under the program's former name holds the same hotkeys and watches the clipboard too
        pytest.exit(f"Close {' and '.join(LEGACY_EXES)} (an earlier install under the program's former name) before "
                    f"running the tests: it holds the same hotkeys and watches the clipboard.", returncode=3)
    ARTIFACTS.mkdir(exist_ok=True)
    stale = sorted(ARTIFACTS.glob("backup-*"))
    if stale:
        # a run that was killed before its restore: the data folder now holds test data, and a new backup would
        # save that instead of the user's own state
        pytest.exit(f"A backup that was never restored is in {stale[0]}: put it back first (see its README.txt), then "
                    f"delete or move the folder.", returncode=5)
    st = UserState()
    st.backup()
    clip = None
    try:
        clip = win.clipboard_text()
    except Exception:
        pass
    yield st
    apps.kill_all()
    st.restore()
    if clip is not None:
        win.set_clipboard_text(clip)


@pytest.fixture
def logs():
    return Logs()


@pytest.fixture
def lib():
    return LibraryWatcher()


@pytest.fixture
def capture(request):
    apps.kill_all()
    c = apps.Capture()
    yield c
    _screenshot_on_failure(request)
    apps.kill_all()


@pytest.fixture
def editor(request):
    apps.kill_all()
    e = apps.Editor()
    yield e
    _screenshot_on_failure(request)
    apps.kill_all()


@pytest.fixture
def target():
    t = TargetWindow()
    yield t
    t.close()


def _screenshot_on_failure(request):
    rep = getattr(request.node, "rep_call", None)
    if rep is not None and rep.failed:
        try:
            from PIL import ImageGrab
            ImageGrab.grab(all_screens=True).save(ARTIFACTS / f"FAIL_{request.node.name}_{int(time.time())}.png")
        except Exception:
            pass


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    setattr(item, f"rep_{outcome.get_result().when}", outcome.get_result())


@pytest.fixture(autouse=True)
def _escape_leftovers():
    """A failed test can leave the crosshair or a form open, which would swallow the next test's input."""
    yield
    for cls in ("AnyCaptureSelectWindowClass", "AnyCaptureForm", "AnyCaptureTextWnd"):
        for h in win.top_windows(cls):
            win.close_window(h)
