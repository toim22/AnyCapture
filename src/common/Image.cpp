#include "Image.h"
#include "snapcore.h"
#include "webp/decode.h"
#include "webp/encode.h"

namespace snap {

uint32_t BlendOver(uint32_t d, uint32_t s) {
    int sa = CA(s);
    if (sa == 255) return s;
    if (sa == 0) return d;
    int da = CA(d);
    int oa = sa + da * (255 - sa) / 255;
    if (oa == 0) return 0;
    auto ch = [&](int sc, int dc) { return (sc * sa + dc * da * (255 - sa) / 255) / oa; };
    return ARGB(oa, ch(CR(s), CR(d)), ch(CG(s), CG(d)), ch(CB(s), CB(d)));
}

Image Image::Crop(RECT r) const {
    r.left = std::max<LONG>(0, r.left); r.top = std::max<LONG>(0, r.top);
    r.right = std::min<LONG>(w, r.right); r.bottom = std::min<LONG>(h, r.bottom);
    if (r.right <= r.left || r.bottom <= r.top) return {};
    Image out(r.right - r.left, r.bottom - r.top);
    for (int y = 0; y < out.h; y++)
        memcpy(out.row(y), row(r.top + y) + r.left, out.w * 4);
    return out;
}

void Image::Blit(const Image& src, int dx, int dy, bool blend) {
    for (int y = 0; y < src.h; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= h) continue;
        for (int x = 0; x < src.w; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= w) continue;
            uint32_t s = src.at(x, y);
            at(tx, ty) = blend ? BlendOver(at(tx, ty), s) : s;
        }
    }
}

void Image::FillRect(RECT r, uint32_t color) {
    r.left = std::max<LONG>(0, r.left); r.top = std::max<LONG>(0, r.top);
    r.right = std::min<LONG>(w, r.right); r.bottom = std::min<LONG>(h, r.bottom);
    for (int y = r.top; y < r.bottom; y++)
        std::fill(row(y) + r.left, row(y) + r.right, color);
}

bool Image::HasTransparency() const {
    for (uint32_t p : px) if (CA(p) != 255) return true;
    return false;
}

void Image::MakeOpaque() { for (auto& p : px) p |= 0xFF000000u; }

HBITMAP Image::ToHBITMAP() const {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hbm && bits) memcpy(bits, px.data(), px.size() * 4);
    return hbm;
}

