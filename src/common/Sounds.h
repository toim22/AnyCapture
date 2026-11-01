// Capture sound (C31; ShareX dossier 01 · A7: a notification sound played after the capture, not by the engine).
// The built-in sounds are synthesized in memory (no files to ship); the user can pick a WAV of their own.
// Settings: CaptureSound (0 off — the default —, 1 camera shutter, 2 click, 3 soft chime, 4 custom file),
// CaptureSoundFile.
#pragma once
#include "Common.h"

namespace snap::sounds {

enum Kind { Off = 0, Shutter, Click, Chime, Custom, Count };
const wchar_t* KindName(Kind k);        // translated name for the Options list
void Play(Kind k, const std::wstring& customFile = L"");
void PlayCaptureSound();                // with the user's settings (nothing when off)

} // namespace snap::sounds
