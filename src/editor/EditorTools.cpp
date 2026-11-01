#include "EditorTools.h"
#include "Color.h"
#include "ColorPicker.h"
#include "Effects.h"
#include "Ui.h"
#include <regex>
#include <dwmapi.h>
#include <array>
#include <deque>

using namespace Gdiplus;

namespace snap::ed {

// ================================================================ C09 combine (dossier 01 · F12 ImageCombiner)
CombineOptions CombineOptions::Load() {
    CombineOptions o;
    o.layout = std::clamp((int)RegGetDword(L"CombineLayout", 0, L"Editor"), 0, 2);
    o.columns = std::clamp((int)RegGetDword(L"CombineColumns", 2, L"Editor"), 1, 20);
    o.gap = std::clamp((int)RegGetDword(L"CombineGap", 16, L"Editor"), 0, 500);
    o.margin = std::clamp((int)RegGetDword(L"CombineMargin", 0, L"Editor"), 0, 500);
    o.align = std::clamp((int)RegGetDword(L"CombineAlign", 1, L"Editor"), 0, 2);
    o.sameSize = RegGetDword(L"CombineSameSize", 0, L"Editor") != 0;
    o.transparent = RegGetDword(L"CombineTransparent", 0, L"Editor") != 0;
    o.background = RegGetDword(L"CombineBackground", RGB(255, 255, 255), L"Editor");
    return o;
}

void CombineOptions::Save() const {
    RegSetDword(L"CombineLayout", layout, L"Editor"); RegSetDword(L"CombineColumns", columns, L"Editor");
    RegSetDword(L"CombineGap", gap, L"Editor"); RegSetDword(L"CombineMargin", margin, L"Editor");
    RegSetDword(L"CombineAlign", align, L"Editor"); RegSetDword(L"CombineSameSize", sameSize, L"Editor");
    RegSetDword(L"CombineTransparent", transparent, L"Editor"); RegSetDword(L"CombineBackground", background, L"Editor");
}

Image CombineImages(const std::vector<Image>& in, const CombineOptions& o, std::vector<RECT>* placed) {
    std::vector<Image> imgs;
    for (auto& i : in) if (!i.empty()) imgs.push_back(i);
    if (placed) placed->clear();
    if (imgs.empty()) return {};
    const int n = (int)imgs.size();
    const int cols = o.layout == 0 ? n : o.layout == 1 ? 1 : std::clamp(o.columns, 1, n);
    const int rows = (n + cols - 1) / cols;
    if (o.sameSize) {
        int maxW = 0, maxH = 0;
        for (auto& i : imgs) { maxW = std::max(maxW, i.w); maxH = std::max(maxH, i.h); }
        for (auto& i : imgs) {
            if (o.layout == 0 && i.h != maxH) i = ScaleImage(i, std::max(1, (int)std::lround((double)i.w * maxH / i.h)), maxH);
            else if (o.layout != 0 && i.w != maxW) i = ScaleImage(i, maxW, std::max(1, (int)std::lround((double)i.h * maxW / i.w)));
        }
    }
    // column widths and row heights of the grid (side by side = one row, stacked = one column)
    std::vector<int> colW(cols, 0), rowH(rows, 0);
    for (int i = 0; i < n; i++) {
        colW[i % cols] = std::max(colW[i % cols], imgs[i].w);
        rowH[i / cols] = std::max(rowH[i / cols], imgs[i].h);
    }
    int W = 2 * o.margin + o.gap * (cols - 1), H = 2 * o.margin + o.gap * (rows - 1);
    for (int w : colW) W += w;
    for (int h : rowH) H += h;
    if ((long long)W * H > 400000000LL) return {};
    Image out(W, H, o.transparent ? 0 : FromColorref(o.background));
    int y = o.margin;
    for (int r = 0; r < rows; r++) {
        int x = o.margin;
        for (int c = 0; c < cols; c++) {
            int i = r * cols + c;
            if (i >= n) break;
            const Image& im = imgs[i];
            // alignment across the layout direction (side by side: vertical; stacked: horizontal; grid: both)
            int ox = 0, oy = 0;
            int fx = colW[c] - im.w, fy = rowH[r] - im.h;
            if (o.layout != 0) ox = o.align == 0 ? 0 : o.align == 1 ? fx / 2 : fx;
            if (o.layout != 1) oy = o.align == 0 ? 0 : o.align == 1 ? fy / 2 : fy;
            out.Blit(im, x + ox, y + oy, true);
            if (placed) placed->push_back({ x + ox, y + oy, x + ox + im.w, y + oy + im.h });
            x += colW[c] + o.gap;
        }
        y += rowH[r] + o.gap;
    }
    return out;
}

bool CombineDialog(HWND owner, const std::vector<Image>& imgs, CombineOptions& o) {
    using F = ui::Field;
    std::vector<F> f = {
        F::MakeLabel(Format(Tr(L"%zu imagens"), imgs.size())),
        F::MakeCombo(Tr(L"Disposição"), { Tr(L"Lado a lado"), Tr(L"Empilhadas"), Tr(L"Grade") }, o.layout),
        F::MakeInt(Tr(L"Colunas (grade)"), o.columns, 1, 20),
        F::MakeInt(Tr(L"Espaço entre as imagens (px)"), o.gap, 0, 500),
        F::MakeInt(Tr(L"Margem (px)"), o.margin, 0, 500),
        F::MakeCombo(Tr(L"Alinhamento"), { Tr(L"Início (topo/esquerda)"), Tr(L"Centro"), Tr(L"Fim (base/direita)") }, o.align),
        F::MakeCheck(Tr(L"Mesmo tamanho (ajusta altura ou largura)"), o.sameSize),
        F::MakeCheck(Tr(L"Fundo transparente"), o.transparent),
        F::MakeColor(Tr(L"Cor de fundo"), o.background),
    };
    // the preview works on reduced copies (fast while typing)
    std::vector<Image> mini;
    for (auto& i : imgs) mini.push_back(MakeThumbnail(i, 480, 480));
    auto read = [](std::vector<F>& v) {
        CombineOptions r;
        r.layout = v[1].value; r.columns = v[2].value; r.gap = v[3].value; r.margin = v[4].value; r.align = v[5].value;
        r.sameSize = v[6].value != 0; r.transparent = v[7].value != 0; r.background = v[8].color;
        return r;
    };
    auto preview = [&](std::vector<F>& v) {
        CombineOptions r = read(v);
        double k = imgs.empty() || imgs[0].w == 0 ? 1.0 : (double)mini[0].w / imgs[0].w;
        r.gap = (int)std::lround(r.gap * k); r.margin = (int)std::lround(r.margin * k);
        return CombineImages(mini, r);
    };
    if (!ui::RunFormPreview(owner, Tr(L"Combinar imagens"), f, preview, { 420, 320 })) return false;
    o = read(f);
    o.Save();
    return true;
}

// ================================================================ C11 automatic redaction
namespace {

bool Luhn(const std::wstring& digits) {
    int sum = 0; bool alt = false;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        int d = *it - L'0';
        if (alt) { d *= 2; if (d > 9) d -= 9; }
        sum += d; alt = !alt;
    }
    return digits.size() >= 13 && sum % 10 == 0;
}

std::wstring DigitsOf(const std::wstring& s) {
    std::wstring d;
    for (wchar_t c : s) if (c >= L'0' && c <= L'9') d += c;
    return d;
}

bool IbanValid(const std::wstring& s) {
    std::wstring t;
    for (wchar_t c : s) if (c != L' ') t += (wchar_t)towupper(c);
    if (t.size() < 15 || t.size() > 34) return false;
    t = t.substr(4) + t.substr(0, 4);
    int mod = 0;
    for (wchar_t c : t) {
        int v = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'A' && c <= L'Z' ? c - L'A' + 10 : -1;
        if (v < 0) return false;
        mod = v >= 10 ? (mod * 100 + v) % 97 : (mod * 10 + v) % 97;
    }
    return mod == 1;
}

// CPF check digits (Brazil)
bool CpfValid(const std::wstring& s) {
    std::wstring d = DigitsOf(s);
    if (d.size() != 11 || d.find_first_not_of(d[0]) == std::wstring::npos) return false;
    for (int k = 9; k <= 10; k++) {
        int sum = 0;
        for (int i = 0; i < k; i++) sum += (d[i] - L'0') * (k + 1 - i);
        int v = sum * 10 % 11 % 10;
        if (v != d[k] - L'0') return false;
    }
    return true;
}

struct Pattern { const wchar_t* kind; std::wregex re; std::function<bool(const std::wstring&)> check; };

const std::vector<Pattern>& Patterns() {
    using std::regex_constants::icase;
    static const std::vector<Pattern> p = {
        { TR_NOOP(L"Link com token"), std::wregex(LR"(https?://\S+?[?&#](?:token|key|apikey|api_key|access_token|auth|sig|signature|code|secret|password|pwd|session)=[^\s&]+\S*)", icase), nullptr },
        { TR_NOOP(L"Chave de API"), std::wregex(LR"((?:sk-[A-Za-z0-9_-]{16,}|AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9]{30,}|xox[baprs]-[A-Za-z0-9-]{10,}|AIza[0-9A-Za-z_-]{35}|eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{5,}))"), nullptr },
        { TR_NOOP(L"E-mail"), std::wregex(LR"([A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,})"), nullptr },
        { L"IBAN", std::wregex(LR"(\b[A-Z]{2}\d{2}(?: ?[A-Z0-9]{4}){2,7}(?: ?[A-Z0-9]{1,4})?\b)"), IbanValid },
        { L"CNPJ", std::wregex(LR"(\b\d{2}\.\d{3}\.\d{3}/\d{4}-\d{2}\b)"), nullptr },
        { L"CPF", std::wregex(LR"(\b\d{3}\.\d{3}\.\d{3}-\d{2}\b)"), CpfValid },
        { TR_NOOP(L"Cartão"), std::wregex(LR"(\b(?:\d[ -]?){12,18}\d\b)"), [](const std::wstring& s) { return Luhn(DigitsOf(s)); } },
        { TR_NOOP(L"Documento (SSN)"), std::wregex(LR"(\b\d{3}-\d{2}-\d{4}\b)"), nullptr },
        { TR_NOOP(L"Endereço IP"), std::wregex(LR"(\b(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)\b)"), nullptr },
        { TR_NOOP(L"Telefone"), std::wregex(LR"((?:\+\d{1,3}[ .-]?)?(?:\(\d{1,4}\)[ .-]?)?\d{2,5}[ .-]?\d{3,5}(?:[ .-]?\d{2,5})?)"),
          [](const std::wstring& s) { size_t n = DigitsOf(s).size(); return n >= 8 && n <= 15; } },
    };
    return p;
}

} // namespace

