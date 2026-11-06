#include "Beautify.h"
#include "Effects.h"
#include "Ui.h"
#include "SnapcoreBridge.h"
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

SnapcoreBeautify ToSnapcore(const BeautifyOptions& o) {
    SnapcoreBeautify b{};
    b.background = o.background; b.color1 = o.color1; b.color2 = o.color2; b.angle = o.angle;
    b.imagePath = o.imagePath.data(); b.imagePathLen = o.imagePath.size();
    b.padding = o.padding; b.radius = o.radius; b.shadow = o.shadow; b.shadowSize = o.shadowSize;
    b.align = o.align; b.aspect = o.aspect; b.autoBalance = o.autoBalance;
    return b;
}

std::wstring WallpaperPath() {
    SnapcoreWStr s{};
    if (!snapcore_wallpaper_path(&s)) return L"";
    std::wstring r = s.len ? std::wstring(s.data, s.len) : std::wstring();
    snapcore_wstr_free(s);
    return r;
}

// the render is snapcore's (rust/snapcore/src/beautify.rs)
Image Beautify(const Image& src, const BeautifyOptions& o, POINT* origin) {
    SnapcoreImageIn in = ImageIn(src);
    SnapcoreBeautify b = ToSnapcore(o);
    int32_t at[2] = { 0, 0 };
    Image r = TakeImage(snapcore_beautify(&in, &b, at));
    if (origin) *origin = { at[0], at[1] };
    return r;
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
