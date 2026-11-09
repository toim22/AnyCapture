AnyCapture — screen capture, video recording and image editor
===========================================================

REQUIREMENTS
  Windows 10 version 2004 or later, or Windows 11 (64-bit).
  Nothing else needs to be installed besides this folder.

HOW TO USE
  1. Open AnyCapture.exe. It lives in the notification area (next to the clock).
  2. Print Screen          captures an image
     Shift+Print Screen    records a video
     Shift+F9 / Shift+F10  pauses / stops the recording
  3. The capture opens in AnyCapture Editor (AnyCaptureEditor.exe), where you can
     annotate, apply effects, cut videos and save as PNG, JPG, GIF, PDF etc.

  The complete user manual is help\AnyCapture-Manual.html (Help > AnyCapture Manual,
  or F1, in both programs).

  The files AnyCapture.exe, AnyCaptureEditor.exe and SnapEx64.dll and the lang
  (translations) and help (manual) folders must stay together in the same folder. To install
  with Start menu shortcuts, use AnyCapture-<version>-setup.exe.

WHERE YOUR DATA IS STORED
  Captures and videos:  %LOCALAPPDATA%\AnyCapture\Library
  Presets:              %LOCALAPPDATA%\AnyCapture\Presets1.xml
  Logs:                 %LOCALAPPDATA%\AnyCapture\Logs
  Settings:             HKEY_CURRENT_USER\Software\AnyCapture

COMMAND LINE
  AnyCapture.exe /capture | /video | /preset "name" | /clipboard | /hide | /exit | /help
  AnyCaptureEditor.exe /Open "file" | /NewFromClipboard | /help

LICENSE
  Copyright © 2026 Antonio Rezende.
  AnyCapture is free software, distributed under the GNU General Public License
  version 3 (LICENSE file). No warranty of any kind.

UNINSTALL
  1. Right-click the tray icon and choose "Exit".
  2. If you turned on "Start with Windows" in Options, turn it off before exiting.
  3. Delete this folder. To also delete your captures and settings,
     delete %LOCALAPPDATA%\AnyCapture and the HKEY_CURRENT_USER\Software\AnyCapture key.