std::wstring SensitiveKind(const std::wstring& textIn) {
    std::wstring text = Trim(textIn);
    for (auto& p : Patterns()) {
        std::wsmatch m;
        if (std::regex_match(text, m, p.re) && (!p.check || p.check(text))) return p.kind;
    }
    return L"";
}

std::vector<RedactItem> FindSensitive(const std::vector<OcrLine>& lines) {
    std::vector<RedactItem> out;
    for (auto& line : lines) {
        // the line rebuilt from its words (single spaces), with each word's range, so a match maps back to boxes
        std::wstring t;
        std::vector<std::pair<size_t, size_t>> ranges;
        for (auto& w : line.words) {
            if (!t.empty()) t += L' ';
            ranges.push_back({ t.size(), t.size() + w.text.size() });
            t += w.text;
        }
        std::vector<bool> used(t.size(), false);
        for (auto& p : Patterns()) {
            for (std::wsregex_iterator it(t.begin(), t.end(), p.re), end; it != end; ++it) {
                size_t a = (size_t)it->position(), b = a + (size_t)it->length();
                std::wstring s = it->str();
                // trailing punctuation picked up by the greedy patterns
                while (!s.empty() && wcschr(L".,;:)]}>\"'", s.back())) { s.pop_back(); b--; }
                if (s.empty() || (p.check && !p.check(s))) continue;
                bool overlap = false;
                for (size_t i = a; i < b && !overlap; i++) overlap = used[i];
                if (overlap) continue;
                for (size_t i = a; i < b; i++) used[i] = true;
                RedactItem item;
                item.kind = Tr(p.kind);
                item.text = s;
                RECT u{ LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
                for (size_t k = 0; k < ranges.size(); k++) {
                    if (ranges[k].second <= a || ranges[k].first >= b) continue;
                    const RECT& r = line.words[k].box;
                    u.left = std::min(u.left, r.left); u.top = std::min(u.top, r.top);
                    u.right = std::max(u.right, r.right); u.bottom = std::max(u.bottom, r.bottom);
                }
                if (u.left > u.right) continue;
                InflateRect(&u, 3, 3);
                item.box = u;
                out.push_back(item);
            }
        }
    }
    return out;
}

static void ApplyRedaction(Image& img, RECT r, int style) {
    RECT b{ 0, 0, img.w, img.h };
    if (!IntersectRect(&r, &r, &b)) return;
    int h = r.bottom - r.top;
    if (style == 0) { fx::BoxBlur(img, r, std::max(4, h / 3)); fx::BoxBlur(img, r, std::max(4, h / 3)); }
    else if (style == 1) fx::Pixelate(img, r, std::max(6, h / 3));
    else img.FillRect(r, 0xFF000000);
}

bool RedactDialog(HWND owner, const Image& img, std::vector<RedactItem>& items, int& style) {
    using F = ui::Field;
    std::vector<F> f = { F::MakeCombo(Tr(L"Ocultar com"), { Tr(L"Desfoque"), Tr(L"Pixelização"), Tr(L"Tarja preta") }, style),
                         F::MakeLabel(Format(Tr(L"%zu itens encontrados — desmarque o que deve ficar visível:"), items.size())) };
    // one check per item (the first 40); beyond that, one per kind
    const size_t perItem = std::min<size_t>(items.size(), 40);
    for (size_t i = 0; i < perItem; i++) {
        std::wstring t = items[i].text;
        if (t.size() > 34) t = t.substr(0, 32) + L"…";
        f.push_back(F::MakeCheck(items[i].kind + L": " + t, items[i].on));
    }
    std::vector<std::wstring> restKinds;
    for (size_t i = perItem; i < items.size(); i++)
        if (std::find(restKinds.begin(), restKinds.end(), items[i].kind) == restKinds.end()) restKinds.push_back(items[i].kind);
    for (auto& k : restKinds) f.push_back(F::MakeCheck(Format(Tr(L"Outros: %s"), k.c_str()), true));
    auto apply = [&](std::vector<F>& v) {
        for (size_t i = 0; i < perItem; i++) items[i].on = v[2 + i].value != 0;
        for (size_t i = perItem; i < items.size(); i++) {
            size_t k = std::find(restKinds.begin(), restKinds.end(), items[i].kind) - restKinds.begin();
            items[i].on = v[2 + perItem + k].value != 0;
        }
    };
    Image mini = MakeThumbnail(img, 900, 700);
    const double k = img.w ? (double)mini.w / img.w : 1.0;
    auto preview = [&](std::vector<F>& v) {
        apply(v);
        Image out = mini;
        auto bmp = out.WrapGdip();
        for (auto& it : items) {
            RECT r{ (LONG)(it.box.left * k), (LONG)(it.box.top * k), (LONG)std::ceil(it.box.right * k), (LONG)std::ceil(it.box.bottom * k) };
            if (it.on) ApplyRedaction(out, r, v[0].value);
        }
        Graphics g(bmp.get());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        for (auto& it : items) {
            RectF r((REAL)(it.box.left * k), (REAL)(it.box.top * k), (REAL)((it.box.right - it.box.left) * k), (REAL)((it.box.bottom - it.box.top) * k));
            Pen p(it.on ? Color(230, 229, 50, 45) : Color(200, 120, 120, 128), 2);
            if (!it.on) p.SetDashStyle(DashStyleDash);
            g.DrawRectangle(&p, r);
        }
        return out;
    };
    if (!ui::RunFormPreview(owner, Tr(L"Ocultar dados sensíveis"), f, preview, { 480, 360 })) return false;
    apply(f);
    style = f[0].value;
    return true;
}

// ================================================================ C17 difference image
Image DiffImage(const Image& a, const Image& b, int tolerance, int* changedPixels, RECT* changedBox) {
    int W = std::max(a.w, b.w), H = std::max(a.h, b.h);
    Image out(W, H, 0xFFFFFFFF);
    int changed = 0;
    RECT box{ LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            bool ina = x < a.w && y < a.h, inb = x < b.w && y < b.h;
            uint32_t pa = ina ? a.at(x, y) : 0, pb = inb ? b.at(x, y) : 0;
            bool diff = ina != inb || std::abs(CR(pa) - CR(pb)) > tolerance || std::abs(CG(pa) - CG(pb)) > tolerance ||
                        std::abs(CB(pa) - CB(pb)) > tolerance || std::abs(CA(pa) - CA(pb)) > tolerance;
            if (diff) {
                out.at(x, y) = 0xFFE5322D;
                changed++;
                box.left = std::min<LONG>(box.left, x); box.top = std::min<LONG>(box.top, y);
                box.right = std::max<LONG>(box.right, x + 1); box.bottom = std::max<LONG>(box.bottom, y + 1);
            } else {
                // the unchanged image, faded (gray at 25%)
                int l = (CR(pa) * 30 + CG(pa) * 59 + CB(pa) * 11) / 100;
                int v = 255 - (255 - l) / 4;
                out.at(x, y) = ARGB(255, v, v, v);
            }
        }
    if (changedPixels) *changedPixels = changed;
    if (changedBox) *changedBox = changed ? box : RECT{};
    return out;
}

// ================================================================ C23 draw & hold shape recognition
static float SegDist(PointF p, PointF a, PointF b) {
    float dx = b.X - a.X, dy = b.Y - a.Y, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / l2, 0.0f, 1.0f) : 0;
    return std::hypot(p.X - (a.X + t * dx), p.Y - (a.Y + t * dy));
}

