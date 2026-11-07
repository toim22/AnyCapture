# Architecture

Snapnab is a C++20/Win32 (x64) implementation built from reference architecture and pseudocode documents that are
not part of this repository (the `§N`, `comp_N` and `ed_comp_N` citations in the code point to them). It consists of
**two cooperating executables** plus a DLL:

| Binary | Equivalent of | Role |
|---|---|---|
| `Snapnab.exe` | `Snagit32.exe` | tray, capture window, presets, global hotkeys, capture engine, video recording, outputs |
| `SnapnabEditor.exe` | `SnagitEditor.exe` | image editing (vector objects + raster), effects, OCR, PDF, video and GIF editing, library |
| `SnapEx64.dll` | `SnagEx64.dll` | extended window capture (`WH_CALLWNDPROC` hook + `WM_GETMINMAXINFO` 19500×19500) |

## Source code

| Folder | Contents |
|---|---|
| `src/common/` | shared static library: image (WIC/GDI+), XML, IPC, `.snap` format, capture library (with capture names), effects, computer vision, GIF, OCR, settings backup (`Settings.*`), forms, icons and the UI shared by both programs: title bar with menus (`CaptionBar.*`), accent-derived colors (`Theme.*`) and modern controls (`Widgets.*`) |
| `src/capture/` | `Snapnab.exe`: capture window (`main.cpp`), presets, hotkeys, crosshair (`SelectOverlay.*`), capture engine (with each window as an object), links of the captured area (`Links.*`, UI Automation), panoramic scrolling (`Panoramic.*`), video recorder and HUD, webcam (`Webcam.*`) and One Click (`OneClick.*`) |
| `src/editor/` | `SnapnabEditor.exe`: document and undo, object rendering, canvas and tools, panels (properties, effects, layers, tray), clipboard formats (`Clip.cpp`), video, PDF, OCR |
| `src/snapex/` | `SnapEx64.dll` |
| `rust/` | Cargo workspace of the C++ to Rust migration (one phase at a time, each behind the unchanged e2e suite). `snapcore/`: static library linked into both programs by Corrosion, with a C ABI in `src/ffi.rs` / `include/snapcore.h`: the `.snap` documents (`snap.rs`, over the compound file container of `container.rs`), the capture library (`library.rs`), the animated GIF writer (`gif.rs`), the log, the registry settings, the interface language (catalog and `.po` reader), the XML reader and writer and the image files (`codec.rs`: PNG, JPEG, BMP, GIF, TIFF, AVIF, HEIC through WIC, with metadata and EXIF orientation, and WebP with libwebp 1.5.0 from the `libwebp-sys` crate), the image effects and capture preset effects (`fx.rs`), Beautify (`beautify.rs`), image scaling and the GDI+ flat API (`gdip.rs`), computer vision (`cv.rs`), the PDF writer and page breaks (`pdf.rs`), the color texts (`color.rs`), HDR capture (`hdr.rs`: DXGI duplication and tone mapping), OCR and face detection (`ocr.rs`, Windows.Media.Ocr and FaceAnalysis through WinRT), QR codes and barcodes (`barcode.rs`, with the `rxing` crate), HTTPS, SHA-256/HMAC, JSON and the Credential Manager secrets (`net.rs`), the upload destinations with their settings, sign-in and custom uploader syntax (`upload.rs`), the AI assistant's request (`ai.rs`), the Editor's objects and their painters (`doc.rs`, `render.rs`: shapes, arrows and their ends, texts, callouts, steps, magnifiers, hide areas, shadows and opacity layers) with the stamp images (`stamps.rs`: emoji through Direct2D, labels and cursors), their hit-testing (`doc.rs`), their XML in the `.snap` and on the clipboard and the document's save and load (`serial.rs`), and printf, atoi, atof, strtoul and sscanf through the C runtime (`crt.rs`, so numbers come out and read back as the C++ did them); `Util.cpp`, `I18n.cpp`, `Xml.cpp`, `Image.cpp`, `SnapFile.cpp`, `Library.cpp`, `Gif.cpp`, `Effects.cpp`, `Cv.cpp`, `Beautify.cpp` (presets and dialog stay in C++), `Color.cpp`, `Hdr.cpp`, `Ocr.cpp`, `Barcode.cpp` (the results dialog stays), `Net.cpp`, `Upload.cpp` (the settings dialogs and the sign-in wait stay), `Emoji.cpp`, `editor/Render.cpp`, `editor/Serialize.cpp`, `editor/Cursors.cpp` and `editor/Pdf.cpp` keep their C++ API as thin wrappers (`SnapcoreBridge.h` converts `Image` and `XmlNode`, `editor/ObjBridge.h` the objects both ways); `Document.cpp` keeps the undo history and the moves until the Editor's interface moves. `snapui/`: the new interface in egui 0.37 (immediate mode, drawn by wgpu, accessible through AccessKit, the stack of the PhotoCraft editor), tested with `egui_kittest` against images in `tests/snapshots` (the first one: the Windows fonts and the text of every shipped script, Arabic and Urdu right to left, Hindi and Bengali shaped); `host.rs` runs the windows itself on winit + egui-winit + egui-wgpu (one GPU device, a surface, renderer and egui context per window) instead of eframe, so each window keeps its Win32 class and a window subclass hands raw messages (WM_COPYDATA, WM_COMMAND, tray clicks, hotkeys) to the app. `snapnab/`: `Snapnab.exe` in Rust, built beside the C++ one until it does everything (phase 5): single instance and the forwarded command line (`snapcore/src/instance.rs`), the IPC channel (`snapcore/src/ipc.rs`, same kernel objects as `Ipc.cpp`), the presets (`snapcore/src/presets.rs`, the same `Presets1.xml` as the C++ app, checked against `tests/data/presets-cpp.txt`), the tray icon and menu, the command line switches, the capture window, the global hotkeys with their low-level keyboard hook fallback (`hotkeys.rs`), the clipboard monitor, and the capture engine (`engine.rs`: delay, region, window, object, fixed, freehand and multi-area selection, full screen, monitor, clipboard and file inputs, windows as objects, the capture effects, the library, clipboard, file and program outputs and the OCR text window) with the selection overlay in egui (`select.rs`, tested with kittest in `tests/select.rs`), and video recording (`engine/video.rs`: the HUD in egui, `hud.rs`, with its volume panel; the red frame, the countdown and the button tooltips as plain Win32 windows, `rawwin.rs`; the restart question, the snapshot, the overlay menu and the recording hotkeys, registered only while the HUD is up), the scrolling window, extended window and panoramic captures (`engine/scroll.rs`: the first two on their own thread, the panoramic bar in egui, the new size asked in a code-built form, `form.rs`, with the C++ window class and control IDs); the recorder itself (`snapcore/src/recorder.rs`: Media Foundation H.264/AAC in a fragmented MP4 remuxed at the end, WASAPI microphone and loopback mixed at 48 kHz with automatic gain, the cursor highlight, clicks, keys and zoom drawn into the frames, and the recovery of interrupted recordings), the screen, window and cursor grabs (`snapcore/src/screen.rs`), the clipboard formats (`clip.rs`) and the capture sounds (`sounds.rs`) and the panoramic stitching (`panorama.rs`) are in snapcore; `snapex/` builds `SnapEx64.dll` in Rust (the WH_CALLWNDPROC hook that lets a window grow beyond the screen, with its messages in a section shared by every process); the vector icons are in `snapui/src/icons.rs`; `build.rs` embeds `src/resources/app.manifest`; the webcam (`snapcore/src/webcam.rs`: Media Foundation source reader, RGB32 frames on their own thread, the chroma key) shown in a layered bubble (`camwin.rs`); pinned images (`pins.rs`: one layered Win32 window per pin drawn with GDI+, its state behind `GWLP_USERDATA`, the quick annotation with the Editor's own objects, the history in `Pins\`), the QR results task dialog (`qr.rs`), the quick access overlay (`quick.rs`) and One Click (`oneclick.rs`), the color picker, the screen focus and the clean desktop (`desk.rs`) and the interval capture (`interval.rs`, a message-only window with its timer), all raw Win32 windows that send their requests to the app as `UiEv` through the event loop (`pins::send`, a process-wide sink so the forms' own threads can use it too); the automation command line is read in `cmdline.rs` and combined into one capture in `main.rs` as `ProcessCommandLine` did; the upload and Office outputs run on their own threads (`share.rs`), Office through IDispatch late binding in `snapcore/src/office.rs` (`SNAPNAB_UPLOADERS_FILE` points the e2e upload test at its own settings). The Options and Presets dialogs are next |
| `src/resources/` | icons and manifest (DPI PerMonitorV2, Common Controls 6) |
| `tests/` | integration tests (`SnapnabTests.exe`, run by CTest) and the end-to-end UI suite (`tests/e2e`, pytest + pywinauto) |
| `cmake/` | `Dependencies.cmake`: Corrosion through `FetchContent` (pinned, hashed); the Rust crates, libwebp and rxing among them, are pinned in `rust/Cargo.lock` |
| `help/` | the manual and its screenshots (`help/images`, regenerated by `scripts/manual-screenshots.ps1`) |
| `lang/` | interface translations: `<language>.po` (gettext) and the `snapnab.pot` template, generated by `scripts/i18n.py` |

