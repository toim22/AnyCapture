// The WIC image codecs of Image.cpp as they were before they moved to Rust (rust/snapcore/src/codec.rs), kept only to
// check that the Rust ones read and write the same files (tests.cpp, [18] "Image files: Rust and the former C++").
// WebP is not here: it stays with libwebp in Image.cpp.
#include "Common.h"
#include "Image.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <propvarutil.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace snap::imgref {

struct ComScope {
    HRESULT hr;
    ComScope() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

static thread_local std::wstring g_ioError;
const std::wstring& Error() { return g_ioError; }
bool EncodeImageEx(IStream* stream, const Image& imgIn, ImageFormat fmt, const SaveOptions& opt);
bool EncodeToMemoryEx(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, const SaveOptions& opt);

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
    imgref::ReadMeta(frame.Get(), out);
    imgref::ApplyOrientation(frame.Get(), out);
    return true;
}

bool LoadImageFile(const std::wstring& path, Image& out) {
    g_ioError.clear();
    ComScope cs;
    auto f = Factory();
    if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    HRESULT hr = f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    bool ok = SUCCEEDED(hr) && imgref::DecodeWith(f.Get(), dec.Get(), out);
    if (!ok) {
        ImageFormat fmt = FormatFromExt(FileExt(path));
        g_ioError = CodecHint(fmt);
        if (g_ioError.empty() && hr == WINCODEC_ERR_COMPONENTNOTFOUND) g_ioError = Tr(L"Formato de imagem não suportado.");
    }
    return ok;
}

bool DecodeImage(IStream* stream, Image& out) {
    g_ioError.clear();
    ComScope cs;
    auto f = Factory();
    if (!f) return false;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    return imgref::DecodeWith(f.Get(), dec.Get(), out);
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
bool EncodeImage(IStream* stream, const Image& imgIn, ImageFormat fmt, int quality) {
    SaveOptions o = SaveOptions::FromSettings();
    o.jpegQuality = quality;
    return imgref::EncodeImageEx(stream, imgIn, fmt, o);
}

bool EncodeImageEx(IStream* stream, const Image& imgIn, ImageFormat fmt, const SaveOptions& opt) {
    g_ioError.clear();
    if (imgIn.empty()) return false;
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
    if (!opt.stripMetadata) imgref::WriteMeta(frame.Get(), fmt, imgIn);
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

bool EncodeToMemory(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, int quality) {
    SaveOptions o = SaveOptions::FromSettings();
    o.jpegQuality = quality;
    o.stripMetadata = true;                  // internal copies (.snap streams, PDF, clipboard) carry no metadata
    return imgref::EncodeToMemoryEx(img, fmt, out, o);
}

bool EncodeToMemoryEx(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, const SaveOptions& opt) {
    ComPtr<IStream> st;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &st))) return false;
    if (!imgref::EncodeImageEx(st.Get(), img, fmt, opt)) return false;
    STATSTG ss{}; st->Stat(&ss, STATFLAG_NONAME);
    out.resize((size_t)ss.cbSize.QuadPart);
    LARGE_INTEGER z{}; st->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG rd = 0; st->Read(out.data(), (ULONG)out.size(), &rd);
    return rd == out.size();
}

bool DecodeFromMemory(const BYTE* data, size_t len, Image& out) {
    ComPtr<IStream> st;
    st.Attach(SHCreateMemStream(data, (UINT)len));
    return st && imgref::DecodeImage(st.Get(), out);
}

// a JPEG with an EXIF orientation (1–8), as phones write them: for the test of the upright turn
bool EncodeJpegWithOrientation(const Image& img, int orientation, std::vector<BYTE>& out) {
    ComScope cs;
    auto f = Factory();
    ComPtr<IStream> st;
    if (!f || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &st))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) || FAILED(enc->Initialize(st.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(enc->CreateNewFrame(&frame, &props)) || FAILED(frame->Initialize(props.Get()))) return false;
    frame->SetSize(img.w, img.h);
    ComPtr<IWICMetadataQueryWriter> q;
    if (FAILED(frame->GetMetadataQueryWriter(&q))) return false;
    PROPVARIANT v; PropVariantInit(&v);
    v.vt = VT_UI2; v.uiVal = (USHORT)orientation;
    if (FAILED(q->SetMetadataByName(L"/app1/ifd/{ushort=274}", &v))) return false;
    ComPtr<IWICBitmap> src;
    f->CreateBitmapFromMemory(img.w, img.h, GUID_WICPixelFormat32bppBGRA, img.w * 4, (UINT)img.px.size() * 4, (BYTE*)img.px.data(), &src);
    WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
    frame->SetPixelFormat(&pf);
    ComPtr<IWICFormatConverter> conv;
    f->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(src.Get(), pf, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(frame->WriteSource(conv.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(enc->Commit())) return false;
    STATSTG ss{}; st->Stat(&ss, STATFLAG_NONAME);
    out.resize((size_t)ss.cbSize.QuadPart);
    LARGE_INTEGER z{}; st->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG rd = 0; st->Read(out.data(), (ULONG)out.size(), &rd);
    return rd == out.size();
}

} // namespace snap::imgref
