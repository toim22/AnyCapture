// Beautify a capture (C05; ShareX dossier 01 · B7 ImageBeautifier: margin, padding, rounded corner, shadow, background;
// 02 · E.1 DrawBackground*/wallpaper cover-fit): makes a capture presentation-ready in one click — background
// (gradient, desktop wallpaper, image or solid color), padding, rounded corners, shadow, alignment, aspect-ratio presets
// and "auto balance" of the margins. Named presets live in BeautifyPresets.xml; a preset effect can apply one to every
// capture (CaptureEffects::beautify).
#pragma once
#include "Image.h"
#include "Xml.h"

namespace snap::fx {

struct BeautifyOptions {
    int background = 1;              // 0 solid color, 1 gradient, 2 desktop wallpaper, 3 image file, 4 transparent
    COLORREF color1 = RGB(102, 126, 234), color2 = RGB(118, 75, 162);
    int angle = 135;                 // gradient direction (degrees)
    std::wstring imagePath;          // background image (3)
    int padding = 64;                // space around the capture (px)
    int radius = 12;                 // rounded corners of the capture (px)
    int shadow = 45;                 // shadow opacity (%), 0 = none
    int shadowSize = 24;             // shadow blur (px)
    int align = 4;                   // 0..8 on a 3×3 grid (4 = center) when the aspect ratio leaves free space
    int aspect = 0;                  // 0 free, 1 16:9, 2 4:3, 3 1:1, 4 9:16
    bool autoBalance = true;         // trims uniform borders of the capture first, so the margins look even
    XmlNode ToXml(const char* name = "Beautify") const;
    static BeautifyOptions FromXml(const XmlNode& n);
};

Image Beautify(const Image& src, const BeautifyOptions& o, POINT* contentOrigin = nullptr);   // where the capture ended up
const wchar_t* AspectName(int aspect);
std::wstring WallpaperPath();        // the desktop wallpaper file (SPI_GETDESKWALLPAPER, or the transcoded cache)

// named presets (BeautifyPresets.xml): the built-in ones first
struct BeautifyPreset { std::wstring name; BeautifyOptions opt; bool builtin = false; };
std::vector<BeautifyPreset> BeautifyPresets();
void SaveBeautifyPresets(const std::vector<BeautifyPreset>& all);    // only the user's ones are written
// settings dialog (Editor › Image › Beautify and the capture preset effect): a saved preset or the fields, with a live
// preview of 'sample' when given; a name in "Save as preset" stores the fields in BeautifyPresets.xml
bool BeautifyDialog(HWND owner, BeautifyOptions& o, const Image* sample = nullptr);

} // namespace snap::fx