int RecognizeStroke(const std::vector<PointF>& pts, PointF& a, PointF& b) {
    if (pts.size() < 4) return RecNone;
    float x0 = pts[0].X, y0 = pts[0].Y, x1 = x0, y1 = y0, L = 0;
    for (size_t i = 0; i < pts.size(); i++) {
        x0 = std::min(x0, pts[i].X); y0 = std::min(y0, pts[i].Y); x1 = std::max(x1, pts[i].X); y1 = std::max(y1, pts[i].Y);
        if (i) L += std::hypot(pts[i].X - pts[i - 1].X, pts[i].Y - pts[i - 1].Y);
    }
    float w = x1 - x0, h = y1 - y0, diag = std::hypot(w, h);
    if (diag < 16 || L < 16) return RecNone;
    PointF s = pts.front(), e = pts.back();
    float chord = std::hypot(e.X - s.X, e.Y - s.Y);
    // straight line: every point close to the chord
    float maxDev = 0;
    for (auto& p : pts) maxDev = std::max(maxDev, SegDist(p, s, e));
    if (chord > 20 && maxDev < std::max(4.0f, chord * 0.05f) && L < chord * 1.25f) { a = s; b = e; return RecLine; }
    // arrow: straight to the tip (the farthest point from the start), then a short hook drawn for the head
    size_t tip = 0;
    float farthest = 0;
    for (size_t i = 0; i < pts.size(); i++) { float d = std::hypot(pts[i].X - s.X, pts[i].Y - s.Y); if (d > farthest) { farthest = d; tip = i; } }
    if (farthest > 30 && tip > pts.size() / 2 && tip + 2 < pts.size()) {
        float dev = 0, tail = 0;
        for (size_t i = 0; i <= tip; i++) dev = std::max(dev, SegDist(pts[i], s, pts[tip]));
        for (size_t i = tip + 1; i < pts.size(); i++) tail += std::hypot(pts[i].X - pts[i - 1].X, pts[i].Y - pts[i - 1].Y);
        if (dev < std::max(5.0f, farthest * 0.07f) && tail > farthest * 0.08f && tail < farthest * 0.9f) { a = s; b = pts[tip]; return RecArrow; }
    }
    // closed outlines: the end comes back near the start
    if (chord > diag * 0.25f || w < 12 || h < 12) return RecNone;
    double area = 0;                                       // shoelace on the closed stroke
    for (size_t i = 0; i < pts.size(); i++) {
        const PointF& p = pts[i]; const PointF& q = pts[(i + 1) % pts.size()];
        area += (double)p.X * q.Y - (double)q.X * p.Y;
    }
    area = std::fabs(area) / 2;
    a = PointF(x0, y0); b = PointF(x1, y1);
    // Measured in the smallest box around the stroke at any angle (a hand-drawn square is rarely level, and in the
    // level box a tilted square looks round): the share of the box it fills (rectangle ≈ 1, ellipse ≈ 0.785,
    // triangle ≈ 0.5), the radial error against the ellipse inscribed in that box (a square ≈ 0.148, an ellipse ≈ 0)
    // and how many of the box's corners the stroke reaches (an ellipse stays about 0.21 of the size away).
    std::vector<PointF> hull, sorted = pts;                // convex hull (monotone chain)
    std::sort(sorted.begin(), sorted.end(), [](const PointF& p, const PointF& q) { return p.X < q.X || (p.X == q.X && p.Y < q.Y); });
    auto cross = [](const PointF& o, const PointF& p, const PointF& q) { return (p.X - o.X) * (q.Y - o.Y) - (p.Y - o.Y) * (q.X - o.X); };
    for (int pass = 0; pass < 2; pass++) {
        size_t base = hull.size();
        for (auto& p : sorted) {
            while (hull.size() >= base + 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
            hull.push_back(p);
        }
        hull.pop_back();
        std::reverse(sorted.begin(), sorted.end());
    }
    float bestArea = 1e30f, ux = 1, uy = 0;
    for (size_t i = 0; i < hull.size(); i++) {              // the smallest box has a side along a hull edge
        PointF p = hull[i], q = hull[(i + 1) % hull.size()];
        float len = std::hypot(q.X - p.X, q.Y - p.Y);
        if (len < 1e-3f) continue;
        float dx = (q.X - p.X) / len, dy = (q.Y - p.Y) / len, u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        for (auto& r : hull) {
            float u = r.X * dx + r.Y * dy, v = -r.X * dy + r.Y * dx;
            u0 = std::min(u0, u); u1 = std::max(u1, u); v0 = std::min(v0, v); v1 = std::max(v1, v);
        }
        if ((u1 - u0) * (v1 - v0) < bestArea) { bestArea = (u1 - u0) * (v1 - v0); ux = dx; uy = dy; }
    }
    float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
    for (auto& p : pts) {
        float u = p.X * ux + p.Y * uy, v = -p.X * uy + p.Y * ux;
        u0 = std::min(u0, u); u1 = std::max(u1, u); v0 = std::min(v0, v); v1 = std::max(v1, v);
    }
    float bw = std::max(1.0f, u1 - u0), bh = std::max(1.0f, v1 - v0);
    double fill = area / ((double)bw * bh);
    if (fill < 0.62) return fill > 0.3 ? RecTriangle : RecNone;
    float err = 0, cn[4] = { 1e9f, 1e9f, 1e9f, 1e9f };
    for (auto& p : pts) {
        float nu = (p.X * ux + p.Y * uy - u0) / bw, nv = (-p.X * uy + p.Y * ux - v0) / bh;      // 0..1 in the box
        err += std::fabs(std::hypot(nu * 2 - 1, nv * 2 - 1) - 1);
        for (int k = 0; k < 4; k++) cn[k] = std::min(cn[k], std::hypot(nu - (k & 1), nv - (k >> 1)));
    }
    err /= pts.size();
    int corners = (int)std::count_if(cn, cn + 4, [](float d) { return d < 0.14f; });
    return corners >= 3 || fill > 0.87 || err >= 0.11f ? RecRect : RecEllipse;
}

// ================================================================ C12 text under an area
std::vector<RectF> WordsInArea(const std::vector<OcrLine>& lines, RectF area) {
    std::vector<RectF> out;
    for (auto& l : lines)
        for (auto& w : l.words) {
            RectF r((REAL)w.box.left, (REAL)w.box.top, (REAL)(w.box.right - w.box.left), (REAL)(w.box.bottom - w.box.top)), x;
            if (r.Width <= 0 || r.Height <= 0 || !RectF::Intersect(x, r, area)) continue;
            if (x.Width * x.Height * 2 < r.Width * r.Height) continue;
            out.push_back(x);
        }
    return out;
}

std::vector<RectF> SnapToTextLines(const std::vector<OcrLine>& lines, RectF area) {
    std::vector<RectF> out;
    for (auto& l : lines) {
        RectF u;
        bool any = false;
        for (auto& w : l.words) {
            RectF r((REAL)w.box.left, (REAL)w.box.top, (REAL)(w.box.right - w.box.left), (REAL)(w.box.bottom - w.box.top)), x;
            if (r.Width <= 0 || r.Height <= 0 || !RectF::Intersect(x, r, area)) continue;
            // touched: a good part of the word, or most of the line's height under the stroke
            if (x.Width * x.Height * 2.5f < r.Width * r.Height && x.Height < r.Height * 0.5f) continue;
            if (!any) u = r; else RectF::Union(u, u, r);
            any = true;
        }
        if (!any) continue;
        float pad = std::max(1.0f, u.Height * 0.12f);
        u.Inflate(pad, pad);
        out.push_back(u);
    }
    return out;
}

// ================================================================ C26 date and time stamp
std::wstring DateStampFormat() { return RegGetString(L"DateStampFormat", L"", L"Editor"); }
void SetDateStampFormat(const std::wstring& f) { RegSetString(L"DateStampFormat", f, L"Editor"); }

static std::wstring UiLocale() {
    std::wstring c = i18n::Current();
    if (c == L"pt") return L"pt-BR";
    if (c == L"zh") return L"zh-CN";
    return c;
}

std::wstring DateStampText(const std::wstring& format, const SYSTEMTIME& t) {
    std::wstring loc = UiLocale();
    auto dateFmt = [&](const wchar_t* pic) {
        wchar_t b[128] = L"";
        if (!GetDateFormatEx(loc.c_str(), 0, &t, pic, b, 128, nullptr)) GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &t, pic, b, 128, nullptr);
        return std::wstring(b);
    };
    if (Trim(format).empty()) {
        wchar_t d[128] = L"", tm[64] = L"";
        if (!GetDateFormatEx(loc.c_str(), DATE_SHORTDATE, &t, nullptr, d, 128, nullptr)) GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &t, nullptr, d, 128, nullptr);
        if (!GetTimeFormatEx(loc.c_str(), 0, &t, nullptr, tm, 64)) GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &t, nullptr, tm, 64);
        return std::wstring(d) + L" " + tm;
    }
    std::wstring out;
    for (size_t i = 0; i < format.size();) {
        wchar_t c = format[i];
        if (c == L'\'') {                               // literal text
            size_t e = format.find(L'\'', i + 1);
            if (e == std::wstring::npos) e = format.size();
            out += format.substr(i + 1, e - i - 1);
            i = e + 1;
            continue;
        }
        size_t n = 1;
        while (i + n < format.size() && format[i + n] == c) n++;
        auto two = [](int v) { return Format(L"%02d", v); };
        switch (c) {
        case L'y': out += n >= 4 ? Format(L"%04d", t.wYear) : two(t.wYear % 100); break;
        case L'M': out += n >= 4 ? dateFmt(L"MMMM") : n == 3 ? dateFmt(L"MMM") : n == 2 ? two(t.wMonth) : std::to_wstring(t.wMonth); break;
        case L'd': out += n >= 4 ? dateFmt(L"dddd") : n == 3 ? dateFmt(L"ddd") : n == 2 ? two(t.wDay) : std::to_wstring(t.wDay); break;
        case L'H': out += n >= 2 ? two(t.wHour) : std::to_wstring(t.wHour); break;
        case L'h': { int h12 = t.wHour % 12 ? t.wHour % 12 : 12; out += n >= 2 ? two(h12) : std::to_wstring(h12); break; }
        case L'm': out += n >= 2 ? two(t.wMinute) : std::to_wstring(t.wMinute); break;
        case L's': out += n >= 2 ? two(t.wSecond) : std::to_wstring(t.wSecond); break;
        case L't': out += t.wHour < 12 ? L"AM" : L"PM"; break;
        default: out += format.substr(i, n); break;
        }
        i += n;
    }
    return out;
}

