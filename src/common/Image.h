// In-memory image (BGRA 32 bpp, straight alpha) + format I/O (role of LEADTOOLS, ds_arch §13.3 → WIC)
#pragma once
#include "Common.h"

namespace snap {

// metadata carried with an image (C29): what an imported photo had (EXIF) and what Snapnab writes when saving, unless
// the user removes it (StripMetadata)
struct ImageMeta {
    std::wstring dateTaken;             // "YYYY:MM:DD HH:MM:SS" (EXIF form)
    std::wstring make, model;           // camera
    std::wstring software;              // program that created the source file
    bool gps = false;
    double lat = 0, lon = 0;            // degrees (south / west negative)
    bool empty() const { return dateTaken.empty() && make.empty() && model.empty() && software.empty() && !gps; }
};

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;           // 0xAARRGGBB per pixel (BGRA in memory), row by row from top to bottom
    float dpi = 0;                      // resolution written to the file (0 = encoder default)
    std::shared_ptr<ImageMeta> meta;    // metadata of the source photo (null = none)

    Image() = default;
    Image(int width, int height, uint32_t fill = 0) : w(width), h(height), px((size_t)width * height, fill) {}

    bool empty() const { return w <= 0 || h <= 0; }
    uint32_t* row(int y) { return px.data() + (size_t)y * w; }
    const uint32_t* row(int y) const { return px.data() + (size_t)y * w; }
    uint32_t& at(int x, int y) { return px[(size_t)y * w + x]; }
    uint32_t at(int x, int y) const { return px[(size_t)y * w + x]; }

    Image Crop(RECT r) const;
    void Blit(const Image& src, int dx, int dy, bool blend = true);
    void FillRect(RECT r, uint32_t color);
    bool HasTransparency() const;
    void MakeOpaque();

    // GDI / GDI+ bridges
    HBITMAP ToHBITMAP() const;                                     // DIB section 32 bpp top-down
    static Image FromHBITMAP(HBITMAP hbm);
    static Image FromDC(HDC hdc, RECT r);                          // BitBlt of an area of the DC
    std::unique_ptr<Gdiplus::Bitmap> WrapGdip();                   // shares px (no copy)
    static Image FromGdip(Gdiplus::Bitmap& bmp);
    void DrawTo(HDC hdc, int x, int y, int dw, int dh, bool smooth) const;  // StretchDIBits
};

inline uint32_t ARGB(int a, int r, int g, int b) { return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }
inline uint32_t FromColorref(COLORREF c, int a = 255) { return ARGB(a, GetRValue(c), GetGValue(c), GetBValue(c)); }
inline COLORREF ToColorref(uint32_t p) { return RGB((p >> 16) & 255, (p >> 8) & 255, p & 255); }
inline int CA(uint32_t p) { return (p >> 24) & 255; }
inline int CR(uint32_t p) { return (p >> 16) & 255; }
inline int CG(uint32_t p) { return (p >> 8) & 255; }
inline int CB(uint32_t p) { return p & 255; }
uint32_t BlendOver(uint32_t dst, uint32_t src);

// ---- Formats (E1/K5) ----
// WebP through libwebp (always available); AVIF and HEIC through the Windows codecs (WIC: AV1 Video Extension, HEIF
// Image Extensions, HEVC Video Extensions), with a hint on how to get a missing one (C03)
enum class ImageFormat { PNG, JPEG, BMP, GIF, TIFF, WEBP, AVIF, HEIC, Unknown };
ImageFormat FormatFromExt(const std::wstring& ext);
const wchar_t* FormatExt(ImageFormat fmt);                 // ".png", ".webp"…

// how a file is written: qualities of the lossy formats and whether the metadata is removed (C29). FromSettings reads
// the user's choices (WebPLossless, WebPQuality, AvifQuality, StripMetadata in the registry)
struct SaveOptions {
    int jpegQuality = 90;
    bool webpLossless = true;
    int webpQuality = 90;
    int avifQuality = 80;
    bool stripMetadata = false;
    static SaveOptions FromSettings();
};

bool LoadImageFile(const std::wstring& path, Image& out);
bool SaveImageFile(const std::wstring& path, const Image& img, int jpegQuality = 90);
bool SaveImageFileEx(const std::wstring& path, const Image& img, const SaveOptions& opt);
bool EncodeImage(IStream* stream, const Image& img, ImageFormat fmt, int jpegQuality = 90);
bool EncodeImageEx(IStream* stream, const Image& img, ImageFormat fmt, const SaveOptions& opt);
bool DecodeImage(IStream* stream, Image& out);
// in memory (internal use: .snap streams, PDF, clipboard): without metadata unless options are given
bool EncodeToMemory(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, int jpegQuality = 90);
bool EncodeToMemoryEx(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, const SaveOptions& opt);
bool DecodeFromMemory(const BYTE* data, size_t len, Image& out);
const std::wstring& ImageIoError();    // why the last load/save on this thread failed (codec missing…), or ""
std::wstring CodecHint(ImageFormat fmt);   // how to install the Windows codec of AVIF/HEIC ("" for the others)
bool CanEncode(ImageFormat fmt);       // AVIF needs a Windows AV1 encoder
std::wstring ImageOpenFilter();        // filter for GetOpenFileName
std::wstring ImageSaveFilter();

// ---- Clipboard (E2) ----
bool CopyImageToClipboard(HWND owner, const Image& img);
bool ClipboardHasImage();
bool GetClipboardImage(Image& out);

// ---- Thumbnail ----
Image MakeThumbnail(const Image& img, int maxW, int maxH);
Image ScaleImage(const Image& img, int nw, int nh, bool highQuality = true);

} // namespace snap
