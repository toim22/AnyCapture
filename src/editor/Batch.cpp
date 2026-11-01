// Batch processing (C28; ShareX dossier 01 · F14 "Image effects" applied to many files / FastStone batch convert):
// resize, an effect (quick effects and Beautify presets), watermark, format conversion with quality, metadata removal
// and renaming, for many images at once (library selection or files). Preview of the first image, progress with
// cancel (IProgressDialog); the originals are never changed — the results go to the chosen folder.
#include "EditorTools.h"
#include "Effects.h"
#include "Ui.h"
#include <shlobj.h>
#include <shobjidl.h>

namespace snap::ed {

namespace {

struct BatchSettings {
    int resize = 0, percent = 50, maxW = 1280, maxH = 0;
    int effect = 0;
    std::wstring watermark;
    int wmOpacity = 30;
    int format = 0, quality = 90;
    bool strip = true;
    std::wstring pattern = L"{name}", folder;
};

const ImageFormat kFormats[] = { ImageFormat::Unknown, ImageFormat::PNG, ImageFormat::JPEG, ImageFormat::WEBP, ImageFormat::AVIF,
                                 ImageFormat::BMP, ImageFormat::GIF, ImageFormat::TIFF };

std::wstring DefaultFolder() {
    PWSTR pics = nullptr;
    std::wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pics))) d = std::wstring(pics) + L"\\Snapnab\\" + Tr(L"Lote");
    CoTaskMemFree(pics);
    return d;
}

Image Process(const Image& src, const BatchSettings& b, const std::vector<BatchEffect>& effects) {
    Image img = src;
    if (b.resize == 1 && b.percent != 100) img = fx::Scale(img, b.percent);
    else if (b.resize == 2 && (b.maxW > 0 || b.maxH > 0)) {
        double k = 1;
        if (b.maxW > 0 && img.w > b.maxW) k = std::min(k, (double)b.maxW / img.w);
        if (b.maxH > 0 && img.h > b.maxH) k = std::min(k, (double)b.maxH / img.h);
        if (k < 1) img = ScaleImage(img, std::max(1, (int)std::lround(img.w * k)), std::max(1, (int)std::lround(img.h * k)));
    }
    if (b.effect > 0 && b.effect <= (int)effects.size() && effects[b.effect - 1].run) img = effects[b.effect - 1].run(img);
    if (!Trim(b.watermark).empty()) fx::Watermark(img, Trim(b.watermark), b.wmOpacity, true, false);
    img.meta = src.meta;
    return img;
}

std::wstring OutputName(const BatchSettings& b, const std::wstring& src, int n, ImageFormat fmt) {
    std::wstring name = FileNameOf(src);
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);
    std::wstring out = Trim(b.pattern).empty() ? L"{name}" : Trim(b.pattern);
    auto rep = [&](const std::wstring& tok, const std::wstring& v) {
        for (size_t p; (p = out.find(tok)) != std::wstring::npos;) out.replace(p, tok.size(), v);
    };
    rep(L"{name}", name);
    rep(L"{n}", Format(L"%03d", n));
    rep(L"{date}", TimeStamp(L"%04d-%02d-%02d"));
    for (wchar_t& c : out) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    return out + FormatExt(fmt);
}

} // namespace