// ================================================================ C27 background removal
Image RemoveBackground(const Image& src, int tolerance, int feather) {
    if (src.empty()) return src;
    const int W = src.w, H = src.h;
    const float T = 6.0f + std::clamp(tolerance, 1, 100) * 1.4f;      // color distance (RGB, 0..441)
    auto dist = [](uint32_t a, uint32_t b) {
        float dr = (float)CR(a) - CR(b), dg = (float)CG(a) - CG(b), db = (float)CB(a) - CB(b);
        return std::sqrt(dr * dr + dg * dg + db * db);
    };
    // 1. background colors: the dominant colors of the image border (quantized buckets covering most of it)
    std::map<uint32_t, int> buckets;
    std::map<uint32_t, std::array<long long, 4>> sums;
    auto addBorder = [&](uint32_t p) {
        if (CA(p) < 16) return;
        uint32_t key = (uint32_t)((CR(p) >> 4) << 8 | (CG(p) >> 4) << 4 | (CB(p) >> 4));
        buckets[key]++;
        auto& s = sums[key]; s[0] += CR(p); s[1] += CG(p); s[2] += CB(p); s[3]++;
    };
    for (int x = 0; x < W; x++) { addBorder(src.at(x, 0)); addBorder(src.at(x, H - 1)); }
    for (int y = 0; y < H; y++) { addBorder(src.at(0, y)); addBorder(src.at(W - 1, y)); }
    std::vector<std::pair<int, uint32_t>> order;
    int total = 0;
    for (auto& [k, n] : buckets) { order.push_back({ n, k }); total += n; }
    std::sort(order.rbegin(), order.rend());
    std::vector<uint32_t> bg;
    int covered = 0;
    for (auto& [n, k] : order) {
        auto& s = sums[k];
        bg.push_back(ARGB(255, (int)(s[0] / s[3]), (int)(s[1] / s[3]), (int)(s[2] / s[3])));
        covered += n;
        if (covered >= total * 85 / 100 || bg.size() >= 12) break;
    }
    auto nearBg = [&](uint32_t p) { float d = 1e9f; for (uint32_t b : bg) d = std::min(d, dist(p, b)); return d; };
    // 2. flood from the border through pixels that look like the background (or continue a smooth gradient of it)
    std::vector<uint8_t> isBg((size_t)W * H, 0);
    std::deque<int> q;
    auto seed = [&](int x, int y) {
        int i = y * W + x;
        if (isBg[i]) return;
        uint32_t p = src.px[i];
        if (CA(p) < 16 || nearBg(p) < T) { isBg[i] = 1; q.push_back(i); }
    };
    for (int x = 0; x < W; x++) { seed(x, 0); seed(x, H - 1); }
    for (int y = 0; y < H; y++) { seed(0, y); seed(W - 1, y); }
    while (!q.empty()) {
        int i = q.front(); q.pop_front();
        int x = i % W, y = i / W;
        const int nx[4] = { x - 1, x + 1, x, x }, ny[4] = { y, y, y - 1, y + 1 };
        for (int k = 0; k < 4; k++) {
            if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= H) continue;
            int j = ny[k] * W + nx[k];
            if (isBg[j]) continue;
            uint32_t p = src.px[j];
            float d = nearBg(p);
            if (CA(p) < 16 || d < T || (dist(p, src.px[i]) < T * 0.25f && d < T * 2.2f)) { isBg[j] = 1; q.push_back(j); }
        }
    }
    // 3. keep the main subject: foreground pieces much smaller than the largest become background
    std::vector<int> comp((size_t)W * H, -1);
    std::vector<int> sizes;
    for (int i = 0; i < W * H; i++) {
        if (isBg[i] || comp[i] >= 0) continue;
        int id = (int)sizes.size(), n = 0;
        q.push_back(i); comp[i] = id;
        while (!q.empty()) {
            int c = q.front(); q.pop_front(); n++;
            int x = c % W, y = c / W;
            const int nx[4] = { x - 1, x + 1, x, x }, ny[4] = { y, y, y - 1, y + 1 };
            for (int k = 0; k < 4; k++) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= H) continue;
                int j = ny[k] * W + nx[k];
                if (!isBg[j] && comp[j] < 0) { comp[j] = id; q.push_back(j); }
            }
        }
        sizes.push_back(n);
    }
    if (sizes.empty()) return src;                       // all background: nothing recognizable
    int largest = *std::max_element(sizes.begin(), sizes.end());
    // 4. alpha mask (soft edge: box blur of the mask over 'feather' px)
    std::vector<float> a((size_t)W * H);
    for (int i = 0; i < W * H; i++) a[i] = (!isBg[i] && sizes[comp[i]] * 50 >= largest) ? 1.0f : 0.0f;
    int r = std::clamp(feather, 0, 20);
    if (r > 0) {
        std::vector<float> tmp(a.size());
        for (int y = 0; y < H; y++) {
            float acc = 0; int cnt = 0;
            for (int x = -r; x < W; x++) {
                if (x + r < W) { acc += a[y * W + x + r]; cnt++; }
                if (x - r - 1 >= 0) { acc -= a[y * W + x - r - 1]; cnt--; }
                if (x >= 0) tmp[y * W + x] = acc / cnt;
            }
        }
        for (int x = 0; x < W; x++) {
            float acc = 0; int cnt = 0;
            for (int y = -r; y < H; y++) {
                if (y + r < H) { acc += tmp[(y + r) * W + x]; cnt++; }
                if (y - r - 1 >= 0) { acc -= tmp[(y - r - 1) * W + x]; cnt--; }
                if (y >= 0) a[y * W + x] = std::min(a[y * W + x] > 0 ? 1.0f : 0.0f, acc / cnt) * 0.5f + acc / cnt * 0.5f;
            }
        }
    }
    Image out = src;
    for (int i = 0; i < W * H; i++) {
        uint32_t p = out.px[i];
        int al = (int)std::lround(CA(p) * std::clamp(a[i], 0.0f, 1.0f));
        out.px[i] = (p & 0x00FFFFFF) | ((uint32_t)al << 24);
    }
    return out;
}

