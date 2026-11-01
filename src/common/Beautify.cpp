#include "Beautify.h"
#include "Effects.h"
#include "Ui.h"
#include <cmath>

namespace snap::fx {

XmlNode BeautifyOptions::ToXml(const char* name) const {
    XmlNode n(name);
    n.seti("Background", background).setW("Color1", ColorToHex(color1)).setW("Color2", ColorToHex(color2)).seti("Angle", angle)
     .setW("Image", imagePath).seti("Padding", padding).seti("Radius", radius).seti("Shadow", shadow).seti("ShadowSize", shadowSize)
     .seti("Align", align).seti("Aspect", aspect).seti("AutoBalance", autoBalance);
    return n;
}

BeautifyOptions BeautifyOptions::FromXml(const XmlNode& n) {
    BeautifyOptions o;
    o.background = std::clamp(n.geti("Background", o.background), 0, 4);
    o.color1 = HexToColor(n.getW("Color1"), o.color1);
    o.color2 = HexToColor(n.getW("Color2"), o.color2);
    o.angle = n.geti("Angle", o.angle) % 360;
    o.imagePath = n.getW("Image");
    o.padding = std::clamp(n.geti("Padding", o.padding), 0, 1000);
    o.radius = std::clamp(n.geti("Radius", o.radius), 0, 200);
    o.shadow = std::clamp(n.geti("Shadow", o.shadow), 0, 100);
    o.shadowSize = std::clamp(n.geti("ShadowSize", o.shadowSize), 0, 200);
    o.align = std::clamp(n.geti("Align", o.align), 0, 8);
    o.aspect = std::clamp(n.geti("Aspect", o.aspect), 0, 4);
    o.autoBalance = n.getb("AutoBalance", o.autoBalance);
    return o;
}

const wchar_t* AspectName(int a) {
    switch (a) {
    case 1: return L"16:9";
    case 2: return L"4:3";
    case 3: return L"1:1";
    case 4: return L"9:16";
    default: return Tr(L"Livre");
    }
}

std::wstring WallpaperPath() {
    wchar_t b[MAX_PATH * 2] = L"";
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH * 2, b, 0) && b[0] && FileExists(b)) return b;
    // the copy Windows keeps (also when the wallpaper came from a folder that is gone)
    wchar_t ad[MAX_PATH]; ExpandEnvironmentStringsW(L"%APPDATA%\\Microsoft\\Windows\\Themes\\TranscodedWallpaper", ad, MAX_PATH);
    return FileExists(ad) ? ad : L"";
}

// background image scaled to cover the canvas (dossier 02 · E.1 cover-fit), centered
static void CoverFit(Image& canvas, const Image& bg) {
    if (bg.empty()) return;
    double k = std::max((double)canvas.w / bg.w, (double)canvas.h / bg.h);
    int w = std::max(1, (int)std::ceil(bg.w * k)), h = std::max(1, (int)std::ceil(bg.h * k));
    Image s = ScaleImage(bg, w, h, true);
    canvas.Blit(s, (canvas.w - w) / 2, (canvas.h - h) / 2, false);
}

// distance-based alpha of a rounded rectangle (antialiased edge)
static int RoundAlpha(int x, int y, int w, int h, int r) {
    if (r <= 0) return 255;
    float cx = x + 0.5f, cy = y + 0.5f, dx = 0, dy = 0;
    if (cx < r) dx = r - cx; else if (cx > w - r) dx = cx - (w - r);
    if (cy < r) dy = r - cy; else if (cy > h - r) dy = cy - (h - r);
    if (dx <= 0 || dy <= 0) return 255;
    float d = std::sqrt(dx * dx + dy * dy);
    return (int)std::clamp((r - d + 0.5f) * 255.0f, 0.0f, 255.0f);
}

// auto balance: the plain border around the content is made even — the smallest of the four margins on every side, in
// the border's own color — so the content sits centered without touching the edges
static Image BalanceMargins(const Image& img) {
    const uint32_t bg = img.at(0, 0);
    auto differs = [&](uint32_t p) {
        return std::abs(CR(p) - CR(bg)) > 6 || std::abs(CG(p) - CG(bg)) > 6 || std::abs(CB(p) - CB(bg)) > 6 || std::abs(CA(p) - CA(bg)) > 6;
    };
    int x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++)
            if (differs(img.at(x, y))) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 < 0) return img;                                   // all one color
    int m = std::min({ x0, y0, img.w - 1 - x1, img.h - 1 - y1 });
    Image out(x1 - x0 + 1 + 2 * m, y1 - y0 + 1 + 2 * m, bg);
    out.Blit(img.Crop({ x0, y0, x1 + 1, y1 + 1 }), m, m, false);
    return out;
}