bool PickImageFiles(HWND owner, std::vector<std::wstring>& files) {
    files.clear();
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
    DWORD opt = 0; d->GetOptions(&opt);
    d->SetOptions(opt | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    std::wstring spec = L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.avif;*.heic;*.heif;*.snap";
    std::wstring label = Tr(L"Imagens");
    COMDLG_FILTERSPEC fs[] = { { label.c_str(), spec.c_str() } };
    d->SetFileTypes(1, fs);
    d->SetTitle(Tr(L"Escolha as imagens"));
    if (SUCCEEDED(d->Show(owner))) {
        IShellItemArray* arr = nullptr;
        if (SUCCEEDED(d->GetResults(&arr))) {
            DWORD n = 0; arr->GetCount(&n);
            for (DWORD i = 0; i < n; i++) {
                IShellItem* it = nullptr;
                if (FAILED(arr->GetItemAt(i, &it))) continue;
                PWSTR p = nullptr;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { files.push_back(p); CoTaskMemFree(p); }
                it->Release();
            }
            arr->Release();
        }
    }
    d->Release();
    return !files.empty();
}

void BatchDialog(HWND owner, std::vector<std::wstring> files, std::function<bool(const std::wstring&, Image&)> load,
                 const std::vector<BatchEffect>& effects) {
    if (files.empty() && !PickImageFiles(owner, files)) return;
    BatchSettings b;
    b.resize = std::clamp((int)RegGetDword(L"BatchResize", 0, L"Editor"), 0, 2);
    b.percent = std::clamp((int)RegGetDword(L"BatchPercent", 50, L"Editor"), 1, 1000);
    b.maxW = (int)RegGetDword(L"BatchMaxWidth", 1280, L"Editor");
    b.maxH = (int)RegGetDword(L"BatchMaxHeight", 0, L"Editor");
    b.format = std::clamp((int)RegGetDword(L"BatchFormat", 0, L"Editor"), 0, 7);
    b.quality = std::clamp((int)RegGetDword(L"BatchQuality", 90, L"Editor"), 1, 100);
    b.strip = RegGetDword(L"BatchStripMetadata", 1, L"Editor") != 0;
    b.pattern = RegGetString(L"BatchPattern", L"{name}", L"Editor");
    b.folder = RegGetString(L"BatchFolder", DefaultFolder(), L"Editor");
    using F = ui::Field;
    std::vector<std::wstring> fxNames = { Tr(L"(nenhum)") };
    for (auto& e : effects) fxNames.push_back(e.name);
    std::vector<F> f = {
        F::MakeLabel(Format(Tr(L"%zu imagens — os originais não são alterados."), files.size())),
        F::MakeCombo(Tr(L"Redimensionar"), { Tr(L"Não"), Tr(L"Porcentagem"), Tr(L"Largura/altura máxima") }, b.resize),
        F::MakeInt(Tr(L"Porcentagem"), b.percent, 1, 1000),
        F::MakeInt(Tr(L"Largura máxima (0 = livre)"), b.maxW, 0, 100000),
        F::MakeInt(Tr(L"Altura máxima (0 = livre)"), b.maxH, 0, 100000),
        F::MakeCombo(Tr(L"Efeito"), fxNames, 0),
        F::MakeText(Tr(L"Marca d'água (texto)"), L""),
        F::MakeInt(Tr(L"Opacidade da marca d'água (%)"), b.wmOpacity, 5, 100),
        F::MakeCombo(Tr(L"Formato"), { Tr(L"O mesmo do original"), L"PNG", L"JPEG", L"WebP", L"AVIF", L"BMP", L"GIF", L"TIFF" }, b.format),
        F::MakeInt(Tr(L"Qualidade (JPEG, WebP, AVIF)"), b.quality, 1, 100),
        F::MakeCheck(Tr(L"Remover metadados (EXIF, GPS…)"), b.strip),
        F::MakeText(Tr(L"Nome dos arquivos"), b.pattern),
        F::MakeLabel(Tr(L"{name} nome original · {n} número · {date} data de hoje")),
        F::MakeText(Tr(L"Pasta de destino"), b.folder),
    };
    auto read = [&](std::vector<F>& v) {
        BatchSettings r;
        r.resize = v[1].value; r.percent = v[2].value; r.maxW = v[3].value; r.maxH = v[4].value; r.effect = v[5].value;
        r.watermark = v[6].text; r.wmOpacity = v[7].value; r.format = v[8].value; r.quality = v[9].value; r.strip = v[10].value != 0;
        r.pattern = v[11].text; r.folder = Trim(v[13].text);
        return r;
    };
    Image first;
    load(files[0], first);
    auto preview = [&](std::vector<F>& v) {
        if (first.empty()) return Image();
        // effects and the watermark are drawn in pixels: preview on a copy at most 1200 px wide (fast enough)
        Image src = first.w > 1200 ? ScaleImage(first, 1200, std::max(1, first.h * 1200 / first.w)) : first;
        BatchSettings r = read(v);
        double k = (double)src.w / first.w;
        if (r.resize == 2) { r.maxW = (int)(r.maxW * k); r.maxH = (int)(r.maxH * k); }
        return Process(src, r, effects);
    };
    auto validate = [&](std::vector<F>& v) -> std::wstring {
        std::wstring dir = Trim(v[13].text);
        if (dir.empty()) return Tr(L"Escolha a pasta de destino.");
        if (!EnsureDir(dir)) return Tr(L"Não foi possível criar a pasta de destino.");
        if (v[8].value == 4 && !CanEncode(ImageFormat::AVIF)) return CodecHint(ImageFormat::AVIF);
        return L"";
    };
    if (!ui::RunFormPreview(owner, Tr(L"Processamento em lote"), f, preview, { 380, 300 }, validate, 460)) return;
    b = read(f);
    RegSetDword(L"BatchResize", b.resize, L"Editor"); RegSetDword(L"BatchPercent", b.percent, L"Editor");
    RegSetDword(L"BatchMaxWidth", b.maxW, L"Editor"); RegSetDword(L"BatchMaxHeight", b.maxH, L"Editor");
    RegSetDword(L"BatchFormat", b.format, L"Editor"); RegSetDword(L"BatchQuality", b.quality, L"Editor");
    RegSetDword(L"BatchStripMetadata", b.strip, L"Editor"); RegSetString(L"BatchPattern", b.pattern, L"Editor");
    RegSetString(L"BatchFolder", b.folder, L"Editor");

    IProgressDialog* pd = nullptr;
    CoCreateInstance(CLSID_ProgressDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pd));
    if (pd) {
        pd->SetTitle(Tr(L"Processamento em lote"));
        pd->StartProgressDialog(owner, nullptr, PROGDLG_NORMAL | PROGDLG_AUTOTIME, nullptr);
    }
    int ok = 0, failed = 0;
    std::wstring firstError;
    SaveOptions so = SaveOptions::FromSettings();
    so.jpegQuality = b.quality; so.webpQuality = b.quality; so.avifQuality = b.quality;
    so.webpLossless = false;
    so.stripMetadata = b.strip;
    for (size_t i = 0; i < files.size(); i++) {
        if (pd) {
            if (pd->HasUserCancelled()) break;
            pd->SetLine(1, FileNameOf(files[i]).c_str(), TRUE, nullptr);
            pd->SetProgress((DWORD)i, (DWORD)files.size());
        }
        Image src;
        if (!load(files[i], src) || src.empty()) { failed++; if (firstError.empty()) firstError = FileNameOf(files[i]); continue; }
        ImageFormat fmt = kFormats[b.format];
        if (fmt == ImageFormat::Unknown) {
            fmt = FormatFromExt(FileExt(files[i]));
            if (fmt == ImageFormat::Unknown || fmt == ImageFormat::HEIC || (fmt == ImageFormat::AVIF && !CanEncode(fmt))) fmt = ImageFormat::PNG;
        }
        if (fmt == ImageFormat::WEBP && b.format == 0) so.webpLossless = true;
        Image out = Process(src, b, effects);
        std::wstring name = OutputName(b, files[i], (int)i + 1, fmt), path = b.folder + L"\\" + name;
        for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; n++) {
            size_t dot = name.rfind(L'.');
            path = b.folder + L"\\" + name.substr(0, dot) + Format(L"_%d", n) + name.substr(dot);
        }
        if (SaveImageFileEx(path, out, so)) ok++;
        else { failed++; if (firstError.empty()) firstError = FileNameOf(files[i]) + L": " + ImageIoError(); }
    }
    if (pd) { pd->StopProgressDialog(); pd->Release(); }
    std::wstring msg = Format(Tr(L"%d imagens processadas em:\n%s"), ok, b.folder.c_str());
    if (failed) msg += Format(Tr(L"\n\n%d falharam (primeira: %s)."), failed, firstError.c_str());
    msg += Tr(L"\n\nAbrir a pasta?");
    if (MessageBoxW(owner, msg.c_str(), Tr(L"Processamento em lote"), MB_YESNO | (failed ? MB_ICONWARNING : MB_ICONINFORMATION)) == IDYES)
        ShellExecuteW(nullptr, L"open", b.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace snap::ed
