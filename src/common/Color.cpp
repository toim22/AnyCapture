#include "Color.h"
#include "Clipboard.h"
#include <cmath>

namespace snap::color {

const wchar_t* FmtName(Fmt f) {
    switch (f) {
    case Hex: return L"HEX  #RRGGBB";
    case Rgb: return L"RGB  r, g, b";
    case Hsl: return L"HSL  hsl(h, s%, l%)";
    case HexNoHash: return L"HEX  RRGGBB";
    case CssRgb: return L"CSS  rgb(r, g, b)";
    case Code0x: return L"C/C++  0xRRGGBB";
    case CSharp: return L"C#  Color.FromArgb(r, g, b)";
    case Delphi: return L"Delphi  $00BBGGRR";
    default: return L"";
    }
}

void RgbToHsl(COLORREF c, double& h, double& s, double& l) {
    double r = GetRValue(c) / 255.0, g = GetGValue(c) / 255.0, b = GetBValue(c) / 255.0;
    double mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
    l = (mx + mn) / 2;
    if (d < 1e-9) { h = 0; s = 0; return; }
    s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) h = std::fmod((g - b) / d + 6, 6.0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
}

std::wstring ToText(COLORREF c, Fmt f) {
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    switch (f) {
    case Rgb: return Format(L"%d, %d, %d", r, g, b);
    case Hsl: {
        double h, s, l; RgbToHsl(c, h, s, l);
        return Format(L"hsl(%d, %d%%, %d%%)", (int)std::lround(h) % 360, (int)std::lround(s * 100), (int)std::lround(l * 100));
    }
    case HexNoHash: return Format(L"%02X%02X%02X", r, g, b);
    case CssRgb: return Format(L"rgb(%d, %d, %d)", r, g, b);
    case Code0x: return Format(L"0x%02X%02X%02X", r, g, b);
    case CSharp: return Format(L"Color.FromArgb(%d, %d, %d)", r, g, b);
    case Delphi: return Format(L"$00%02X%02X%02X", b, g, r);
    default: return Format(L"#%02X%02X%02X", r, g, b);
    }
}

Fmt DefaultFmt() { return (Fmt)std::clamp<DWORD>(RegGetDword(L"ColorPickerFormat", Hex), 0, FmtCount - 1); }
void SetDefaultFmt(Fmt f) { RegSetDword(L"ColorPickerFormat", (DWORD)f); }

double Luminance(COLORREF c) {
    auto lin = [](int v) { double x = v / 255.0; return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(GetRValue(c)) + 0.7152 * lin(GetGValue(c)) + 0.0722 * lin(GetBValue(c));
}

double ContrastRatio(COLORREF a, COLORREF b) {
    double la = Luminance(a), lb = Luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

std::vector<COLORREF> Recent() {
    std::vector<COLORREF> v;
    std::wstring s = RegGetString(L"RecentColors", L"");
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(L';', i);
        std::wstring t = s.substr(i, j == std::wstring::npos ? std::wstring::npos : j - i);
        if (!t.empty()) v.push_back(HexToColor(t, 0));
        if (j == std::wstring::npos) break;
        i = j + 1;
    }
    return v;
}

void AddRecent(COLORREF c) {
    auto v = Recent();
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
    v.insert(v.begin(), c);
    if (v.size() > 16) v.resize(16);
    std::wstring s;
    for (COLORREF x : v) s += ColorToHex(x) + L";";
    RegSetString(L"RecentColors", s);
}

bool CopyText(HWND owner, const std::wstring& text) { return clip::CopyText(owner, text); }

} // namespace snap::color