The product version lives only in `project(VERSION …)` in `CMakeLists.txt`; CMake generates `Version.h`, used by the
code and by the executables' `VERSIONINFO` resources.

## How the reference architecture was followed

| Document | Implementation |
|---|---|
| Process model (pseudocode §1–§2) | single instance through a mutex + window (`SnapnabUI` / `SnapnabEditor`); the 2nd instance forwards its command line through `WM_COPYDATA` (`dwData 0x8010`, GUID + size + UTF-16 ≤ 10 000 bytes); the Editor starts the capturer if it is not running |
| `CCaptureEngine` state machine (§3) | `src/capture/CaptureEngine.*`: `Idle → Activated → InputSelecting → InputSelected → Capturing → HaveInput → Outputting`, `Suspended` while the video is paused (all logged) |
| IPC (§4, §19.8, ed_comp_14) | `src/common/Ipc.*`: 4 KiB mapping + mutex + `…Event%d` + ack `…WaitUntilMessageSentEvent%d`; message `[code][kind][payload]` (int / 16 B / UTF-16); waits for the peer 75×200 ms; 10 s ack only if the peer is not hung (`IsHungAppWindow`). Codes: `0x06` show, `0x0A` close if hidden, `0x0C` reload, `0x1E` new image from clipboard, `0x02/0x03` document opened/released, `0x12` capture requested by the Editor, `0x13` Editor closed; Snapnab's own: `0x30` open file, `0x31` library changed, `0x32` restart the Editor (language change) |
| Extended window (§6) | `src/snapex/SnapEx64.cpp` (`Snap5000`/`Snap5001`) |
| Automatic scrolling (§3.2, §9.5) | mouse wheel scrolling + an in-house `PhaseCorrelationTranslationDetector` (2D FFT, `conj(B)`, `DBL_EPSILON`, Hann window, negated peak) with SAD verification and stitching |
| Panoramic scrolling (comp_1/comp_3) | `src/capture/Panoramic.*`: the chosen region is copied ~11 times per second while the user scrolls; the offset between frames comes from comparing row (or column) signatures, and only the new strip is pasted, so fixed headers are not repeated |
| One Click (comp_7 §3) | `src/capture/OneClick.*`: layered window docked to an edge of the screen; collapses to an accent-colored strip and opens with the mouse over it; dragging detaches the panel and it snaps to the nearest edge (`OneClickEdge`, `OneClickPos`, `OneClickMonX/Y`, `OneClickEnabled` in the registry); hides during a capture and stays out of captures (`WDA_EXCLUDEFROMCAPTURE`) |
| Webcam (comp_3 §8) | `src/capture/Webcam.*`: `MFEnumDeviceSources` + `IMFSourceReader` (RGB32 through the video processor) in a mirrored layered window inside the recorded area (circle, rounded square, square, 16:9 or 4:3); since the recording copies the screen, the webcam ends up in the video; chroma key by chrominance distance (Cb/Cr) with a soft edge and halo reduction; the HUD has the show/hide button (`ToggleWebcamCommand`) |
| Grab Text | "Capture text" preset: the capture goes through OCR (`rust/snapcore/src/ocr.rs`, C++ API in `src/common/Ocr.*`), the text goes to the clipboard and opens in a window |
| Computer vision (§9) | `rust/snapcore/src/cv.rs` (C++ API in `src/common/Cv.*`): cost-ordered wand, global selection, `DoWand` (clamp 0.01–100, shortcut 100), Dingo7 (dominant color of the dilated neighborhood), diffusion (role of Navier-Stokes), Smart Move automatic tolerance (formula from §9.6) |
| `.snap` format (ed_comp_2 §3) | `src/common/SnapFile.*`: 24-byte header + OLE compound file; streams `Index` (XML), `Frame%d/Document`, `Frame%d/ImageStorage/Image_N.png`. The whole document (index, frame documents, base and embedded images, photo metadata) is read and written by `rust/snapcore/src/snap.rs`, over the compound file of `container.rs` (the `cfb` crate, version 3 files like the Windows API writes); `SnapFile.cpp` hands the frames over and copies them back (`XmlNode` stays in C++ until its users move). The former C++ code over the structured storage API and an offset `ILockBytes` stays as the Windows backend, so the tests can check one against the other |
| Library (DataStore) | `%LOCALAPPDATA%\Snapnab\Library` (`<GUID>.snap`, `Video_*.mp4`, `Thumbnails\`); the folder is watched by the Editor. Capture names (the reference's DataStore keeps them in its database) are in `Names.xml` in the library, by file name. Listing, adding, thumbnails, names, cleanup and automatic names are in `rust/snapcore/src/library.rs` (C++ API in `src/common/Library.*`); the thumbnail is scaled (`MakeThumbnail`, GDI+ in `gdip.rs`) and written by Rust |
| Interactive or fixed region (comp_1 §6, comp_7 §2) | Region gear: `RegionSelectionType` Interactive/Fixed, `Region{Left,Top,Width,Height}` and `SetPosition`; with a position, the input is ready without a selector (`INPUT_REGION_FIXED`); without one, the crosshair shows a fixed-size rectangle to place |
| Presets (comp_7) | `Presets1.xml` with capture, effects, outputs, audio/video and hotkey in the same object; `.snappresets` import/export |
| Hotkeys (comp_7 §4) | `RegisterHotKey` with internal conflict detection; an external conflict (e.g. Windows' Print Screen) falls back to a `WH_KEYBOARD_LL` hook; Esc registered as hotkey id 3 while the crosshair is active |
| Video (§11, comp_3) | `src/capture/Recorder.*`: BitBlt → `IMFSinkWriter` H.264/MP4 (optional hardware, falls back to software), WASAPI microphone + system loopback, mixer with separate mute and volume (0–200%, automatic microphone gain optional) → AAC, pause without a time gap, downscaling to 1080p, cursor, disk limits, prevents sleep. Before the recording the HUD opens the sources only for the meters (`StartMonitor`) |
| MP4 cutting (§11.4) | `TranscodeWithCuts` with `IsInsideAnyCut` / `PrecedingCutsDuration` (PTS rewriting) |
| GIF (§11.5) | `rust/snapcore/src/gif.rs` (C++ API in `src/common/Gif.*`): in-house LZW, 4 bpp (16 colors) or 8 bpp, median cut palette, Floyd–Steinberg dithering, delta frames; the palette sorts with a port of the MSVC `std::sort`, so the files stay byte for byte those of the C++ writer; frames of another size are scaled with GDI+ (`ScaleImage`, `gdip.rs`) |
| PDF (§10.1) | A4 or Letter page according to the measurement system, orientation from the image, margin 0; captured links become `/Link` annotations with a `/URI` action |
| Polygon objects (comp_5 §5.1 `CPaintOpPolygon`) | the Highlighter, Spotlight and Hide areas take an `AreaShape` (the `ShapeKind` outlines, a clicked polygon or a freehand outline) and keep the corners in `Points`, as the reference's `<Polygon>` |
| Layers and z-order (comp_5 §4 `CPaintOpReorderComponentSelection`) | `src/editor/LayersPanel.cpp`: hide, lock, rename and reorder are document operations with an undo step; `Hidden`, `Locked` and `Name` attributes of each object in the `.snap` |
| Interface language (ed_comp_1 §3, step 5) | `rust/snapcore/src/i18n.rs` (C++ API in `src/common/I18n.*`): like the `SnagItEditorRes.dll` in the language folder, each program loads `lang\<code>.po` from the executable's folder at startup; the code stays in Portuguese and `Tr(L"…")` (in Rust, `tr!("…")`) returns the translation (static tables mark strings with `TR_NOOP` and translate them when shown). Language = the one chosen in *Options* (`Language` in `HKCU\Software\Snapnab\1`) or the first Windows display language with a catalog, otherwise English; anything missing from a catalog falls back to English. Changing the language restarts both programs (`/restartwait` waits for the previous instance to close). Built-in names stored as data (presets, themes, quick effects) stay in Portuguese in the file and are translated when shown. `Format` uses `_vswprintf_p`, so translations can reorder the values (`%1$s`) |

## Capture window

Compact and fixed-size, with a custom title bar (icon and the File, Capture, View and Help menus on the same strip,
also opened with Alt+letter and F10). Image/Video tabs are vertical; Selection, Effects and Share fields (image) or
Selection, Quality and Webcam (video); option switches and Delay; a round red capture button (the hotkey is in its tooltip).
Bottom bar: ⚙ Presets (opens the list), + (new preset from the current settings, new preset, help) and Open Editor.
Each row of the list has "Add hotkey" and a gear (capture, rename, hotkey, duplicate, delete).
The colors are slightly tinted toward the Windows accent color (Personalization > Colors) and follow changes immediately.

The controls are custom (`src/common/Widgets.*`) and speak the standard controls' messages (`BM_GETCHECK`, `CB_ADDSTRING`,
`CBN_SELCHANGE`…): toggle switches, flat or icon-only buttons, "Label Value ›" rows that open a menu and combo boxes with
a drop-down list, which can have the settings gear inside (`MUI_GEARCLICK` notification). The **Effects** box lists
Border, Filters, Shadow, Capture info, Color replacement, Resize image, Edge effects, Trim edges,
Color adjustment, Rotate image, Watermark and Image resolution; choosing one opens its settings and turns it on, with
"Add another effect" and "Remove this effect" to combine several.

## Editor

Same title bar, colors and controls as the capture window. At the top, Capture and Record on the left, Copy all,
Save as and Share on the right and, in the middle, the customizable toolbar (TopPanel.ToolBar, ed_comp_3 §3.2): items of the
`ToolBarItemType` kinds — tool, effect, output, command and spacer — and "More/Edit" at the end, with the tools
that are not on the bar and the customization. Automatic: Move, Selection, Text, Highlighter and Shape, the 4 most
recently used tools and the Magnifier; custom: any item, through the *Customize toolbar* window (available × on the bar)
or by dragging one item over another. Registry: `ToolbarCustom`, `ToolbarItems` (`t:5,e:1039,o:4,c:1015,s:0`) and
`ToolbarRecentTools`.

On the right, the Properties and Effects tabs. The effects panel (EffectsPanelControls, ed_comp_5 §4) has the quick
effects gallery at the top (`%LOCALAPPDATA%\Snapnab\QuickEffects.xml`; a quick effect combines border, edge effect, page
curl and shadow, and "Add" saves the panel's current settings as a new one) and, below, Border, Shadow,
Perspective, Page curl, Edge effects, Filters, Color adjustment, Color replacement, Spotlight, Watermark and
Caption. Clicking an effect opens its settings inside the panel; each change shows the live preview in place of the image
(PreviewEffect) and *Apply* writes the effect with one undo step (ApplyEffect). Border, shadow and caption keep the
objects editable (only shifted); the others flatten the objects first, so the preview and the result are the same. The
*Image › Effects* menu and the toolbar's effect items open the same panel. The same function produces the thumbnails (with
pixel values scaled to the thumbnail size, rebuilt 350 ms after the last edit).

The Highlighter creates `Highlighter` objects (`CPaintOpHighlight`, comp_5 §3.2) in the chosen color and opacity, in
any area shape (rectangle by default); old freehand highlighters still open with their color multiplied over the image.

New object properties of this version are written to the `.snap` only when they are used and read with defaults, so
older files open unchanged: `Name`, `Hidden`, `Locked`, `Floating` (lifted piece), `AreaShape` + `Points`,
`LineSpacing`, `StepOutlineColor`/`StepOutlineWidth`, `FillGradient`/`GradientAngle`/`GradientStops`
(`pos:#AARRGGBB;…`), `BackgroundBlur`/`BackgroundBlurAmount`, `Loupe`/`LoupeHandle`, the arrow label (`Text` + font
attributes on `<Arrow>`), `<Link … Url>` objects and the document's `<Guides>`. The quick styles (`StyleToXml`) carry
the same style fields.