Image Image::FromHBITMAP(HBITMAP hbm) {
    BITMAP bm{};
    if (!GetObjectW(hbm, sizeof(bm), &bm)) return {};
    Image img(bm.bmWidth, std::abs(bm.bmHeight));
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = img.w; bi.bmiHeader.biHeight = -img.h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    GetDIBits(dc, hbm, 0, img.h, img.px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (bm.bmBitsPixel < 32) img.MakeOpaque();
    else {
        bool allZero = true;
        for (uint32_t p : img.px) if (CA(p)) { allZero = false; break; }
        if (allZero) img.MakeOpaque();
    }
    return img;
}

Image Image::FromDC(HDC src, RECT r) {
    int cw = r.right - r.left, ch = r.bottom - r.top;
    if (cw <= 0 || ch <= 0) return {};
    Image img(cw, ch);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cw; bi.bmiHeader.biHeight = -ch;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(src, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC mem = CreateCompatibleDC(src);
    HGDIOBJ old = SelectObject(mem, hbm);
    BitBlt(mem, 0, 0, cw, ch, src, r.left, r.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    memcpy(img.px.data(), bits, img.px.size() * 4);
    SelectObject(mem, old);
    DeleteDC(mem); DeleteObject(hbm);
    img.MakeOpaque();
    return img;
}

std::unique_ptr<Gdiplus::Bitmap> Image::WrapGdip() {
    return std::make_unique<Gdiplus::Bitmap>(w, h, w * 4, PixelFormat32bppARGB, (BYTE*)px.data());
}

Image Image::FromGdip(Gdiplus::Bitmap& bmp) {
    Image img(bmp.GetWidth(), bmp.GetHeight());
    Gdiplus::BitmapData bd{};
    bd.Width = img.w; bd.Height = img.h; bd.Stride = img.w * 4;
    bd.PixelFormat = PixelFormat32bppARGB; bd.Scan0 = img.px.data();
    Gdiplus::Rect rc(0, 0, img.w, img.h);
    bmp.LockBits(&rc, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeUserInputBuf, PixelFormat32bppARGB, &bd);
    bmp.UnlockBits(&bd);
    return img;
}

void Image::DrawTo(HDC hdc, int x, int y, int dw, int dh, bool smooth) const {
    if (empty()) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    int oldMode = SetStretchBltMode(hdc, smooth ? HALFTONE : COLORONCOLOR);
    if (smooth) SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchDIBits(hdc, x, y, dw, dh, 0, 0, w, h, px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(hdc, oldMode);
}

// ---------------------------------------------------------------- WIC
ImageFormat FormatFromExt(const std::wstring& extIn) {
    std::wstring e = ToLower(extIn);
    if (e == L".png") return ImageFormat::PNG;
    if (e == L".jpg" || e == L".jpeg" || e == L".jpe") return ImageFormat::JPEG;
    if (e == L".bmp") return ImageFormat::BMP;
    if (e == L".gif") return ImageFormat::GIF;
    if (e == L".tif" || e == L".tiff") return ImageFormat::TIFF;
    if (e == L".webp") return ImageFormat::WEBP;
    if (e == L".avif") return ImageFormat::AVIF;
    if (e == L".heic" || e == L".heif" || e == L".hif") return ImageFormat::HEIC;
    return ImageFormat::Unknown;
}

const wchar_t* FormatExt(ImageFormat fmt) {
    switch (fmt) {
    case ImageFormat::JPEG: return L".jpg";
    case ImageFormat::BMP: return L".bmp";
    case ImageFormat::GIF: return L".gif";
    case ImageFormat::TIFF: return L".tif";
    case ImageFormat::WEBP: return L".webp";
    case ImageFormat::AVIF: return L".avif";
    case ImageFormat::HEIC: return L".heic";
    default: return L".png";
    }
}

SaveOptions SaveOptions::FromSettings() {
    SaveOptions o;
    o.webpLossless = RegGetDword(L"WebPLossless", 1) != 0;
    o.webpQuality = (int)std::clamp<DWORD>(RegGetDword(L"WebPQuality", 90), 1, 100);
    o.avifQuality = (int)std::clamp<DWORD>(RegGetDword(L"AvifQuality", 80), 1, 100);
    o.stripMetadata = RegGetDword(L"StripMetadata", 0) != 0;
    return o;
}

static thread_local std::wstring g_ioError;
const std::wstring& ImageIoError() { return g_ioError; }

std::wstring CodecHint(ImageFormat fmt) {
    if (fmt == ImageFormat::HEIC)
        return Tr(L"Para abrir fotos HEIC, instale da Microsoft Store as extensões \"Extensões de Imagem HEIF\" e \"Extensões de Vídeo HEVC\".");
    if (fmt == ImageFormat::AVIF)
        return Tr(L"Para usar AVIF, instale da Microsoft Store a \"Extensão de Vídeo AV1\" e as \"Extensões de Imagem HEIF\". "
                  L"Salvar em AVIF também precisa de um codificador AV1 (placas de vídeo recentes).");
    return L"";
}

static bool IsWebP(const BYTE* d, size_t n) { return n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4); }

static bool DecodeWebP(const BYTE* d, size_t n, Image& out) {
    int w = 0, h = 0;
    if (!WebPGetInfo(d, n, &w, &h) || w <= 0 || h <= 0) { g_ioError = Tr(L"Arquivo WebP inválido."); return false; }
    out = Image(w, h);
    if (!WebPDecodeBGRAInto(d, n, (uint8_t*)out.px.data(), out.px.size() * 4, w * 4)) {
        out = Image(); g_ioError = Tr(L"Arquivo WebP inválido."); return false;
    }
    return true;
}

static bool EncodeWebP(const Image& img, const SaveOptions& opt, std::vector<BYTE>& out) {
    uint8_t* data = nullptr;
    size_t n = opt.webpLossless ? WebPEncodeLosslessBGRA((const uint8_t*)img.px.data(), img.w, img.h, img.w * 4, &data)
                                : WebPEncodeBGRA((const uint8_t*)img.px.data(), img.w, img.h, img.w * 4, (float)opt.webpQuality, &data);
    if (!n || !data) { g_ioError = Tr(L"Falha ao codificar WebP."); return false; }
    out.assign(data, data + n);
    WebPFree(data);
    return true;
}

// ---------------------------------------------------------------- codecs (rust/snapcore/src/codec.rs, through WIC)
// PNG, JPEG, BMP, GIF, TIFF, AVIF and HEIC are read and written by snapcore with the Windows codecs, with the photo's
// metadata (C29) and its EXIF orientation; WebP is read and written here with libwebp.

// the copy snapcore decoded, moved into an Image
static bool TakeDecoded(SnapcoreImage* d, Image& out) {
    if (!d) return false;
    SnapcoreImageInfo info{};
    snapcore_image_info(d, &info);
    out = Image((int)info.w, (int)info.h);
    if (!out.px.empty()) memcpy(out.px.data(), snapcore_image_pixels(d), out.px.size() * 4);
    if (info.hasMeta) {
        auto m = std::make_shared<ImageMeta>();
        m->dateTaken = snapcore_image_meta_text(d, 0);
        m->make = snapcore_image_meta_text(d, 1);
        m->model = snapcore_image_meta_text(d, 2);
        m->software = snapcore_image_meta_text(d, 3);
        m->gps = info.gps; m->lat = info.lat; m->lon = info.lon;
        out.meta = m;
    }
    snapcore_image_free(d);
    return true;
}

static std::vector<BYTE> ReadFileBytes(const std::wstring& path) {
    std::vector<BYTE> d;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return d;
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < 512LL * 1024 * 1024) {
        d.resize((size_t)sz.QuadPart);
        DWORD rd = 0;
        if (!ReadFile(h, d.data(), (DWORD)d.size(), &rd, nullptr) || rd != d.size()) d.clear();
    }
    CloseHandle(h);
    return d;
}

bool LoadImageFile(const std::wstring& path, Image& out) {
    g_ioError.clear();
    {
        BYTE head[12] = {};
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD rd = 0; ReadFile(h, head, sizeof head, &rd, nullptr); CloseHandle(h);
            if (rd == sizeof head && IsWebP(head, sizeof head)) {        // WebP: libwebp (the Windows codec is optional)
                auto d = ReadFileBytes(path);
                return !d.empty() && DecodeWebP(d.data(), d.size(), out);
            }
        }
    }
    int32_t err = 0;
    bool ok = TakeDecoded(snapcore_image_load(path.data(), path.size(), &err), out);
    if (!ok) {
        ImageFormat fmt = FormatFromExt(FileExt(path));
        g_ioError = CodecHint(fmt);
        if (g_ioError.empty() && err == 2) g_ioError = Tr(L"Formato de imagem não suportado.");
    }
    return ok;
}

bool DecodeFromMemory(const BYTE* data, size_t len, Image& out) {
    g_ioError.clear();
    if (len >= 12 && IsWebP(data, len)) return DecodeWebP(data, len, out);
    int32_t err = 0;
    return TakeDecoded(snapcore_image_decode(data, len, &err), out);
}

bool DecodeImage(IStream* stream, Image& out) {
    g_ioError.clear();
    STATSTG ss{};
    LARGE_INTEGER z{};
    if (FAILED(stream->Stat(&ss, STATFLAG_NONAME)) || FAILED(stream->Seek(z, STREAM_SEEK_SET, nullptr))) return false;
    std::vector<BYTE> d((size_t)ss.cbSize.QuadPart);
    ULONG got = 0;
    if (d.empty() || FAILED(stream->Read(d.data(), (ULONG)d.size(), &got)) || got != d.size()) return false;
    return DecodeFromMemory(d.data(), d.size(), out);
}

bool EncodeToMemoryEx(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, const SaveOptions& opt) {
    g_ioError.clear();
    if (img.empty()) return false;
    if (fmt == ImageFormat::WEBP) return EncodeWebP(img, opt, out);
    const ImageMeta* m = img.meta.get();
    SnapcoreEncodeRequest r{};
    r.w = (uint32_t)img.w; r.h = (uint32_t)img.h; r.px = img.px.data(); r.dpi = img.dpi;
    r.format = (int32_t)fmt; r.jpegQuality = opt.jpegQuality; r.avifQuality = opt.avifQuality;
    r.stripMetadata = opt.stripMetadata;
    if (m) {
        r.hasMeta = true;
        r.dateTaken = m->dateTaken.c_str(); r.make = m->make.c_str(); r.model = m->model.c_str(); r.software = m->software.c_str();
        r.gps = m->gps; r.lat = m->lat; r.lon = m->lon;
    }
    r.version = kAppVersion;
    SnapcoreBuf buf{};
    int32_t err = 0;
    if (!snapcore_image_encode(&r, &buf, &err)) {
        if (err == 2) g_ioError = CodecHint(fmt);
        return false;
    }
    out.assign(buf.data, buf.data + buf.len);
    snapcore_buf_free(buf);
    return true;
}

bool EncodeImage(IStream* stream, const Image& imgIn, ImageFormat fmt, int quality) {
    SaveOptions o = SaveOptions::FromSettings();
    o.jpegQuality = quality;
    return EncodeImageEx(stream, imgIn, fmt, o);
}

bool EncodeImageEx(IStream* stream, const Image& imgIn, ImageFormat fmt, const SaveOptions& opt) {
    std::vector<BYTE> d;
    if (!EncodeToMemoryEx(imgIn, fmt, d, opt)) return false;
    ULONG wr = 0;
    return SUCCEEDED(stream->Write(d.data(), (ULONG)d.size(), &wr)) && wr == d.size();
}

bool SaveImageFile(const std::wstring& path, const Image& img, int quality) {
    SaveOptions o = SaveOptions::FromSettings();
    o.jpegQuality = quality;
    return SaveImageFileEx(path, img, o);
}

bool SaveImageFileEx(const std::wstring& path, const Image& img, const SaveOptions& opt) {
    ImageFormat fmt = FormatFromExt(FileExt(path));
    if (fmt == ImageFormat::Unknown || fmt == ImageFormat::HEIC) fmt = ImageFormat::PNG;     // HEIC is only opened
    std::vector<BYTE> d;
    if (!EncodeToMemoryEx(img, fmt, d, opt)) return false;
    std::wstring tmp = path + L".tmp";       // written whole, then put in place
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(h, d.data(), (DWORD)d.size(), &wr, nullptr) && wr == d.size();
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool CanEncode(ImageFormat fmt) {
    if (fmt != ImageFormat::AVIF) return fmt != ImageFormat::HEIC && fmt != ImageFormat::Unknown;
    static int cached = -1;                  // a tiny test image: the AV1 encoder exists or not
    if (cached < 0) {
        std::vector<BYTE> d;
        SaveOptions o; o.stripMetadata = true;
        cached = EncodeToMemoryEx(Image(16, 16, 0xFF808080), ImageFormat::AVIF, d, o) && !d.empty();
    }
    return cached == 1;
}

bool EncodeToMemory(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, int quality) {
    SaveOptions o = SaveOptions::FromSettings();
    o.jpegQuality = quality;
    o.stripMetadata = true;                  // internal copies (.snap streams, PDF, clipboard) carry no metadata
    return EncodeToMemoryEx(img, fmt, out, o);
}

std::wstring ImageOpenFilter() {
    std::wstring f = Tr(L"Imagens e documentos|*.snap;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico;*.webp;*.avif;*.heic;*.heif;*.mp4|"
                        L"Documento Snapnab (*.snap)|*.snap|Vídeo MP4 (*.mp4)|*.mp4|Todos os arquivos (*.*)|*.*|");
    std::replace(f.begin(), f.end(), L'|', L'\0');
    return f;
}

// the order matters: the Editor's Save as maps the chosen filter to an extension (kSaveExts)
std::wstring ImageSaveFilter() {
    std::wstring f = Tr(L"PNG (*.png)|*.png|JPEG (*.jpg)|*.jpg|BMP (*.bmp)|*.bmp|GIF (*.gif)|*.gif|TIFF (*.tif)|*.tif|"
                        L"WebP (*.webp)|*.webp|AVIF (*.avif)|*.avif|PDF (*.pdf)|*.pdf|Documento Snapnab (*.snap)|*.snap|");
    std::replace(f.begin(), f.end(), L'|', L'\0');
    return f;
}

// ---------------------------------------------------------------- clipboard
static UINT PngFormat() { static UINT f = RegisterClipboardFormatW(L"PNG"); return f; }

bool CopyImageToClipboard(HWND owner, const Image& img) {
    if (img.empty()) return false;
    std::vector<BYTE> png;
    EncodeToMemory(img, ImageFormat::PNG, png);
    // CF_DIB: 32 bpp bottom-up, composited over white (old apps ignore alpha)
    size_t sz = sizeof(BITMAPINFOHEADER) + img.px.size() * 4;
    HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!hDib) return false;
    auto* bih = (BITMAPINFOHEADER*)GlobalLock(hDib);
    *bih = {};
    bih->biSize = sizeof(BITMAPINFOHEADER); bih->biWidth = img.w; bih->biHeight = img.h;
    bih->biPlanes = 1; bih->biBitCount = 32; bih->biCompression = BI_RGB;
    bih->biSizeImage = (DWORD)(img.px.size() * 4);
    auto* dst = (uint32_t*)(bih + 1);
    for (int y = 0; y < img.h; y++) {
        const uint32_t* s = img.row(img.h - 1 - y);
        for (int x = 0; x < img.w; x++) dst[(size_t)y * img.w + x] = BlendOver(0xFFFFFFFF, s[x]);
    }
    GlobalUnlock(hDib);
    if (!OpenClipboard(owner)) { GlobalFree(hDib); return false; }
    EmptyClipboard();
    SetClipboardData(CF_DIB, hDib);
    if (!png.empty()) {
        HGLOBAL hPng = GlobalAlloc(GMEM_MOVEABLE, png.size());
        if (hPng) {
            memcpy(GlobalLock(hPng), png.data(), png.size());
            GlobalUnlock(hPng);
            SetClipboardData(PngFormat(), hPng);
        }
    }
    CloseClipboard();
    return true;
}

bool ClipboardHasImage() {
    return IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) ||
           IsClipboardFormatAvailable(PngFormat()) || IsClipboardFormatAvailable(CF_BITMAP);
}

