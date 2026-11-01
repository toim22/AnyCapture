#include "Image.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <propvarutil.h>
#include "webp/decode.h"
#include "webp/encode.h"

using Microsoft::WRL::ComPtr;

namespace snap {

struct ComScope {
    HRESULT hr;
    ComScope() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

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

static ComPtr<IWICImagingFactory> Factory() {
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    return f;
}

// ---------------------------------------------------------------- metadata (C29)
static std::wstring PropString(const PROPVARIANT& v) {
    if (v.vt == VT_LPWSTR && v.pwszVal) return v.pwszVal;
    if (v.vt == VT_LPSTR && v.pszVal) return Utf8ToW(v.pszVal);
    if (v.vt == VT_BSTR && v.bstrVal) return v.bstrVal;
    return L"";
}

// degrees from the EXIF rational triple (degrees, minutes, seconds)
static bool PropDegrees(const PROPVARIANT& v, double& out) {
    double part[3];
    if (v.vt == (VT_VECTOR | VT_UI8) && v.cauh.cElems >= 3) {           // EXIF rationals
        for (int i = 0; i < 3; i++) {
            ULARGE_INTEGER r = v.cauh.pElems[i];
            part[i] = r.HighPart ? (double)r.LowPart / r.HighPart : 0;
        }
    } else if (v.vt == (VT_VECTOR | VT_R8) && v.cadbl.cElems >= 3) {     // the photo policy gives doubles
        for (int i = 0; i < 3; i++) part[i] = v.cadbl.pElems[i];
    } else return false;
    out = part[0] + part[1] / 60 + part[2] / 3600;
    return true;
}

static void ReadMeta(IWICBitmapFrameDecode* frame, Image& out) {
    ComPtr<IWICMetadataQueryReader> q;
    if (FAILED(frame->GetMetadataQueryReader(&q)) || !q) return;
    auto get = [&](const wchar_t* name, PROPVARIANT& v) { PropVariantInit(&v); return SUCCEEDED(q->GetMetadataByName(name, &v)); };
    auto meta = std::make_shared<ImageMeta>();
    PROPVARIANT v;
    if (get(L"System.Photo.DateTaken", v)) {
        if (v.vt == VT_FILETIME) {
            FILETIME local; FileTimeToLocalFileTime(&v.filetime, &local);   // EXIF dates are local time
            SYSTEMTIME st; FileTimeToSystemTime(&local, &st);
            meta->dateTaken = Format(L"%04d:%02d:%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        } else meta->dateTaken = PropString(v);
        PropVariantClear(&v);
    }
    if (get(L"System.Photo.CameraManufacturer", v)) { meta->make = PropString(v); PropVariantClear(&v); }
    if (get(L"System.Photo.CameraModel", v)) { meta->model = PropString(v); PropVariantClear(&v); }
    if (get(L"System.ApplicationName", v)) { meta->software = PropString(v); PropVariantClear(&v); }
    // GPS: the photo policy, or the EXIF GPS directory of JPEG / TIFF
    for (const wchar_t* base : { L"System.GPS.", L"/app1/ifd/gps/", L"/ifd/gps/" }) {
        bool policy = base[0] == L'S';
        auto name = [&](const wchar_t* pol, int tag) { return policy ? std::wstring(base) + pol : std::wstring(base) + Format(L"{ushort=%d}", tag); };
        double lat = 0, lon = 0;
        PROPVARIANT a, b;
        bool okLat = get(name(L"Latitude", 2).c_str(), a) && PropDegrees(a, lat);
        bool okLon = get(name(L"Longitude", 4).c_str(), b) && PropDegrees(b, lon);
        PropVariantClear(&a); PropVariantClear(&b);
        if (!okLat || !okLon) continue;
        if (get(name(L"LatitudeRef", 1).c_str(), v)) { if (PropString(v).rfind(L"S", 0) == 0) lat = -lat; PropVariantClear(&v); }
        if (get(name(L"LongitudeRef", 3).c_str(), v)) { if (PropString(v).rfind(L"W", 0) == 0) lon = -lon; PropVariantClear(&v); }
        meta->gps = true; meta->lat = lat; meta->lon = lon;
        break;
    }
    if (!meta->empty()) out.meta = meta;
}

// photos from phones and cameras are stored sideways with an EXIF orientation: the image is turned upright on load
static void ApplyOrientation(IWICBitmapFrameDecode* frame, Image& img) {
    ComPtr<IWICMetadataQueryReader> q;
    if (FAILED(frame->GetMetadataQueryReader(&q)) || !q) return;
    PROPVARIANT v; PropVariantInit(&v);
    if (FAILED(q->GetMetadataByName(L"System.Photo.Orientation", &v))) return;
    int o = v.vt == VT_UI2 ? v.uiVal : v.vt == VT_UI4 ? (int)v.ulVal : 1;
    PropVariantClear(&v);
    if (o < 2 || o > 8) return;
    bool swap = o >= 5;
    Image r(swap ? img.h : img.w, swap ? img.w : img.h);
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            int nx = x, ny = y;
            switch (o) {
            case 2: nx = img.w - 1 - x; break;                         // mirrored
            case 3: nx = img.w - 1 - x; ny = img.h - 1 - y; break;     // 180°
            case 4: ny = img.h - 1 - y; break;
            case 5: nx = y; ny = x; break;
            case 6: nx = img.h - 1 - y; ny = x; break;                 // 90° clockwise
            case 7: nx = img.h - 1 - y; ny = img.w - 1 - x; break;
            case 8: nx = y; ny = img.w - 1 - x; break;                 // 90° counterclockwise
            }
            r.at(nx, ny) = img.at(x, y);
        }
    r.dpi = img.dpi; r.meta = img.meta;
    img = std::move(r);
}

static bool DecodeWith(IWICImagingFactory* f, IWICBitmapDecoder* dec, Image& out) {
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom))) return false;
    UINT w, h; conv->GetSize(&w, &h);
    out = Image(w, h);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)out.px.size() * 4, (BYTE*)out.px.data()))) return false;
    ReadMeta(frame.Get(), out);
    ApplyOrientation(frame.Get(), out);
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
    ComScope cs;
    auto f = Factory();
    if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    HRESULT hr = f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    bool ok = SUCCEEDED(hr) && DecodeWith(f.Get(), dec.Get(), out);
    if (!ok) {
        ImageFormat fmt = FormatFromExt(FileExt(path));
        g_ioError = CodecHint(fmt);
        if (g_ioError.empty() && hr == WINCODEC_ERR_COMPONENTNOTFOUND) g_ioError = Tr(L"Formato de imagem não suportado.");
    }
    return ok;
}