Canvas extras: rulers and guides (`Rulers` in the registry, guides per document), edge snapping by contrast for the
selection and crop (`EdgeSnap`; the strongest column/row difference within 6 screen pixels along the edge), axis lock
with Shift while moving, the polygon tool, the context menu, exact geometry (`GeometryKind`/`SetGeometry`) and the lifted
piece (a `floating` picture object merged by *Drop piece*). Copying objects puts them on the clipboard as
`Snapnab.Objects` (the objects' XML with the embedded images in base64) and an area of the image adds `Snapnab.Origin`;
*Paste in place* uses them. *Paste text without formatting* (`PlainTextPaste`) takes, when the formatting is kept, the
first run of *Rich Text Format* or *HTML Format* (`Clip.cpp`).

Properties tab (`PropsPanel.cpp`, `ToolProperties.*` + Quick Styles, ed_comp_3 §2.3 and ed_comp_8 §5): at the top, the
tool's quick styles (`QuickStyles.*`): each theme stores up to 4 colors and the styles of every tool are
generated from them (GenerateQuickStylesForTheme), plus the ones the user adds with "+"
(`%LOCALAPPDATA%\Snapnab\ToolThemes.xml`; `.snaptheme` export). The star marks a style as a favorite
(`Favorites.xml`); the Favorites tool shows all of them and, when one is chosen, switches the drawing tool while keeping
the bar on Favorites. For the Stamp tool, the gallery (`Stamps.*`): categories of color emoji (drawn with
Direct2D in `rust/snapcore/src/stamps.rs`, at the object's size) and My stamps (`%LOCALAPPDATA%\Snapnab\Stamps`).
Below, the tool properties, drawn by the panel itself: colors (palette in `Popups.*`), shadow
(ShadowPropertiesControl: direction on the 3×3 grid, color, opacity, blur, distance), shape, ends and dash pattern,
sliders with a value box, advanced text. Objects are drawn on their own layer (`rust/snapcore/src/render.rs`, through `Render.cpp`):
the shadow comes from the layer's alpha and the opacity applies to the whole object. The current styles of each tool are
stored in `ToolStyles.xml`.

Customize toolbar (CBCGPToolbarCustomize): grid window with Drawing tools, Menu items, Share outputs, Effects and
Miscellaneous; dragging an item onto the bar inserts it where the bar indicates; while the window is open the
bar is in customization mode and dragging an item off it removes it. Cancel restores the previous bar. The
same window chooses how many tools the automatic bar shows (6 to 16, `ToolbarToolCount`).

Image menu (as in the reference): Tools, Effects (items with icons, 32 bpp `hbmpItem`), Arrange (order one step
or all the way, Align — one object relative to the image, several among themselves —, Flip the selected objects or the image),
Group/Ungroup, Flatten selected / Flatten all, Trim, Resize image/canvas, Apply template
(disabled: templates not implemented), Rotate, Snap to canvas (`CanvasSnapping`), Canvas color and Tag
image (`Tags` attribute of the `.snap` document).

Multiple object selection (`Canvas`): the primary object (`selected`, the last one clicked, whose properties appear in the
panel) plus the other selected ones; Ctrl/Shift+click adds or removes, and the Move tool's selection rectangle picks the
objects it touches. Group (`CPaintOpGroup`, comp_5 §3.2) replaces the selected objects with a `DrawObj` of type Group that
stores the children by value (copying the group copies the children, which keeps undo correct); in the `.snap` it is
`<Group X1… >` with the children's elements inside.

At the bottom, the recent captures tray (Tray, ed_comp_3 §5): `Tray.bin` in the library stores the order, up to
`RecentTrayDocs` items (default 10); new captures and opened documents go to the front, a deleted item frees a slot
for the next capture and "Remove from tray" does not come back by itself. Dragging items to another program
(TrayDragDrop) delivers PNGs with the annotations (written to `%TEMP%\Snapnab`) or the MP4 in a custom `IDataObject` with
`CF_HDROP` and `Shell IDList Array`, preferred effect "copy" and "move" allowed only for the temporary copies (Paint
only accepts "move"; the MP4 is the library file); the `WM_DROPFILES` that arrives when the item is dropped back on the
Editor itself is discarded at the end of the drag. Files dropped from Explorer are imported. Holding the button on empty
space draws the selection rectangle (Ctrl/Shift add to the selection). *Library* (or *View › Library*) shows all
captures in a grid in place of the image, with multiple selection, batch export and delete, and a search by name.
Resting the mouse on a thumbnail shows a larger preview (the composed image, or the first frame and the duration of a
video). `Tray.bin` is only rewritten when the tray changes (the library folder is watched).

Stamps: the *Labels* category draws rounded badges at run time (`label:RRGGBB:Text`, the text in the interface language
when the stamp is placed, stored in the object); the gallery order and the pinned stamps are in `StampOrder.xml`, by
category key (language independent).

Settings backup (`src/common/Settings.*`): one XML file (`.snapsettings`) with every value of `HKCU\Software\Snapnab\1`
and its subkeys and the settings files of the data folder (base64). Importing closes the Editor first (it writes its
settings when it closes), saves the current settings to `Backups\Before-import-*.snapsettings`, replaces or merges
(registry values that already exist, list items with the same name and the tool styles stay; the missing presets,
themes, favorites, quick effects and stamp categories are added) and restarts both programs. The capture
program writes `Backups\Settings-YYYY-MM-DD.snapsettings` once a week and keeps the last 5.

Capture of each window as an object (full screen / monitor preset option): `PaintDesktop` draws the wallpaper, the
taskbars come from the screen capture and each visible window (`EnumWindows` z-order, without cloaked, transparent or
own windows) is captured with `PrintWindow(PW_RENDERFULLCONTENT)`, cut to `DWMWA_EXTENDED_FRAME_BOUNDS`, with the
Windows 11 rounded corners; programs that do not draw this way fall back to their visible part. The library receives a
`.snap` with the background and one `<Image Name="title">` per window. Links (`Links.*`): UI Automation searches the
`Hyperlink` elements of browser and document viewer windows under the area (URL from the Value or LegacyIAccessible
value), on a worker thread limited to 900 ms; they become `<Link>` objects (they follow scale and border effects; other
effects that change the geometry drop them).

## Competitor feature batch (C01–C44)

The features of the competitor feature request (local notes in `docs/pesquisa/`, not part of the repository) follow the patterns above (presets,
`RunForm`, objects added through `Canvas::PasteObjects`, settings in `HKCU\Software\Snapnab\1` or the data folder,
exported with the settings) and, where it has the feature, the ShareX reverse-engineering dossier
(`docs/pesquisa/01-ANALISE.md`, `02-EDITOR_ANALISE.md`). Map:

| Feature | Module |
|---|---|
| Auto-copy, quick thumbnail (C08), capture sound (C31), clean desktop (C16), HDR (C02), rounded corners (C06), cursor as object (C25) | `capture/CaptureEngine.*`, `capture/QuickOverlay.*`, `common/Sounds.*`, `capture/CleanDesktop.*`, `common/Hdr.*` and `rust/snapcore/src/hdr.rs` (DXGI FP16 duplication + tone mapping) |
| Pins (C01) | `capture/Pins.*` (renders `.snap` objects with the Editor's `Render.cpp`) |
| Region history, keyboard selection, color in the crosshair (C18, C19, C10) | `capture/SelectOverlay.*`, `common/ColorPicker.*`, `common/Color.*` (texts in `rust/snapcore/src/color.rs`) |
| Interval capture (C24), screen focus (C39), library cleanup (C41), command line (C44) | `capture/IntervalCapture.*`, `capture/ScreenFocus.*`, `common/Library.*`, `capture/main.cpp` |
| WebP/AVIF/HEIC and metadata (C03, C29) | `rust/snapcore/src/codec.rs` (WebP with libwebp 1.5.0 through `libwebp-sys`; AVIF/HEIC through WIC, HEIF encoder with AV1), C++ API in `common/Image.*` |
| QR (C04) | `rust/snapcore/src/barcode.rs` (the `rxing` crate, a Rust port of ZXing); `common/Barcode.*` (results dialog) |
| Beautify (C05) | `rust/snapcore/src/beautify.rs` (the render); `common/Beautify.*` (presets, capture effect and Editor dialog) |
| Copy as (C30), drag out | `common/Clipboard.*`, `common/DragOut.*` |
| Office (C20), upload (C21) | `common/Office.*` (IDispatch late binding), `rust/snapcore/src/upload.rs` + `net.rs` (WinHTTP, SigV4, OAuth PKCE loopback, Credential Manager); `common/Upload.*` (dialogs) |
| Combine, redaction, cursors, date stamp, background removal, split, contrast (C09, C11, C25–C27, C34, C36) | `editor/EditorTools.*`, `editor/Cursors.cpp`, `editor/Pdf.cpp` and `rust/snapcore/src/pdf.rs` (PDF writer, smart page breaks) |
| Compare (C17), AI assistant (C22), batch (C28) | `editor/Compare.cpp`, `editor/Assistant.cpp` (window; the request in `rust/snapcore/src/ai.rs`), `editor/Batch.cpp` |
| Smart highlighter, text only (C12), draw & hold (C23), touch/pen (C43) | `editor/Canvas.cpp` (OCR word boxes from `common/Ocr.*`), `RecognizeStroke` in `EditorTools.cpp` |
| Hand-drawn style (C35), vertical text and text on a path (C37) | `rust/snapcore/src/render.rs`, `editor/Document.h` (new `Style` fields, written to the `.snap` only when set) |
| Clicks/keys overlay (C07), zoom (C15), crash-safe recording (C14), audio only (C32), restart confirmation (C33) | `capture/Recorder.*` (low-level hooks feed the overlays; fragmented MP4 remuxed at the end; `RecoverInterrupted` at startup), `capture/RecorderHud.*` |

## Deliberate differences from the reference

- **Not implemented (out of scope or dependent on third-party services):** licensing, telemetry, cloud crash
  reporting, updater, account/login, JWT feature switches, cloud plugins (`.tscplugin`), COM automation of Snapnab
  itself (Office is driven as a COM client), virtual printer, helper process with `uiAccess`, phone integration, drop
  zones (templates are the *Combine images* layouts), Simplify, spell checker,
  XLIFF translation, video from images, full-screen webcam (the *switcher* that swaps the screen for the camera) and
  All-in-One mode.
- **Replacements for third-party components:** LEADTOOLS → WIC (plus libwebp for WebP); ABBYY → Windows OCR
  (`Windows.Media.Ocr`, depends on the installed languages); barcode reading → rxing (Rust port of ZXing); SQL CE → the library folder itself; OpenCV → in-house algorithms in `cv.rs`; Cairo/Pango → GDI+.
- Help: the reference opens its help online (`IRedirect.LaunchHelpRedirect`, `HELP_*` topics); Snapnab ships an
  offline manual, `help/Snapnab-Manual.html`, opened by *Help › Snapnab Manual* and F1 (`OpenManual` in
  `src/common/Util.cpp`). A topic is a `#section` anchor; since a file association drops the fragment, the default
  browser is started with the `file://` URL. In the Editor, F1 opens the anchor of the current tool, the library or the
  video editor.
- `SnapEx64` installs the hook only on the target window's thread (the reference uses a global hook), so the DLL is not
  injected into the whole desktop.
- Dingo7 counts only the ring around the selection (the pseudocode includes the object's own pixels, which would pull
  its color in).
- The "Image resolution" effect only applies to files saved by the capture; the image sent to the Editor does not keep the DPI.
- Inno Setup installer (`Snapnab-<version>-setup.exe`, per user without administrator rights or for all users) instead
  of the reference's MSI installed with `msiexec` (comp_9); there is no automatic updater.
- Translations in gettext catalogs instead of per-language resource DLLs (same choice at program startup, but
  editable without compiling). In Arabic and Urdu each string reads right to left (RLE/PDF embedding), but the
  window layout is not mirrored: the UI drawn with GDI+ does not follow GDI mirroring.

## Tests

`SnapnabTests.exe` (through `build.bat test` or `ctest --preset release`) covers: phase correlation (37/120/250 px), wand and
inpainting, `.snap` round trip, rendering of every object, PDF, 4/8 bpp GIF, effects (including the 12 capture effects
together), IPC between sender and receiver, real MP4 recording (frame orientation and pause excluded from the duration),
MP4 cutting, GIF from video, OCR, panoramic scrolling stitching (vertical with a move back, horizontal and unchanged
frame), the batch of editor features (area shapes, gradients, step outline, line spacing, arrow label, frosted
callout, loupe, label stamps, hidden objects, the `.snap` round trip of every new property and the defaults of old
files, quick styles, the clipboard object format, RTF/HTML formatting and PDF links) and the translation catalogs (all
complete and with the same `%d`/`%s` markers as the original).

The `.snap` tests also write with each container and read with the other, and feed both damaged files (garbage after
the header, a file cut in half); `cargo test --workspace` in `rust/` runs the Rust tests (container, `.snap`
documents, XML, image files, `.po` reader and shipped catalogs, registry round trip). The parts moved to Rust are
checked against the former C++ code, kept in `tests/*Reference.cpp`: the XML, the image files and the `.snap` documents
are written byte for byte the same (the `.snap` but for the storage times of the compound file) and the files of the
user's library read alike (only read); the library functions run in folders of their own and give the same lists,
names, `Names.xml`, captures, thumbnails and cleanups; the GIF writer writes the same bytes. WebP, whose former build
came from CMake, is checked against `tests/data/webp-libwebp-1.5.0.txt`: the size and hash of the files that build wrote
(lossless and five qualities) and of the pixels it read back. The effects, Beautify, scaling, computer vision and PDF
export are checked against `tests/data/pixels-cpp.txt`: the size, hash, resolution and metadata of what the former C++
code made of the same generated images (every effect and its settings, the capture preset effects, Beautify
backgrounds, aspects and alignments, masks, inpainting, translation detection, page breaks and the PDF bytes). The
barcode reader is checked against `tests/data/barcodes.txt`, what ZXing-C++ 2.2.1 read in `tests/data/barcodes/*.png`
(codes its writer made: every symbology, small, rotated, inverted, transparent, several in one picture): the same codes,
text and symbology, with a box over the same place. The color texts, the HDR tone mapping and OCR (text the C++ drew,
read by Windows) are checked against `tests/data/misc-cpp.txt`. SHA-256/HMAC, URL encoding, the JSON reader and writer,
multipart bodies, the assistant's request bodies, AWS Signature V4 and the custom uploader syntax are checked by
`cargo test` against `tests/data/net-cpp.txt`, what the former C++ code gave for the same inputs. The Editor's
painters are checked against `tests/data/render-cpp.txt`: every object type and style variation drawn by the former
C++ painters on a generated image, with the bounds, text and label boxes, outlines and stamp images they measured.
The objects' XML, older and hand-written files, the clipboard text, a `.snap` saved and opened and hit-testing on a grid
are checked against `tests/data/doc-cpp.txt`, what the former C++ code gave.

Helper modes: `SnapnabTests.exe dump file.snap` prints the document XML; `SnapnabTests.exe audio file.mp4`
reads the audio track; `SnapnabTests.exe comparesnap <folder>` opens every `.snap` of a folder with both containers,
checks that they read the same and times reading and writing. `SnapnabTests.exe webpgolden` prints the WebP fingerprints of the
current build, `SnapnabTests.exe fxgolden` its pixel fingerprints, `barcodegolden` what it reads in the barcode images and `miscgolden`
its color, HDR and OCR results.