Image Beautify(const Image& srcIn, const BeautifyOptions& o, POINT* origin) {
    if (srcIn.empty()) return srcIn;
    Image src = o.autoBalance ? BalanceMargins(srcIn) : srcIn;
    if (src.empty()) src = srcIn;
    // rounded corners of the capture
    int r = std::min({ o.radius, src.w / 2, src.h / 2 });
    if (r > 0)
        for (int y = 0; y < src.h; y++)
            for (int x = 0; x < src.w; x++) {
                int a = RoundAlpha(x, y, src.w, src.h, r);
                if (a < 255) { uint32_t& p = src.at(x, y); p = (p & 0x00FFFFFF) | ((uint32_t)(CA(p) * a / 255) << 24); }
            }
    // canvas: capture + padding, grown to the aspect ratio
    int pad = std::max(0, o.padding);
    int W = src.w + 2 * pad, H = src.h + 2 * pad;
    static const double ratios[5] = { 0, 16.0 / 9, 4.0 / 3, 1.0, 9.0 / 16 };
    if (o.aspect > 0 && o.aspect < 5) {
        double want = ratios[o.aspect];
        if ((double)W / H < want) W = (int)std::ceil(H * want); else H = (int)std::ceil(W / want);
    }
    Image canvas(W, H, 0);
    switch (o.background) {
    case 0: canvas = Image(W, H, FromColorref(o.color1)); break;
    case 1: {                                         // linear gradient along the angle
        double th = o.angle * 3.14159265358979 / 180, cs = std::cos(th), sn = std::sin(th);
        double half = (std::fabs(cs) * W + std::fabs(sn) * H) / 2;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                double t = std::clamp(((x - W / 2.0) * cs + (y - H / 2.0) * sn) / (2 * half) + 0.5, 0.0, 1.0);
                auto mix = [&](int a, int b) { return (int)std::lround(a + (b - a) * t); };
                canvas.at(x, y) = ARGB(255, mix(GetRValue(o.color1), GetRValue(o.color2)), mix(GetGValue(o.color1), GetGValue(o.color2)),
                                       mix(GetBValue(o.color1), GetBValue(o.color2)));
            }
        break;
    }
    case 2: case 3: {
        canvas = Image(W, H, FromColorref(o.color1));
        Image bg;
        std::wstring path = o.background == 2 ? WallpaperPath() : o.imagePath;
        if (!path.empty() && LoadImageFile(path, bg)) CoverFit(canvas, bg);
        break;
    }
    default: break;                                   // transparent
    }
    // where the capture goes (alignment inside the free space)
    int freeW = W - src.w - 2 * pad, freeH = H - src.h - 2 * pad;
    int col = o.align % 3, row = o.align / 3;
    int x0 = pad + freeW * col / 2, y0 = pad + freeH * row / 2;
    // shadow: the capture's alpha, blurred, a little below
    if (o.shadow > 0 && o.shadowSize > 0) {
        int blur = o.shadowSize, off = std::max(1, blur / 3);
        Image sh(W, H, 0);
        for (int y = 0; y < src.h; y++)
            for (int x = 0; x < src.w; x++) {
                int tx = x + x0, ty = y + y0 + off;
                if (tx >= 0 && ty >= 0 && tx < W && ty < H) sh.at(tx, ty) = ARGB(CA(src.at(x, y)) * o.shadow / 100, 0, 0, 0);
            }
        BoxBlur(sh, { 0, 0, W, H }, std::max(1, blur / 2));
        canvas.Blit(sh, 0, 0, true);
    }
    canvas.Blit(src, x0, y0, true);
    canvas.dpi = srcIn.dpi;
    canvas.meta = srcIn.meta;
    if (origin) *origin = { x0, y0 };
    return canvas;
}

// ---------------------------------------------------------------- presets
static std::wstring PresetsFile() { return AppDataDir() + L"\\BeautifyPresets.xml"; }

std::vector<BeautifyPreset> BeautifyPresets() {
    std::vector<BeautifyPreset> v;
    auto add = [&](const wchar_t* name, int bg, COLORREF a, COLORREF b, int angle, int pad, int rad) {
        BeautifyPreset p; p.name = name; p.builtin = true;
        p.opt.background = bg; p.opt.color1 = a; p.opt.color2 = b; p.opt.angle = angle; p.opt.padding = pad; p.opt.radius = rad;
        v.push_back(p);
    };
    add(TR_NOOP(L"Oceano"), 1, RGB(102, 126, 234), RGB(118, 75, 162), 135, 64, 12);
    add(TR_NOOP(L"Pôr do sol"), 1, RGB(255, 126, 95), RGB(254, 180, 123), 135, 64, 12);
    add(TR_NOOP(L"Menta"), 1, RGB(67, 206, 162), RGB(24, 90, 157), 135, 64, 12);
    add(TR_NOOP(L"Grafite"), 1, RGB(68, 68, 80), RGB(20, 20, 26), 160, 64, 12);
    add(TR_NOOP(L"Papel de parede"), 2, RGB(40, 40, 46), RGB(40, 40, 46), 0, 72, 12);
    add(TR_NOOP(L"Branco simples"), 0, RGB(255, 255, 255), RGB(255, 255, 255), 0, 48, 8);
    XmlNode root;
    if (XmlLoadFile(PresetsFile(), root))
        for (auto& c : root.children) {
            if (c.name != "Preset") continue;
            BeautifyPreset p; p.name = c.getW("Name"); p.opt = BeautifyOptions::FromXml(c);
            if (!p.name.empty()) v.push_back(p);
        }
    return v;
}