bool DecodeImage(IStream* stream, Image& out) {
    g_ioError.clear();
    {
        BYTE head[12] = {}; ULONG rd = 0;
        LARGE_INTEGER z{};
        stream->Seek(z, STREAM_SEEK_SET, nullptr);
        stream->Read(head, sizeof head, &rd);
        stream->Seek(z, STREAM_SEEK_SET, nullptr);
        if (rd == sizeof head && IsWebP(head, sizeof head)) {
            STATSTG ss{}; stream->Stat(&ss, STATFLAG_NONAME);
            std::vector<BYTE> d((size_t)ss.cbSize.QuadPart);
            ULONG got = 0;
            if (d.empty() || FAILED(stream->Read(d.data(), (ULONG)d.size(), &got)) || got != d.size()) return false;
            return DecodeWebP(d.data(), d.size(), out);
        }
    }
    ComScope cs;
    auto f = Factory();
    if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    return DecodeWith(f.Get(), dec.Get(), out);
}

// metadata written by Snapnab (unless stripped): the program, the date and what the source photo had
static void WriteMeta(IWICBitmapFrameEncode* frame, ImageFormat fmt, const Image& img) {
    ComPtr<IWICMetadataQueryWriter> q;
    if (FAILED(frame->GetMetadataQueryWriter(&q)) || !q) return;
    auto putStr = [&](const wchar_t* name, const std::wstring& s) {
        if (s.empty()) return;
        std::string u = WToUtf8(s);
        PROPVARIANT v; PropVariantInit(&v);
        v.vt = VT_LPSTR; v.pszVal = u.data();
        q->SetMetadataByName(name, &v);          // not cleared: the string belongs to u
    };
    SYSTEMTIME st; GetLocalTime(&st);
    std::wstring now = Format(L"%04d:%02d:%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring software = L"Snapnab " + std::wstring(kAppVersion);
    const ImageMeta* m = img.meta.get();
    std::wstring date = m && !m->dateTaken.empty() ? m->dateTaken : now;
    if (fmt == ImageFormat::PNG) {
        putStr(L"/tEXt/{str=Software}", software);
        putStr(L"/tEXt/{str=Creation Time}", date);
        return;
    }
    if (fmt != ImageFormat::JPEG && fmt != ImageFormat::TIFF) return;
    std::wstring ifd = fmt == ImageFormat::JPEG ? L"/app1/ifd" : L"/ifd";
    putStr((ifd + L"/{ushort=305}").c_str(), software);
    putStr((ifd + L"/{ushort=306}").c_str(), now);
    putStr((ifd + L"/exif/{ushort=36867}").c_str(), date);
    if (!m) return;
    putStr((ifd + L"/{ushort=271}").c_str(), m->make);
    putStr((ifd + L"/{ushort=272}").c_str(), m->model);
    if (m->gps) {
        auto putDeg = [&](const wchar_t* tag, double deg) {
            deg = std::fabs(deg);
            ULARGE_INTEGER r[3];
            double mins = (deg - std::floor(deg)) * 60;
            r[0].LowPart = (DWORD)std::floor(deg); r[0].HighPart = 1;
            r[1].LowPart = (DWORD)std::floor(mins); r[1].HighPart = 1;
            r[2].LowPart = (DWORD)std::lround((mins - std::floor(mins)) * 60 * 1000); r[2].HighPart = 1000;
            PROPVARIANT v; PropVariantInit(&v);
            v.vt = VT_VECTOR | VT_UI8; v.cauh.cElems = 3; v.cauh.pElems = r;
            q->SetMetadataByName((ifd + L"/gps/" + tag).c_str(), &v);
        };
        putStr((ifd + L"/gps/{ushort=1}").c_str(), m->lat < 0 ? L"S" : L"N");
        putDeg(L"{ushort=2}", m->lat);
        putStr((ifd + L"/gps/{ushort=3}").c_str(), m->lon < 0 ? L"W" : L"E");
        putDeg(L"{ushort=4}", m->lon);
    }
}

// AVIF through the Windows HEIF encoder with AV1 compression
static ComPtr<IWICBitmapEncoder> CreateAvifEncoder(IWICImagingFactory* f) {
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatHeif, nullptr, &enc))) enc.Reset();
    return enc;
}