// ================================================================ C34 split long images
bool SplitDialog(HWND owner, const Image& img, std::vector<int>& cuts, bool& vertical) {
    using F = ui::Field;
    bool tall = img.h >= img.w;
    std::vector<F> f = {
        F::MakeLabel(Format(Tr(L"Imagem de %d × %d px"), img.w, img.h)),
        F::MakeCombo(Tr(L"Cortar"), { Tr(L"Na horizontal (partes empilhadas)"), Tr(L"Na vertical (partes lado a lado)") }, tall ? 0 : 1),
        F::MakeCombo(Tr(L"Dividir por"), { Tr(L"Tamanho de cada parte (px)"), Tr(L"Número de partes") }, 1),
        F::MakeInt(Tr(L"Tamanho (px)"), std::max(100, (tall ? img.h : img.w) / 2), 16, 100000),
        F::MakeInt(Tr(L"Número de partes"), 2, 2, 100),
        F::MakeCheck(Tr(L"Não cortar linhas de texto (quebras inteligentes)"), true),
    };
    auto compute = [&](std::vector<F>& v, std::vector<int>& out, bool& vert) {
        vert = v[1].value == 1;
        int len = vert ? img.w : img.h;
        int size = v[2].value == 0 ? v[3].value : (len + v[4].value - 1) / std::max(1, v[4].value);
        size = std::max(16, size);
        out.clear();
        const bool byCount = v[2].value == 1;
        if (v[5].value) {
            // quiet rows (or columns: the image turned a quarter, its rows are the columns)
            Image t = vert ? fx::Rotate90(img, true) : img;
            out = byCount ? SmartCuts(t, std::max(2, v[4].value)) : SmartBreaks(t, size);
        } else if (byCount) for (int i = 1; i < v[4].value; i++) out.push_back((int)((long long)len * i / v[4].value));
        else for (int p = size; p < len; p += size) out.push_back(p);
    };
    Image mini = MakeThumbnail(img, 700, 700);
    double k = img.w ? (double)mini.w / img.w : 1.0;
    auto preview = [&](std::vector<F>& v) {
        std::vector<int> c; bool vert;
        compute(v, c, vert);
        Image out = mini;
        auto bmp = out.WrapGdip();
        Graphics g(bmp.get());
        Pen p(Color(255, 229, 50, 45), 2);
        p.SetDashStyle(DashStyleDash);
        for (int x : c) {
            REAL t = (REAL)(x * k);
            if (vert) g.DrawLine(&p, t, 0.0f, t, (REAL)out.h); else g.DrawLine(&p, 0.0f, t, (REAL)out.w, t);
        }
        return out;
    };
    if (!ui::RunFormPreview(owner, Tr(L"Dividir imagem"), f, preview, { 360, 360 })) return false;
    compute(f, cuts, vertical);
    return !cuts.empty();
}