void SaveBeautifyPresets(const std::vector<BeautifyPreset>& all) {
    XmlNode root("BeautifyPresets");
    for (auto& p : all) {
        if (p.builtin) continue;
        XmlNode n = p.opt.ToXml("Preset");
        n.setW("Name", p.name);
        root.children.push_back(n);
    }
    XmlSaveFile(PresetsFile(), root);
}

bool BeautifyDialog(HWND owner, BeautifyOptions& o, const Image* sample) {
    using F = ui::Field;
    auto presets = BeautifyPresets();
    std::vector<std::wstring> names = { Tr(L"(ajustes abaixo)") };
    for (auto& p : presets) names.push_back(p.builtin ? Tr(p.name) : p.name);
    std::vector<F> f = { F::MakeCombo(Tr(L"Predefinição"), names, 0),
          F::MakeCombo(Tr(L"Fundo"), { Tr(L"Cor sólida"), Tr(L"Degradê"), Tr(L"Papel de parede"), Tr(L"Imagem"), Tr(L"Transparente") }, o.background),
          F::MakeColor(Tr(L"Cor 1"), o.color1), F::MakeColor(Tr(L"Cor 2 (degradê)"), o.color2),
          F::MakeInt(Tr(L"Ângulo do degradê"), o.angle, 0, 359), F::MakeText(Tr(L"Imagem de fundo"), o.imagePath),
          F::MakeInt(Tr(L"Margem (px)"), o.padding, 0, 1000), F::MakeInt(Tr(L"Cantos arredondados (px)"), o.radius, 0, 200),
          F::MakeInt(Tr(L"Sombra (%)"), o.shadow, 0, 100), F::MakeInt(Tr(L"Tamanho da sombra (px)"), o.shadowSize, 0, 200),
          F::MakeCombo(Tr(L"Proporção"), { AspectName(0), L"16:9", L"4:3", L"1:1", L"9:16" }, o.aspect),
          F::MakeCombo(Tr(L"Alinhamento"), { Tr(L"Superior esquerdo"), Tr(L"Superior"), Tr(L"Superior direito"), Tr(L"Esquerda"), Tr(L"Centro"),
                                            Tr(L"Direita"), Tr(L"Inferior esquerdo"), Tr(L"Inferior"), Tr(L"Inferior direito") }, o.align),
          F::MakeCheck(Tr(L"Equilibrar as margens (corta bordas lisas da captura)"), o.autoBalance),
          F::MakeText(Tr(L"Salvar como predefinição (nome)"), L"") };
    auto read = [&](std::vector<F>& v) {
        if (v[0].value > 0 && v[0].value <= (int)presets.size()) return presets[v[0].value - 1].opt;
        BeautifyOptions b = o;
        b.background = v[1].value; b.color1 = v[2].color; b.color2 = v[3].color; b.angle = v[4].value; b.imagePath = Trim(v[5].text);
        b.padding = v[6].value; b.radius = v[7].value; b.shadow = v[8].value; b.shadowSize = v[9].value; b.aspect = v[10].value;
        b.align = v[11].value; b.autoBalance = v[12].value != 0;
        return b;
    };
    bool ok;
    if (sample && !sample->empty()) {
        // the preview runs on a reduced copy; sizes in px scale along so it looks like the result
        Image mini = MakeThumbnail(*sample, 640, 480);
        double k = (double)mini.w / sample->w;
        ok = ui::RunFormPreview(owner, Tr(L"Embelezar"), f, [&](std::vector<F>& v) {
            BeautifyOptions b = read(v);
            b.padding = (int)std::lround(b.padding * k); b.radius = (int)std::lround(b.radius * k); b.shadowSize = (int)std::lround(b.shadowSize * k);
            return Beautify(mini, b);
        }, { 440, 330 });
    } else ok = ui::RunForm(owner, Tr(L"Embelezar"), f);
    if (!ok) return false;
    o = read(f);
    std::wstring name = Trim(f[13].text);
    if (!name.empty()) {
        auto all = BeautifyPresets();
        auto it = std::find_if(all.begin(), all.end(), [&](const BeautifyPreset& p) { return !p.builtin && p.name == name; });
        if (it != all.end()) it->opt = o; else all.push_back({ name, o, false });
        SaveBeautifyPresets(all);
    }
    return true;
}

} // namespace snap::fx