bool EncodeImage(IStream* stream, const Image& imgIn, ImageFormat fmt, int quality) {
    SaveOptions o = SaveOptions::FromSettings();
    o.jpegQuality = quality;
    return EncodeImageEx(stream, imgIn, fmt, o);
}

bool EncodeImageEx(IStream* stream, const Image& imgIn, ImageFormat fmt, const SaveOptions& opt) {
    g_ioError.clear();
    if (imgIn.empty()) return false;
    if (fmt == ImageFormat::WEBP) {
        std::vector<BYTE> d;
        if (!EncodeWebP(imgIn, opt, d)) return false;
        ULONG wr = 0;
        return SUCCEEDED(stream->Write(d.data(), (ULONG)d.size(), &wr)) && wr == d.size();
    }
    ComScope cs;
    auto f = Factory();
    if (!f) return false;
    const int quality = opt.jpegQuality;
    GUID container;
    switch (fmt) {
    case ImageFormat::PNG: container = GUID_ContainerFormatPng; break;
    case ImageFormat::JPEG: container = GUID_ContainerFormatJpeg; break;
    case ImageFormat::BMP: container = GUID_ContainerFormatBmp; break;
    case ImageFormat::GIF: container = GUID_ContainerFormatGif; break;
    case ImageFormat::TIFF: container = GUID_ContainerFormatTiff; break;
    case ImageFormat::AVIF: container = GUID_ContainerFormatHeif; break;
    default: return false;
    }
    // JPEG/BMP have no alpha: composite over white
    const Image* img = &imgIn;
    Image flat;
    if ((fmt == ImageFormat::JPEG || fmt == ImageFormat::BMP) && imgIn.HasTransparency()) {
        flat = Image(imgIn.w, imgIn.h, 0xFFFFFFFF);
        flat.Blit(imgIn, 0, 0, true);
        img = &flat;
    }
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(f->CreateEncoder(container, nullptr, &enc))) {
        if (fmt == ImageFormat::AVIF) g_ioError = CodecHint(fmt);
        return false;
    }
    if (FAILED(enc->Initialize(stream, WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(enc->CreateNewFrame(&frame, &props))) return false;
    if ((fmt == ImageFormat::JPEG || fmt == ImageFormat::AVIF) && props) {
        PROPBAG2 pq{}; pq.pstrName = (LPOLESTR)L"ImageQuality";
        VARIANT v; VariantInit(&v); v.vt = VT_R4;
        v.fltVal = std::clamp(fmt == ImageFormat::AVIF ? opt.avifQuality : quality, 1, 100) / 100.0f;
        props->Write(1, &pq, &v);
    }
    if (fmt == ImageFormat::AVIF && props) {
        PROPBAG2 pc{}; pc.pstrName = (LPOLESTR)L"HeifCompressionMethod";
        VARIANT v; VariantInit(&v); v.vt = VT_UI1; v.bVal = WICHeifCompressionAV1;
        if (FAILED(props->Write(1, &pc, &v))) { g_ioError = CodecHint(fmt); return false; }
    }
    if (FAILED(frame->Initialize(props.Get()))) { if (fmt == ImageFormat::AVIF) g_ioError = CodecHint(fmt); return false; }
    frame->SetSize(img->w, img->h);
    if (imgIn.dpi > 0) frame->SetResolution(imgIn.dpi, imgIn.dpi);
    if (!opt.stripMetadata) WriteMeta(frame.Get(), fmt, imgIn);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&pf);
    HRESULT hr;
    if (pf == GUID_WICPixelFormat32bppBGRA) {
        hr = frame->WritePixels(img->h, img->w * 4, (UINT)img->px.size() * 4, (BYTE*)img->px.data());
    } else {
        // the encoder asked for another format (JPEG: 24bpp; GIF: indexed) → WIC converter
        ComPtr<IWICBitmap> src;
        f->CreateBitmapFromMemory(img->w, img->h, GUID_WICPixelFormat32bppBGRA, img->w * 4,
                                  (UINT)img->px.size() * 4, (BYTE*)img->px.data(), &src);
        ComPtr<IWICFormatConverter> conv;
        f->CreateFormatConverter(&conv);
        ComPtr<IWICPalette> pal;
        if (fmt == ImageFormat::GIF) {
            f->CreatePalette(&pal);
            pal->InitializeFromBitmap(src.Get(), 256, img->HasTransparency());
        }
        hr = conv->Initialize(src.Get(), pf, pal ? WICBitmapDitherTypeErrorDiffusion : WICBitmapDitherTypeNone,
                              pal.Get(), 0, pal ? WICBitmapPaletteTypeCustom : WICBitmapPaletteTypeMedianCut);
        if (SUCCEEDED(hr)) {
            if (pal) frame->SetPalette(pal.Get());
            hr = frame->WriteSource(conv.Get(), nullptr);
        }
    }
    if (FAILED(hr)) { if (fmt == ImageFormat::AVIF) g_ioError = CodecHint(fmt); return false; }
    if (FAILED(frame->Commit()) || FAILED(enc->Commit())) { if (fmt == ImageFormat::AVIF) g_ioError = CodecHint(fmt); return false; }
    return true;
}

