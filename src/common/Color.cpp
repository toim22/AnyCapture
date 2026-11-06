// Color texts: the work is done by snapcore (rust/snapcore/src/color.rs); these are the C++ entry points.
#include "Color.h"
#include "Clipboard.h"
#include "SnapcoreBridge.h"

namespace snap::color {

const wchar_t* FmtName(Fmt f) { return snapcore_color_format_name((int32_t)f); }

void RgbToHsl(COLORREF c, double& h, double& s, double& l) {
    double v[3] = {};
    snapcore_color_hsl(c, v);
    h = v[0]; s = v[1]; l = v[2];
}

std::wstring ToText(COLORREF c, Fmt f) {
    SnapcoreWStr s{};
    if (!snapcore_color_text(c, (int32_t)f, &s)) return L"";
    std::wstring r = s.len ? std::wstring(s.data, s.len) : std::wstring();
    snapcore_wstr_free(s);
    return r;
}

Fmt DefaultFmt() { return (Fmt)snapcore_color_default_format(); }
void SetDefaultFmt(Fmt f) { snapcore_color_set_default_format((int32_t)f); }

double Luminance(COLORREF c) { return snapcore_color_luminance(c); }
double ContrastRatio(COLORREF a, COLORREF b) { return snapcore_color_contrast(a, b); }

std::vector<COLORREF> Recent() {
    std::vector<COLORREF> v(snapcore_color_recent(nullptr, 0));
    if (!v.empty()) v.resize(snapcore_color_recent((uint32_t*)v.data(), v.size()));
    return v;
}

void AddRecent(COLORREF c) { snapcore_color_add_recent(c); }

bool CopyText(HWND owner, const std::wstring& text) { return clip::CopyText(owner, text); }

} // namespace snap::color
