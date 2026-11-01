// Color values as text for the screen color picker, the crosshair and pinned images (C10): HEX, RGB, HSL and code
// formats, plus the list of recently picked colors (registry RecentColors, shared by both programs)
#pragma once
#include "Common.h"

namespace snap::color {

enum Fmt { Hex, Rgb, Hsl, HexNoHash, CssRgb, Code0x, CSharp, Delphi, FmtCount };
const wchar_t* FmtName(Fmt f);                     // "HEX (#RRGGBB)", "RGB (r, g, b)"…
std::wstring ToText(COLORREF c, Fmt f);
Fmt DefaultFmt();                                  // ColorPickerFormat (HEX by default)
void SetDefaultFmt(Fmt f);

void RgbToHsl(COLORREF c, double& h, double& s, double& l);   // h 0..360, s and l 0..1
double Luminance(COLORREF c);                      // WCAG relative luminance (sRGB)
double ContrastRatio(COLORREF a, COLORREF b);      // WCAG 2 contrast ratio, 1..21

std::vector<COLORREF> Recent();                    // most recent first (up to 16)
void AddRecent(COLORREF c);

bool CopyText(HWND owner, const std::wstring& text);   // CF_UNICODETEXT on the clipboard

} // namespace snap::color