// ================================================================ C36 contrast checker
std::wstring ContrastReport(COLORREF fg, COLORREF bg) {
    double r = color::ContrastRatio(fg, bg);
    auto v = [&](double need) -> std::wstring { return r + 1e-9 >= need ? Tr(L"passa") : Tr(L"falha"); };
    return Format(Tr(L"Contraste %.2f:1\n\nTexto normal — AA (4,5:1): %s · AAA (7:1): %s\nTexto grande — AA (3:1): %s · AAA (4,5:1): %s\nComponentes e gráficos — AA (3:1): %s"),
                  r, v(4.5).c_str(), v(7).c_str(), v(3).c_str(), v(4.5).c_str(), v(3).c_str());
}

namespace {
struct ContrastState {
    COLORREF fg, bg;
    HWND h = nullptr, owner = nullptr;
    bool done = false, picking = false;
    int dpi = 96;
    RECT swFg{}, swBg{}, swap{}, close{}, sample{};
};

LRESULT CALLBACK ContrastProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = (ContrastState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    auto S = [&](int v) { return ui::Scale(v, st ? st->dpi : 96); };
    switch (m) {
    case WM_CREATE: {
        st = (ContrastState*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT cr; GetClientRect(h, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bb = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(mem, bb);
        HBRUSH bgb = CreateSolidBrush(RGB(250, 250, 252)); FillRect(mem, &cr, bgb); DeleteObject(bgb);
        SetBkMode(mem, TRANSPARENT);
        HFONT f = ui::UiFont(st->dpi, 9), fb = ui::UiFont(st->dpi, 9, true);
        int pad = S(14), y = pad, sw = (cr.right - 3 * pad) / 2;
        auto swatch = [&](RECT r, COLORREF c, const std::wstring& label) {
            HGDIOBJ of = SelectObject(mem, f);
            SetTextColor(mem, RGB(60, 64, 72));
            RECT lr{ r.left, r.top, r.right, r.top + S(18) };
            DrawTextW(mem, label.c_str(), -1, &lr, DT_SINGLELINE | DT_LEFT);
            RECT box{ r.left, r.top + S(20), r.left + S(34), r.top + S(54) };
            ui::FillRoundRect(mem, box, S(4), c, RGB(180, 182, 190));
            RECT tr{ box.right + S(8), box.top, r.right, box.bottom };
            SelectObject(mem, fb);
            DrawTextW(mem, (color::ToText(c, color::Hex) + L"\n" + Tr(L"clique para mudar")).c_str(), -1, &tr, DT_LEFT);
            SelectObject(mem, of);
        };
        st->swFg = { pad, y, pad + sw, y + S(54) };
        st->swBg = { 2 * pad + sw, y, 2 * pad + 2 * sw, y + S(54) };
        swatch(st->swFg, st->fg, Tr(L"Texto (primeiro plano)"));
        swatch(st->swBg, st->bg, Tr(L"Fundo"));
        y += S(62);
        // sample text in both sizes
        st->sample = { pad, y, cr.right - pad, y + S(70) };
        ui::FillRoundRect(mem, st->sample, S(6), st->bg, RGB(200, 202, 208));
        SetTextColor(mem, st->fg);
        HFONT big = CreateFontW(-S(24), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, i18n::UiFontFace());
        HFONT normal = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, i18n::UiFontFace());
        HGDIOBJ of = SelectObject(mem, big);
        RECT t1{ st->sample.left + S(12), st->sample.top + S(6), st->sample.right, st->sample.top + S(38) };
        DrawTextW(mem, Tr(L"Texto grande Aa"), -1, &t1, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        SelectObject(mem, normal);
        RECT t2{ st->sample.left + S(12), st->sample.top + S(40), st->sample.right, st->sample.bottom - S(6) };
        DrawTextW(mem, Tr(L"Texto normal — o rato roeu a roupa do rei"), -1, &t2, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(mem, of); DeleteObject(big); DeleteObject(normal);
        y = st->sample.bottom + S(10);
        // verdicts
        SelectObject(mem, f);
        SetTextColor(mem, RGB(30, 32, 36));
        RECT rr{ pad, y, cr.right - pad, cr.bottom - S(48) };
        DrawTextW(mem, ContrastReport(st->fg, st->bg).c_str(), -1, &rr, DT_LEFT | DT_WORDBREAK);
        // buttons
        st->swap = { pad, cr.bottom - S(40), pad + S(150), cr.bottom - S(12) };
        st->close = { cr.right - pad - S(100), cr.bottom - S(40), cr.right - pad, cr.bottom - S(12) };
        ui::FillRoundRect(mem, st->swap, S(4), RGB(255, 255, 255), RGB(190, 192, 200));
        ui::FillRoundRect(mem, st->close, S(4), RGB(0, 103, 192), RGB(0, 90, 170));
        ui::DrawTextCentered(mem, Tr(L"Inverter as cores"), st->swap, RGB(30, 32, 36), f);
        ui::DrawTextCentered(mem, Tr(L"Fechar"), st->close, RGB(255, 255, 255), f);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bb); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ (short)LOWORD(l), (short)HIWORD(l) };
        if (PtInRect(&st->swap, p)) { std::swap(st->fg, st->bg); InvalidateRect(h, nullptr, FALSE); }
        else if (PtInRect(&st->close, p)) st->done = true;
        else if (PtInRect(&st->swFg, p) || PtInRect(&st->swBg, p)) {
            COLORREF* target = PtInRect(&st->swFg, p) ? &st->fg : &st->bg;
            HMENU mm = CreatePopupMenu();
            AppendMenuW(mm, MF_STRING, 1, Tr(L"Conta-gotas na tela…"));
            AppendMenuW(mm, MF_STRING, 2, Tr(L"Escolher cor…"));
            AppendMenuW(mm, MF_STRING, 3, Tr(L"Copiar HEX"));
            POINT sp = p; ClientToScreen(h, &sp);
            int cmd = TrackPopupMenu(mm, TPM_RETURNCMD | TPM_RIGHTBUTTON, sp.x, sp.y, 0, h, nullptr);
            DestroyMenu(mm);
            if (cmd == 1 && !st->picking) {
                st->picking = true;
                ShowWindow(h, SW_HIDE);
                DwmFlush();                       // the frozen screen must not show this window
                Sleep(120);
                RunScreenColorPicker([st, target](bool ok, COLORREF c) {
                    if (ok) *target = c;
                    st->picking = false;
                    if (st->h) { ShowWindow(st->h, SW_SHOW); SetForegroundWindow(st->h); InvalidateRect(st->h, nullptr, FALSE); }
                });
            } else if (cmd == 2) { if (PickColor(h, *target)) InvalidateRect(h, nullptr, FALSE); }
            else if (cmd == 3) color::CopyText(h, color::ToText(*target, color::Hex));
        }
        return 0;
    }
    case WM_KEYDOWN: if (w == VK_ESCAPE) st->done = true; return 0;
    case WM_CLOSE: st->done = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace

void DominantColors(const Image& img, COLORREF& fg, COLORREF& bg) {
    std::map<uint32_t, int> hist;                 // 4 bits per channel
    for (uint32_t p : img.px) if (CA(p) > 128) hist[(uint32_t)((CR(p) >> 4) << 8 | (CG(p) >> 4) << 4 | (CB(p) >> 4))]++;
    if (hist.empty()) return;
    auto color = [](uint32_t k) { return RGB(((k >> 8) & 15) * 17, ((k >> 4) & 15) * 17, (k & 15) * 17); };
    auto best = std::max_element(hist.begin(), hist.end(), [](auto& a, auto& b) { return a.second < b.second; });
    bg = color(best->first);
    double bestScore = -1;
    fg = color::ContrastRatio(bg, RGB(0, 0, 0)) > color::ContrastRatio(bg, RGB(255, 255, 255)) ? RGB(0, 0, 0) : RGB(255, 255, 255);
    for (auto& [k, n] : hist) {
        double cr = color::ContrastRatio(bg, color(k));
        if (cr < 1.6 || n * 400 < (int)img.px.size()) continue;      // too close to the background, or a stray pixel
        double score = n * std::log(cr);
        if (score > bestScore) { bestScore = score; fg = color(k); }
    }
}

void ShowContrastChecker(HWND owner, COLORREF fg, COLORREF bg) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ContrastProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SnapnabContrast";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        reg = true;
    }
    ContrastState st{ fg, bg };
    st.owner = owner;
    st.dpi = owner ? (int)GetDpiForWindow(owner) : 96;
    int W = ui::Scale(460, st.dpi), H = ui::Scale(380, st.dpi);
    RECT orc; GetWindowRect(owner ? owner : GetDesktopWindow(), &orc);
    RECT wr{ 0, 0, W, H };
    AdjustWindowRectExForDpi(&wr, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, st.dpi);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    st.h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SnapnabContrast", Tr(L"Verificador de contraste (WCAG)"), WS_CAPTION | WS_SYSMENU,
                           (orc.left + orc.right - ww) / 2, (orc.top + orc.bottom - wh) / 2, ww, wh, owner, nullptr, GetModuleHandleW(nullptr), &st);
    if (!st.h) return;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(st.h, SW_SHOW);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && msg.hwnd == st.h) { st.done = true; continue; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // a color pick still running hands its result to a closed window: forget the window first
    HWND h = st.h; st.h = nullptr;
    while (st.picking && GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    DestroyWindow(h);
}

} // namespace snap::ed