bool SaveImageFile(const std::wstring& path, const Image& img, int quality) {
    SaveOptions o = SaveOptions::FromSettings();
    o.jpegQuality = quality;
    return SaveImageFileEx(path, img, o);
}

bool SaveImageFileEx(const std::wstring& path, const Image& img, const SaveOptions& opt) {
    ImageFormat fmt = FormatFromExt(FileExt(path));
    if (fmt == ImageFormat::Unknown || fmt == ImageFormat::HEIC) fmt = ImageFormat::PNG;     // HEIC is only opened
    ComPtr<IStream> st;
    std::wstring tmp = path + L".tmp";
    if (FAILED(SHCreateStreamOnFileEx(tmp.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
                                      FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &st))) return false;
    bool ok = EncodeImageEx(st.Get(), img, fmt, opt);
    st.Reset();
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

bool EncodeToMemoryEx(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, const SaveOptions& opt) {
    ComPtr<IStream> st;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &st))) return false;
    if (!EncodeImageEx(st.Get(), img, fmt, opt)) return false;
    STATSTG ss{}; st->Stat(&ss, STATFLAG_NONAME);
    out.resize((size_t)ss.cbSize.QuadPart);
    LARGE_INTEGER z{}; st->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG rd = 0; st->Read(out.data(), (ULONG)out.size(), &rd);
    return rd == out.size();
}

bool DecodeFromMemory(const BYTE* data, size_t len, Image& out) {
    ComPtr<IStream> st;
    st.Attach(SHCreateMemStream(data, (UINT)len));
    return st && DecodeImage(st.Get(), out);
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
