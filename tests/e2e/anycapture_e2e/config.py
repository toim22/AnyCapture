"""Paths and timeouts for the E2E suite (overridable through environment variables)."""
import os
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
E2E_DIR = Path(__file__).resolve().parents[1]

# The installed program is tested by default; ANYCAPTURE_DIR=dist\AnyCapture tests a fresh build instead
APP_DIR = Path(os.environ.get("ANYCAPTURE_DIR", r"C:\Program Files\AnyCapture"))
CAPTURE_EXE = APP_DIR / "AnyCapture.exe"
EDITOR_EXE = APP_DIR / "AnyCaptureEditor.exe"
# the programs of an install made under the program's former name (snapcore/src/legacy.rs), which must not run
# during the tests
LEGACY_EXES = ("Snapnab.exe", "SnapnabEditor.exe")
# AnyCaptureTool.exe (rust/snaptool): "dump file.snap" prints the document XML, the oracle of the Editor tests, and
# "audio file.mp4" reads a recording's audio track
TOOL_EXE = Path(os.environ.get("ANYCAPTURE_TOOL_EXE", REPO / "rust" / "target" / "release" / "AnyCaptureTool.exe"))
RUST_DIR = REPO / "rust"

LOCALAPPDATA = Path(os.environ["LOCALAPPDATA"])
DATA_DIR = LOCALAPPDATA / "AnyCapture"
LIBRARY_DIR = DATA_DIR / "Library"
LOGS_DIR = DATA_DIR / "Logs"
CAPTURE_LOG = LOGS_DIR / "AnyCapture.log"
EDITOR_LOG = LOGS_DIR / "AnyCaptureEditor.log"
PICTURES_DIR = Path(os.environ["USERPROFILE"]) / "Pictures" / "AnyCapture"
REG_KEY = r"HKCU\Software\AnyCapture"

ARTIFACTS = E2E_DIR / "artifacts"
LAUNCH_TIMEOUT = float(os.environ.get("ANYCAPTURE_LAUNCH_TIMEOUT", "20"))
ACTION_TIMEOUT = float(os.environ.get("ANYCAPTURE_ACTION_TIMEOUT", "10"))

# window classes and single-instance mutexes (rust/snapcore/src/ipc.rs)
CAPTURE_CLASS = "AnyCaptureUI"
EDITOR_CLASS = "AnyCaptureEditor"
CAPTURE_MUTEX = "AnyCapture-Capture-{41F1BF9C-5A4E-4B6F-9C33-1D2E3F405A61}"
EDITOR_MUTEX = "AnyCapture-Editor-{800F2EB3-7C1D-4E2A-A3B4-C5D6E7F80912}"
