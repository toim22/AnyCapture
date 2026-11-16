# AnyCapture E2E tests

End-to-end tests that drive the real `AnyCapture.exe` and `AnyCaptureEditor.exe` the way a user does — command line,
menus, the capture window's controls, the crosshair, the recording HUD and the Editor canvas with the real mouse — and
check what the programs produce. Python + pytest + pywinauto, following the `windows-desktop-e2e` skill.

## Running

Close AnyCapture and the Editor first (the suite refuses to start while they run), then, in PowerShell:

```powershell
cd tests\e2e
py -m pip install -r requirements.txt
.\run.ps1                       # everything (~15 min)
.\run.ps1 -m smoke              # the critical paths
.\run.ps1 tests\test_06_editor_tools.py -k text
```

`run.ps1` calls `python.exe` directly. The `py` launcher of the Python Install Manager is an MSIX package and the
programs it starts write to a **virtualized** `%LOCALAPPDATA%` and `HKCU`, so the tests would neither see nor test the
real data; `conftest.py` stops the run if it detects that.

Don't touch the mouse or keyboard while it runs: the tests draw, click and type, and use the clipboard.

### In Windows Sandbox (experimental)

`sandbox\run-sandbox.ps1` runs the suite inside Windows Sandbox, a throwaway Windows desktop with its own mouse,
keyboard, clipboard and screen, so the computer stays free while it runs (leave the Sandbox window open, not
minimized: it is the tests' screen). It needs the *Windows Sandbox* feature (Windows Pro or Enterprise):

```powershell
.\sandbox\run-sandbox.ps1                  # everything (~27 min)
.\sandbox\run-sandbox.ps1 -m smoke         # the same pytest arguments as run.ps1
```

It tests `ANYCAPTURE_DIR` (default `dist\AnyCapture`) with this computer's Python and packages, mapped read-only, plus
its Visual C++ runtime, which the sandbox lacks and pywin32 needs. Inside, `sandbox\inside.ps1` turns off what a fresh
Windows does that a user's desktop usually doesn't (Print Screen opening the Snipping Tool, notification pop-ups),
runs pytest and copies the report, the failure screenshots and the programs' logs to `artifacts\sandbox-<date-time>\`.
Your own data is never touched: the sandbox has its own profile and is thrown away at the end.

Not yet a full replacement for a run on the desktop: of 242 tests, about 20 still fail there. The Save dialog ignores
the path the tests type (Save as, export), Paint, a printer and the OCR language are missing, and a few tests assume a
bigger screen than the sandbox's (about 1520×823). The webcam and microphone are off.

| Variable | Default | |
|---|---|---|
| `ANYCAPTURE_DIR` | `C:\Program Files\AnyCapture` | the program under test (e.g. `dist\AnyCapture` for a fresh build) |
| `ANYCAPTURE_TOOL_EXE` | `rust\target\release\AnyCaptureTool.exe` | `dump file.snap` and `audio file.mp4` |
| `ANYCAPTURE_UPLOADERS_FILE` | (set by `test_11_share.py`) | the upload settings the app reads instead of `Uploaders.xml`: the upload tests send to a fake server on 127.0.0.1 and never touch the user's destinations |

If a run is killed before it puts your data back, its backup stays in `artifacts/backup-*` and the next run refuses to
start until it is restored (see the backup's `README.txt`).

Results: `artifacts/report.html`, plus a full-screen screenshot of each failure (`artifacts/FAIL_*.png`).

## Your data

The programs find their folders with `SHGetKnownFolderPath`, which cannot be redirected, so the tests run against the
real profile. Before the first test the suite copies `%LOCALAPPDATA%\AnyCapture` and exports `HKCU\Software\AnyCapture`
to `artifacts/backup-*`; at the end it puts both back, deletes what the captures saved in `Pictures\AnyCapture` during the
run, checks the folder file by file against the backup and only then removes the backup. The logs are kept (they are
the evidence of the run). Side effects outside that: the clipboard is restored as text only, and
`test_delete_document_asks_and_recycles` sends one test capture to the Recycle Bin.

## How results are checked

| Oracle | Used for |
|---|---|
| `Logs\AnyCapture.log`, `Logs\AnyCaptureEditor.log` | IPC between the two programs (`IPC: sent/received code=0x..`), capture engine states, recorder, hotkeys |
| `.snap` XML through `AnyCaptureTool.exe dump` | Editor objects, sizes, tags, groups, names |
| Copy all (composed image on the clipboard) | raster tools and effects, pixel by pixel |
| Saved files | output formats (magic bytes, size, DPI), MP4 duration and frame size (Windows property system), GIF frames, audio track |

Controls are reached by control ID and commands by `WM_COMMAND` with the program's own IDs (`anycapture_e2e/apps.py`
mirrors the command IDs of `rust/anycapture/src/lib.rs` (`id`) and `rust/snapedit/src/ids.rs`); if they change, update that file.

## Layout

| File | Covers |
|---|---|
| `tests/test_00_rust_tests.py` | runs `cargo test --workspace` in `rust/` |
| `tests/test_01_lifecycle_ipc.py` | single instance, command line, IPC 0x02/0x06/0x0C/0x12/0x13/0x1E/0x31, clipboard monitor |
| `tests/test_02_capture.py` | every selection mode, delay, repeat, windows as objects, hotkeys, outputs (Editor, clipboard, files, program, OCR) |
| `tests/test_03_capture_effects.py` | the 12 capture effects, alone and together |
| `tests/test_04_video.py` | recording, pause, restart, cancel, snapshot, hotkeys, system audio, Editor output |
| `tests/test_05_capture_window.py` | tabs, switches, combos, presets (new, rename, duplicate, delete, hotkey, export/import), Options, settings export/import, One Click, About, folders |
| `tests/test_06_editor_tools.py` | every drawing tool, move, undo/redo |
| `tests/test_07_editor_commands.py` | geometry, effects panel, arrange/group/align/lock, clipboard, OCR, documents, view toggles, print |
| `tests/test_08_editor_files_video.py` | Save as in every format, open, video frame/cut/MP4/GIF/play |

Not covered: tray icon clicks, One Click panel interaction, links captured from browsers, panoramic scrolling, menu
capture with an open menu, webcam, microphone, e-mail and tray drag and drop.