bool GetClipboardImage(Image& out) {
    bool ok = false;
    for (int tries = 0; tries < 10 && !OpenClipboard(nullptr); tries++) Sleep(50);
    if (HANDLE h = GetClipboardData(PngFormat())) {
        void* p = GlobalLock(h);
        if (p) { ok = DecodeFromMemory((BYTE*)p, GlobalSize(h), out); GlobalUnlock(h); }
    }
    if (!ok) {
        HANDLE h = GetClipboardData(CF_DIBV5);
        if (!h) h = GetClipboardData(CF_DIB);
        if (h) {
            auto* bih = (BITMAPINFOHEADER*)GlobalLock(h);
            if (bih && (bih->biBitCount == 32 || bih->biBitCount == 24)) {
                int w = bih->biWidth, hh = std::abs(bih->biHeight);
                bool bottomUp = bih->biHeight > 0;
                BYTE* bits = (BYTE*)bih + bih->biSize;
                if (bih->biCompression == BI_BITFIELDS && bih->biSize == sizeof(BITMAPINFOHEADER)) bits += 12;
                int stride = ((w * bih->biBitCount + 31) / 32) * 4;
                out = Image(w, hh);
                bool anyAlpha = false;
                for (int y = 0; y < hh; y++) {
                    BYTE* r = bits + (size_t)(bottomUp ? hh - 1 - y : y) * stride;
                    for (int x = 0; x < w; x++) {
                        if (bih->biBitCount == 32) {
                            uint32_t v = ((uint32_t*)r)[x];
                            if (CA(v)) anyAlpha = true;
                            out.at(x, y) = v;
                        } else out.at(x, y) = ARGB(255, r[x * 3 + 2], r[x * 3 + 1], r[x * 3]);
                    }
                }
                if (!anyAlpha) out.MakeOpaque();
                ok = true;
            }
            if (bih) GlobalUnlock(h);
        }
    }
    if (!ok) {
        if (HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP)) { out = Image::FromHBITMAP(hbm); ok = !out.empty(); }
    }
    CloseClipboard();
    return ok;
}

// ---------------------------------------------------------------- scaling
Image ScaleImage(const Image& src, int nw, int nh, bool hq) {
    if (src.empty() || nw <= 0 || nh <= 0) return {};
    Image dst(nw, nh, 0);
    Image& s = const_cast<Image&>(src);
    auto sb = s.WrapGdip();
    auto db = dst.WrapGdip();
    {
        Gdiplus::Graphics g(db.get());
        g.SetInterpolationMode(hq ? Gdiplus::InterpolationModeHighQualityBicubic : Gdiplus::InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        Gdiplus::ImageAttributes ia;
        ia.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        g.DrawImage(sb.get(), Gdiplus::Rect(0, 0, nw, nh), 0, 0, src.w, src.h, Gdiplus::UnitPixel, &ia);
    }
    return dst;
}

Image MakeThumbnail(const Image& img, int maxW, int maxH) {
    if (img.empty()) return {};
    double s = std::min(1.0, std::min((double)maxW / img.w, (double)maxH / img.h));
    return ScaleImage(img, std::max(1, (int)(img.w * s)), std::max(1, (int)(img.h * s)));
}

} // namespace snap
