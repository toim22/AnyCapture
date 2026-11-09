# Building

## Requirements

- Windows 10 2004+ or Windows 11, x64
- [Rust](https://rustup.rs) (stable, `x86_64-pc-windows-msvc`, the rustup default on Windows)
- The Visual Studio 2022+ C++ build tools (the *Desktop development with C++* workload or the *Build Tools for Visual
  Studio*): the MSVC linker, `rc.exe` and the Windows SDK. Cargo finds them by itself.
- To build the installer: [Inno Setup 6.7+](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`).
  Without it, `build.bat` produces only the folder and the zip.

Almost everything uses Windows APIs (Win32, GDI+, WIC, Media Foundation, WASAPI, Windows.Media.Ocr, WinHTTP, Direct3D
through wgpu). Cargo downloads the crates pinned in `rust/Cargo.lock` on the first build, so that step needs internet
access. The C runtime is linked statically (`rust/.cargo/config.toml`): the programs run without the Visual C++
Redistributable. The licenses of what is compiled in are in `THIRD-PARTY-NOTICES.txt`, installed next to the programs.

## Command line

```bat
build.bat            :: Release + product package in dist\
build.bat test       :: Release + the workspace tests (cargo test)
build.bat debug      :: Debug in rust\target\debug
build.bat clean      :: deletes rust\target and dist\
```

Cargo works directly too:

```bat
cargo build --release --workspace --manifest-path rust\Cargo.toml
cargo test --release --workspace --manifest-path rust\Cargo.toml
```

The end-to-end suite (`tests/e2e`, Python, pywinauto) drives the real programs; see `tests/e2e/README.md`.

## Editors

Open the repository folder in VS Code (with rust-analyzer), RustRover or Visual Studio. To debug, run
`rust\target\debug\AnyCapture.exe` or `AnyCaptureEditor.exe` from the debugger.

## Where things go

| Folder | What | Committed? |
|---|---|---|
| `rust/target/` | Cargo build tree; `release/` has `AnyCapture.exe`, `AnyCaptureEditor.exe`, `SnapEx64.dll` and `AnyCaptureTool.exe` | no |
| `dist/AnyCapture/` | the program as the user gets it (`AnyCapture.exe`, `AnyCaptureEditor.exe`, `SnapEx64.dll`, `lang/`, `help/`, `README.txt`) | no |
| `dist/AnyCapture-<version>-win-x64.zip` | portable package for distribution (for example, attached to a GitHub *release*) | no |
| `dist/AnyCapture-<version>-setup.exe` | installer (built when Inno Setup is installed) | no |
| `dist/symbols/` | Release PDBs, to analyze crashes; not included in the package | no |

`AnyCaptureTool.exe` is a helper for the tests and for debugging (`dump file.snap` prints a capture's document XML,
`audio file.mp4` reads a recording's audio track); it is not part of the package.

## Installer

`packaging/installer/AnyCapture.iss` is an [Inno Setup 6](https://jrsoftware.org/isinfo.php) script that packages
`dist\AnyCapture\` into an installer (`dist\AnyCapture-<version>-setup.exe`). `build.bat` compiles it automatically when it
finds `ISCC.exe` (in `%LOCALAPPDATA%\Programs`, `Program Files` or on the `PATH`), passing the version from
`rust\Cargo.toml`. To compile it by hand after `build.bat`:

```bat
ISCC.exe /DAppVersion=0.5.0 packaging\installer\AnyCapture.iss
```

What the installer does:

- asks whether to install **just for the current user** (default, no administrator prompt, in
  `%LOCALAPPDATA%\Programs\AnyCapture`) or **for all users** (in *Program Files*, asks for administrator rights); if, in
  per-user mode, the user picks a folder they cannot write to (such as *Program Files*), it offers to restart as
  administrator with that folder;
- the wizard comes in the same 12 languages as the program, chosen from the Windows language: 7 official Inno Setup
  translations and 5 in `packaging/installer/Languages/` (Chinese, Hindi, Bengali, Indonesian and Urdu); it follows
  the Windows light/dark theme in Inno Setup's Windows 11 style, with a Welcome page and the AnyCapture pictures
  (`WizardLarge-*.png`, `WizardLarge-dark-*.png` and `WizardSmall-*.png`, one per display scale, drawn from the app
  icon by `python packaging/installer/wizard_images.py`);
- Start menu shortcuts (AnyCapture and AnyCapture Editor), an optional desktop shortcut and "start with Windows"
  (the same value that *Options › Start with Windows* writes);
- closes AnyCapture if it is running, and on an update installs over the previous version keeping the settings: in
  the same folder and mode (all users or just this one) as before, with no question; installing for all users also
  removes a copy that was installed only for this user;
- uninstalls from *Settings › Apps*; captures, presets and settings stay in the user profile.

The executable is not digitally signed, so SmartScreen may warn on the first run until the file gains
reputation; to sign it, use `SignTool` from the Windows SDK with a code signing certificate.

## Version

The version lives only in `[workspace.package] version` in `rust/Cargo.toml`: the executables' version resources,
`build.bat`, the package name and the installer read it from there. When changing it, update `CHANGELOG.md` too.

## Icons and resources

The artwork is in `rust/resources/icons/`: `capture.svg` (AnyCapture) and `editor.svg` (the Editor), plus a `-small`
version of each for 16 to 32 pixels, without the glow and with thicker frame corners, so the tray and the small
Explorer views stay sharp. `cargo run --release --manifest-path scripts/make_icons/Cargo.toml` renders them (resvg)
into `rust/resources/AnyCapture.ico` and `AnyCaptureEditor.ico`; run it, then `wizard_images.py`, when the artwork
changes. The colored icon is for Windows (taskbar, Start, Explorer, the tray, the installer); inside the windows the
title bar draws a one-color mark in the text color (`snapui/src/caption.rs`). Each executable's
icon, manifest (per-monitor DPI v2, Common Controls 6, asInvoker) and version information come from
`rust/resources/app.rc.in`, filled and compiled by the crates' `build.rs` (`embed-resource`, which runs `rc.exe`).
