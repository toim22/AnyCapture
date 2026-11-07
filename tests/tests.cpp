// Integration tests of Snapnab's critical pieces (console executable, not part of the product)
#include "Common.h"
#include "Image.h"
#include "Cv.h"
#include "Gif.h"
#include "SnapFile.h"
#include "Xml.h"
#include "Ipc.h"
#include "Effects.h"
#include "Icons.h"
#include "Ocr.h"
#include "Settings.h"
#include "../src/capture/Recorder.h"
#include "../src/capture/Panoramic.h"
#include "../src/capture/Webcam.h"
#include "../src/capture/Presets.h"
#include "../src/editor/Video.h"
#include "../src/editor/Document.h"
#include "../src/editor/QuickStyles.h"
#include "../src/editor/EditorTools.h"
#include "Beautify.h"
#include "Color.h"
#include "Net.h"
#include "Upload.h"
#include "Library.h"
#include "SnapcoreBridge.h"
#include "Barcode.h"
#include "Hdr.h"
#include <cstdio>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <shlobj.h>

using namespace snap;

namespace snap::xmlref {      // the former C++ reader and writer (XmlReference.cpp)
std::string Serialize(const XmlNode& root, bool decl);
bool Parse(const std::string& text, XmlNode& root);
}
namespace snap::imgref {      // the former WIC codecs (ImageCodecReference.cpp)
const std::wstring& Error();
bool LoadImageFile(const std::wstring& path, Image& out);
bool DecodeFromMemory(const BYTE* data, size_t len, Image& out);
bool EncodeToMemoryEx(const Image& img, ImageFormat fmt, std::vector<BYTE>& out, const SaveOptions& opt);
bool EncodeJpegWithOrientation(const Image& img, int orientation, std::vector<BYTE>& out);
}
namespace snap::snapref {     // the former C++ .snap reading and writing (SnapFileReference.cpp)
bool SaveSnapFile(const std::wstring& path, const std::vector<SnapFrame>& frames);
bool LoadSnapFile(const std::wstring& path, std::vector<SnapFrame>& frames);
bool LoadSnapDocument(const std::wstring& path, XmlNode& doc);
}
namespace snap::libref {      // the former C++ library, with the folder as a parameter (LibraryReference.cpp)
std::wstring LibraryDir();
std::vector<LibraryItem> ListLibrary(const std::wstring& lib, size_t maxItems);
std::wstring ThumbPathFor(const std::wstring& lib, const std::wstring& itemPath);
std::wstring AddImageToLibrary(const std::wstring& lib, const Image& img, const CaptureInfo& info);
std::wstring AddLayeredToLibrary(const std::wstring& lib, const Image& base, const Image& flat, const CaptureInfo& info,
                                 const XmlNode& objects, const std::map<std::string, Image>& embedded);
std::wstring NewVideoPath(const std::wstring& lib);
std::wstring AudioDir();
std::wstring NewAudioPath(bool mp3);
int CleanupLibrary(const std::wstring& lib, int days, bool keepNamed, bool keepTagged, bool recycle);
bool IsInLibrary(const std::wstring& lib, const std::wstring& path);
std::wstring CaptureName(const std::wstring& lib, const std::wstring& itemPath);
bool SetCaptureName(const std::wstring& lib, const std::wstring& itemPath, const std::wstring& name);
std::wstring NextAutoName(const std::wstring& folder, const std::wstring& prefix, const std::wstring& ext, int digits);
}
namespace snap::gifref {      // the former C++ GIF writer (GifReference.cpp)
bool WriteGif(const std::wstring& path, int w, int h, double fps, int bpp, bool dither, bool delta,
              const std::vector<std::pair<Image, int>>& frames, const std::string& comment);
}

static int g_fail = 0;
static std::wstring TestTempDir() { wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); std::wstring d = t; if (!d.empty() && d.back() == L'\\') d.pop_back(); return d; }
#define CHECK(c, ...) do { if (c) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } else { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); g_fail++; } } while (0)

static Image Pattern(int w, int h, int seed) {
    Image img(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t hsh = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u) ^ (uint32_t)(seed * 83492791u);
            hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
            int v = (int)(hsh & 255);
            // smooth blocks + noise: similar to text/icons on a page
            int blk = ((x / 24 + (y / 18) * 7 + seed) * 2654435761u >> 24) & 255;
            img.at(x, y) = ARGB(255, (blk + v / 4) & 255, (blk * 3 + v / 8) & 255, (255 - blk + v / 6) & 255);
        }
    return img;
}

static std::wstring DigitsOfTest(const std::wstring& s) {
    std::wstring d;
    for (wchar_t c : s) if (c >= L'0' && c <= L'9') d += c;
    return d;
}

// WebP fingerprints: the size and FNV-1a hash of what libwebp writes for fixed images (lossless and 5 qualities) and
// of the pixels it reads back. tests/data/webp-libwebp-1.5.0.txt holds the ones of libwebp 1.5.0 as CMake built it
// before WebP moved to Rust ("SnapnabTests webpgolden" prints those of the current build).
static uint64_t Fnv1a(const void* d, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= ((const uint8_t*)d)[i]; h *= 1099511628211ULL; }
    return h;
}

static std::vector<std::string> WebPFingerprints() {
    std::vector<std::pair<const char*, Image>> imgs;
    imgs.push_back({ "pattern-37x21", Pattern(37, 21, 1) });
    Image alpha = Pattern(64, 48, 2);
    for (size_t i = 0; i < alpha.px.size(); i++) alpha.px[i] = (alpha.px[i] & 0xFFFFFF) | ((uint32_t)(i * 7 % 256) << 24);
    imgs.push_back({ "alpha-64x48", alpha });
    imgs.push_back({ "pixel-1x1", Image(1, 1, 0xFF123456) });
    imgs.push_back({ "pattern-160x120", Pattern(160, 120, 4) });
    Image grad(96, 64);
    for (int y = 0; y < grad.h; y++)
        for (int x = 0; x < grad.w; x++) grad.at(x, y) = ARGB(128 + y, x * 255 / 95, y * 4, 255 - x * 2);
    imgs.push_back({ "gradient-96x64", grad });
    imgs.push_back({ "clear-8x8", Image(8, 8, 0) });
    imgs.push_back({ "pattern-640x480", Pattern(640, 480, 5) });
    std::vector<std::string> lines;
    for (auto& [name, img] : imgs)
        for (int q : { 0, 1, 40, 75, 90, 100 }) {          // 0: lossless
            SaveOptions o; o.webpLossless = q == 0; o.webpQuality = q;
            std::vector<BYTE> enc;
            Image dec;
            bool ok = EncodeToMemoryEx(img, ImageFormat::WEBP, enc, o) && DecodeFromMemory(enc.data(), enc.size(), dec);
            lines.push_back(FormatA("%s %s %zu %016llx %dx%d %016llx", name, q ? FormatA("q%d", q).c_str() : "lossless", ok ? enc.size() : 0,
                                    (unsigned long long)Fnv1a(enc.data(), enc.size()), dec.w, dec.h,
                                    (unsigned long long)Fnv1a(dec.px.data(), dec.px.size() * 4)));
        }
    return lines;
}

// Pixel fingerprints: what the effects, Beautify, the computer vision, scaling and the PDF writer give for fixed
// ---------------------------------------------------------------- QR codes and barcodes
// tests/data/barcodes/*.png were made once with ZXing-C++ 2.2.1's writer (every symbology, small, rotated, inverted,
// transparent, several codes in one picture, noise, none) and tests/data/barcodes.txt holds what the ZXing-C++ reader
// found in each, so the reader can change and be checked ("SnapnabTests barcodegolden" prints what this build reads).
static std::vector<std::pair<std::wstring, Image>> BarcodeImages() {
    std::vector<std::pair<std::wstring, Image>> v;
    std::ifstream golden(ExeDir() + L"\\..\\..\\..\\..\\tests\\data\\barcodes.txt");
    for (std::string l; std::getline(golden, l);) {
        size_t c = l.find(':');
        if (c == std::string::npos) continue;
        std::wstring name = Utf8ToW(l.substr(0, c));
        Image img;
        LoadImageFile(ExeDir() + L"\\..\\..\\..\\..\\tests\\data\\barcodes\\" + name + L".png", img);
        v.push_back({ name, img });
    }
    return v;
}

static std::vector<std::string> BarcodeResults(const std::vector<std::pair<std::wstring, Image>>& imgs) {
    std::vector<std::string> L;
    for (auto& [name, img] : imgs) {
        std::string line = WToUtf8(name) + ":";
        for (auto& r : barcode::Read(img))
            line += FormatA(" [%s|%s|%ld,%ld,%ld,%ld|%d]", WToUtf8(r.format).c_str(), WToUtf8(r.text).c_str(), r.box.left, r.box.top,
                            r.box.right, r.box.bottom, r.url ? 1 : 0);
        L.push_back(line);
    }
    return L;
}

// the color texts, the HDR tone mapping and OCR (Windows reads text the C++ drew), for tests/data/misc-cpp.txt
static std::vector<std::string> MiscFingerprints() {
    std::vector<std::string> L;
    const COLORREF cs[] = { RGB(0, 0, 0), RGB(255, 255, 255), RGB(255, 128, 0), RGB(18, 171, 63), RGB(128, 128, 128),
                            RGB(1, 2, 3), RGB(250, 5, 130), RGB(0, 0, 255), RGB(127, 255, 212), RGB(64, 0, 64) };
    for (COLORREF c : cs) {
        std::string line = FormatA("color %06x:", (unsigned)c);
        for (int f = 0; f < color::FmtCount; f++) line += " [" + WToUtf8(color::ToText(c, (color::Fmt)f)) + "]";
        double h, s, l;
        color::RgbToHsl(c, h, s, l);
        line += FormatA(" hsl %.17g %.17g %.17g lum %.17g contrast %.17g", h, s, l, color::Luminance(c), color::ContrastRatio(c, RGB(40, 90, 200)));
        L.push_back(line);
    }
    for (int f = 0; f < color::FmtCount; f++) L.push_back("format " + WToUtf8(color::FmtName((color::Fmt)f)));
    for (const wchar_t* t : { L"#12AB3F", L"#12ab3fzz", L"12AB3F", L"#FFF", L"# 12AB3F", L"#0x12AB3F", L"#-1", L"#123456789AB" })
        L.push_back(FormatA("hex %s -> %06x", WToUtf8(t).c_str(), (unsigned)HexToColor(t, 0x777777)));
    for (uint32_t h = 0; h < 65536; h += 997) L.push_back(FormatA("half %04x %.9g", h, hdr::HalfToFloat((uint16_t)h)));
    for (float white : { 80.0f, 200.0f, 480.0f, 0.5f }) {
        std::vector<uint8_t> lut(65536);
        for (uint32_t h = 0; h < 65536; h++) lut[h] = hdr::ToneMap(hdr::HalfToFloat((uint16_t)h), white);
        L.push_back(FormatA("tonemap %g %016llx", white, (unsigned long long)Fnv1a(lut.data(), lut.size())));
    }
    for (float v : { -1.0f, 0.0f, 0.001f, 0.5f, 1.0f, 2.0f, 12.5f, 1e9f })
        L.push_back(FormatA("tone %g: %d %d", v, hdr::ToneMap(v, 80), hdr::ToneMap(v, 240)));
    int n = 0;
    for (const wchar_t* t : { L"Hello Snapnab 2026", L"4111 1111 1111 1111", L"Grab text from the screen", L"ABC-123 xyz" }) {
        Image img = fx::Caption(Image(560, 4, 0xFFFFFFFF), t, true, 28, 0xFF000000, 0xFFFFFFFF);
        std::wstring text, err;
        bool ok = RunOcr(img, text, err);
        std::vector<OcrLine> lines;
        std::wstring err2;
        bool ok2 = RunOcrLines(img, lines, err2);
        std::string line = FormatA("ocr %d: %d [%s] %d", n++, ok, WToUtf8(text).c_str(), ok2);
        for (auto& l : lines) {
            line += FormatA(" {%s %ld,%ld,%ld,%ld", WToUtf8(l.text).c_str(), l.box.left, l.box.top, l.box.right, l.box.bottom);
            for (auto& w : l.words) line += FormatA(" <%s %ld,%ld,%ld,%ld>", WToUtf8(w.text).c_str(), w.box.left, w.box.top, w.box.right, w.box.bottom);
            line += "}";
        }
        for (auto& c : line) if (c == '\n') c = '|';
        L.push_back(line);
    }
    std::vector<RECT> faces;
    std::wstring ferr;
    bool fok = DetectFaces(Pattern(200, 150, 9), faces, ferr);
    L.push_back(FormatA("faces %d %zu", fok, faces.size()));
    return L;
}

// inputs. tests/data/pixels-cpp.txt holds the ones of the C++ code before it moved to Rust ("SnapnabTests fxgolden"
// prints those of the current build).
static std::string Fp(const char* name, const Image& img) {
    return FormatA("%s %dx%d %016llx dpi=%g meta=%d", name, img.w, img.h, (unsigned long long)Fnv1a(img.px.data(), img.px.size() * 4),
                   img.dpi, img.meta ? 1 : 0);
}
static std::string FpMask(const char* name, const cv::Mask& m) {
    return FormatA("%s %zu %016llx", name, m.size(), (unsigned long long)Fnv1a(m.data(), m.size()));
}

static std::vector<std::string> PixelFingerprints(const std::wstring& tmpDir) {
    std::vector<std::string> L;
    auto add = [&](const std::string& s) { L.push_back(s); };
    Image P = Pattern(97, 61, 3);
    P.dpi = 144; P.meta = std::make_shared<ImageMeta>(); P.meta->make = L"Maker";
    Image A = Pattern(80, 50, 4);
    for (size_t i = 0; i < A.px.size(); i++) A.px[i] = (A.px[i] & 0xFFFFFF) | ((uint32_t)(i * 13 % 256) << 24);
    Image framed(60, 40, 0xFFF0F0F0);
    framed.Blit(Pattern(30, 20, 5), 12, 9, false);
    Image tiny(1, 1, 0xFF336699);
    std::vector<std::pair<const char*, const Image*>> srcs = { { "P", &P }, { "A", &A }, { "framed", &framed }, { "tiny", &tiny } };
    for (auto& [n, im] : srcs) {
        std::string k = n;
        add(Fp((k + " rot90cw").c_str(), fx::Rotate90(*im, true)));
        add(Fp((k + " rot90ccw").c_str(), fx::Rotate90(*im, false)));
        add(Fp((k + " rot180").c_str(), fx::Rotate180(*im)));
        add(Fp((k + " flipH").c_str(), fx::FlipH(*im)));
        add(Fp((k + " flipV").c_str(), fx::FlipV(*im)));
        add(Fp((k + " rotate15").c_str(), fx::RotateAngle(*im, 15, 0xFFFFFFFF)));
        add(Fp((k + " rotate-37.5").c_str(), fx::RotateAngle(*im, -37.5, 0)));
        for (int anchor = 0; anchor < 9; anchor++) {
            add(Fp(FormatA("%s canvas+%d", n, anchor).c_str(), fx::CanvasResize(*im, im->w + 17, im->h + 8, anchor, 0xFF102030)));
            add(Fp(FormatA("%s canvas-%d", n, anchor).c_str(), fx::CanvasResize(*im, im->w / 2, im->h - 3, anchor, 0)));
        }
        add(Fp((k + " trim8").c_str(), fx::AutoTrim(*im)));
        add(Fp((k + " trim0").c_str(), fx::AutoTrim(*im, 0)));
        for (int pc : { 100, 50, 150, 7 }) add(Fp(FormatA("%s scale%d", n, pc).c_str(), fx::Scale(*im, pc)));
        auto inplace = [&](const char* what, auto f) { Image t = *im; f(t); add(Fp((k + " " + what).c_str(), t)); };
        inplace("gray", [](Image& t) { fx::Grayscale(t); });
        inplace("invert", [](Image& t) { fx::Invert(t); });
        inplace("sepia", [](Image& t) { fx::Sepia(t); });
        inplace("mono128", [](Image& t) { fx::Monochrome(t); });
        inplace("mono60", [](Image& t) { fx::Monochrome(t, 60); });
        inplace("bc30-20", [](Image& t) { fx::BrightnessContrast(t, 30, -20); });
        inplace("bc-50+70", [](Image& t) { fx::BrightnessContrast(t, -50, 70); });
        inplace("hs45+30", [](Image& t) { fx::HueSaturation(t, 45, 30); });
        inplace("hs-120-60", [](Image& t) { fx::HueSaturation(t, -120, -60); });
        inplace("gamma0.5", [](Image& t) { fx::Gamma(t, 0.5); });
        inplace("gamma2.2", [](Image& t) { fx::Gamma(t, 2.2); });
        inplace("replace", [&](Image& t) { fx::ReplaceColor(t, im->px[0], 0xFFFF00FF, 10); });
        inplace("blur", [](Image& t) { fx::BoxBlur(t, { 3, 2, 40, 30 }, 4); });
        inplace("blurAll", [](Image& t) { fx::BoxBlur(t, { -5, -5, 500, 500 }, 9); });
        inplace("pixelate", [](Image& t) { fx::Pixelate(t, { 5, 4, 70, 45 }, 7); });
        inplace("pixelate1", [](Image& t) { fx::Pixelate(t, { 0, 0, 500, 500 }, 1); });
        inplace("emboss3", [](Image& t) { fx::Emboss(t, 3); });
        inplace("emboss10", [](Image& t) { fx::Emboss(t, 12); });
        inplace("sharpen40", [](Image& t) { fx::Sharpen(t, 40); });
        inplace("sharpen100", [](Image& t) { fx::Sharpen(t, 100); });
        inplace("watermark", [](Image& t) { fx::Watermark(t, L"Snapnab", 35, false, false); });
        inplace("watermarkTiled", [](Image& t) { fx::Watermark(t, L"Confidencial ação", 60, true, true); });
        add(Fp((k + " border").c_str(), fx::Border(*im, 5, 0xFF00FF00)));
        add(Fp((k + " shadow").c_str(), fx::Shadow(*im, 6, 6, 50)));
        add(Fp((k + " shadow0").c_str(), fx::Shadow(*im, 0, 1, 100)));
        for (int st = 0; st < 6; st++)
            for (int sides : { (int)fx::All, fx::Bottom | fx::Right, fx::Top | fx::Left })
                for (bool sh : { false, true })
                    add(Fp(FormatA("%s edge%d-%d-%d", n, st, sides, sh).c_str(), fx::EdgeEffect(*im, (fx::Edge)st, 9, sides, sh)));
        add(Fp((k + " persp").c_str(), fx::Perspective(*im, 40, -25, 30)));
        add(Fp((k + " persp2").c_str(), fx::Perspective(*im, -100, 100, -100)));
        for (int c = 0; c < 4; c++) add(Fp(FormatA("%s curl%d", n, c).c_str(), fx::PageCurl(*im, 20, c)));
        add(Fp((k + " spot").c_str(), fx::Spotlight(*im, { 10, 8, 40, 30 }, 50, 3, 100, false)));
        add(Fp((k + " spotZoom").c_str(), fx::Spotlight(*im, { 10, 8, 40, 30 }, 70, 0, 150, true)));
        add(Fp((k + " spotOut").c_str(), fx::Spotlight(*im, { 500, 500, 600, 600 }, 70, 0, 150, true)));
        add(Fp((k + " caption").c_str(), fx::Caption(*im, L"Snapnab — teste", false, 12, 0xFFFFFFFF, 0xFF202020)));
        add(Fp((k + " captionTop").c_str(), fx::Caption(*im, L"Um texto bem comprido que não cabe na largura", true, 14, 0xFF000000, 0x80FFFFFF)));
        add(Fp((k + " scaleHQ").c_str(), ScaleImage(*im, 45, 31)));
        add(Fp((k + " scaleNN").c_str(), ScaleImage(*im, 150, 20, false)));
        add(Fp((k + " thumb").c_str(), MakeThumbnail(*im, 40, 40)));
    }
    // the capture preset effects, alone and together (no {date}/{time} tokens: they change)
    {
        fx::CaptureEffects e;
        auto run = [&](const char* name) { add(Fp(name, fx::ApplyCaptureEffects(P, e))); add(Fp((std::string(name) + "-A").c_str(), fx::ApplyCaptureEffects(A, e))); };
        run("ce none");
        e.trim = true; run("ce trim");
        e.rotate = true; for (int r = 0; r < 5; r++) { e.rotateKind = r; run(FormatA("ce rotate%d", r).c_str()); }
        e.scale = true; e.scalePercent = 80; run("ce scale");
        e.adjust = true; e.brightness = 10; e.contrast = 20; e.hue = 30; e.saturation = -10; run("ce adjust");
        e.filter = true; for (int f = 0; f < 4; f++) { e.filterKind = f; run(FormatA("ce filter%d", f).c_str()); }
        e.replaceColor = true; run("ce replace");
        e.watermark = true; run("ce watermark");
        e.caption = true; e.captionText = L"Snapnab"; e.captionTop = true; run("ce caption");
        e.border = true; run("ce border");
        e.edge = true; e.edgeSides = 0; run("ce edge");
        e.shadow = true; run("ce shadow");
        e.beautify = true; run("ce beautify");
        e.resolution = true; e.dpi = 200; run("ce resolution");
    }
    // Beautify: backgrounds, aspect ratios, alignment, corners, shadow, auto balance
    {
        std::wstring bgFile = tmpDir + L"\\beautify_bg.png";
        SaveImageFile(bgFile, Pattern(50, 90, 9));
        int i = 0;
        for (int bg : { 0, 1, 3, 4 })
            for (int aspect = 0; aspect < 5; aspect++)
                for (int align : { 0, 4, 8 }) {
                    fx::BeautifyOptions o;
                    o.background = bg; o.aspect = aspect; o.align = align; o.imagePath = bgFile;
                    o.angle = (i * 47) % 360; o.padding = 10 + i % 30; o.radius = i % 25; o.shadow = (i * 7) % 101;
                    o.shadowSize = i % 30; o.autoBalance = i % 2 == 0;
                    POINT org{};
                    for (auto& [n, im] : srcs) {
                        Image r = fx::Beautify(*im, o, &org);
                        add(Fp(FormatA("beautify%d %s", i, n).c_str(), r) + FormatA(" at %d,%d", org.x, org.y));
                    }
                    i++;
                }
        DeleteFileW(bgFile.c_str());
    }
    // computer vision
    {
        Image W = Pattern(64, 48, 7);
        for (int y = 10; y < 30; y++) for (int x = 5; x < 40; x++) W.at(x, y) = 0xFF2040F0 + (uint32_t)((x * y) % 5);
        for (auto [sx, sy] : { std::pair{ 6, 11 }, { 0, 0 }, { 63, 47 }, { -1, 3 } })
            for (float tol : { 0.0f, 1.0f, 5.0f, 30.0f, 100.0f, 150.0f })
                for (bool global : { false, true }) {
                    add(FpMask(FormatA("wand %d,%d %g %d", sx, sy, tol, global).c_str(), cv::WandSelect(W, { sx, sy }, tol, global)));
                    add(FpMask(FormatA("dowand %d,%d %g %d", sx, sy, tol, global).c_str(), cv::DoWand(W, { sx, sy }, tol, global)));
                }
        add(FpMask("rect", cv::RectMask(64, 48, { -3, 5, 30, 70 })));
        add(FpMask("ellipse", cv::EllipseMask(64, 48, { 4, 3, 50, 40 })));
        add(FpMask("ellipseThin", cv::EllipseMask(64, 48, { 10, 10, 10, 30 })));
        add(FpMask("polygon", cv::PolygonMask(64, 48, { { 5, 5 }, { 60, 10 }, { 30, 45 }, { 2, 30 } })));
        add(FpMask("polygonStar", cv::PolygonMask(64, 48, { { 32, 0 }, { 40, 47 }, { 0, 15 }, { 63, 15 }, { 24, 47 } })));
        add(FpMask("polygon2", cv::PolygonMask(64, 48, { { 5, 5 }, { 60, 10 } })));
        cv::Mask m = cv::EllipseMask(64, 48, { 20, 12, 44, 36 });
        RECT b = cv::MaskBounds(m, 64, 48);
        add(FormatA("bounds %d %d %d %d empty %d %d", b.left, b.top, b.right, b.bottom, cv::MaskEmpty(m), cv::MaskEmpty(cv::Mask(10, 0))));
        b = cv::MaskBounds(cv::Mask(64 * 48, 0), 64, 48);
        add(FormatA("bounds0 %d %d %d %d", b.left, b.top, b.right, b.bottom));
        for (int k : { 1, 3, 7, 12 }) add(FpMask(FormatA("dilate%d", k).c_str(), cv::Dilate(m, 64, 48, k)));
        for (int k : { 3, 7 }) {
            Image t = W; uint32_t dom = 0; double share = 0;
            bool ok = cv::Dingo7Inpaint(t, m, k, &dom, &share);
            add(Fp(FormatA("dingo%d", k).c_str(), t) + FormatA(" ok=%d dom=%08x share=%.17g", ok, dom, share));
        }
        { Image t = W; bool ok = cv::Dingo7Inpaint(t, cv::Mask(64 * 48, 255), 3); add(Fp("dingoAll", t) + FormatA(" ok=%d", ok)); }
        { Image t = W; cv::DiffusionInpaint(t, m); add(Fp("diffusion", t)); }
        { Image t = W; cv::DiffusionInpaint(t, m, 5); add(Fp("diffusion5", t)); }
        { Image t = A; cv::DiffusionInpaint(t, cv::RectMask(80, 50, { 0, 0, 80, 10 })); add(Fp("diffusionEdge", t)); }
        { Image t = W; cv::SmartFill(t, m); add(Fp("smartfill", t)); }
        { Image t(64, 48, 0xFFFFFFFF); t.FillRect({ 20, 20, 30, 30 }, 0xFF000000); cv::SmartFill(t, cv::RectMask(64, 48, { 18, 18, 32, 32 })); add(Fp("smartfillFlat", t)); }
        for (auto* im : { &P, &A, &tiny }) add(FormatA("autotol %.9g", cv::SmartMoveAutoTolerance(*im)));
        add(FormatA("autotol0 %.9g", cv::SmartMoveAutoTolerance(Image())));
        add(FormatA("tolscale %.9g %.9g", cv::SmartMoveToleranceScaling(12, 2, 75, 0, 100), cv::SmartMoveToleranceScaling(5, 3, 3, 0, 1)));
        // translations: shifted parts of a larger image
        Image big = Pattern(700, 900, 11);
        for (int y = 0; y < big.h; y += 37) for (int x = 20; x < 600; x++) big.at(x, y) = 0xFF000000;
        for (auto [dx, dy, w, h] : { std::tuple{ 0, 40, 300, 400 }, { 0, -90, 640, 500 }, { 13, 7, 200, 160 }, { 0, 0, 120, 90 }, { 0, 300, 600, 560 } }) {
            int ax = std::max(0, -dx) + 10, ay = std::max(0, -dy) + 100;
            Image a = big.Crop({ ax, ay, ax + w, ay + h }), bb = big.Crop({ ax + dx, ay + dy, ax + dx + w, ay + dy + h });
            double conf = 0;
            POINT d = cv::DetectTranslation(a, bb, &conf);
            add(FormatA("translation %d,%d %dx%d -> %d,%d conf %.17g", dx, dy, w, h, d.x, d.y, conf));
        }
        POINT none = cv::DetectTranslation(P, A);
        add(FormatA("translation mismatch %d,%d", none.x, none.y));
    }
    // long images across pages, and the PDF writer
    {
        Image page(300, 2600, 0xFFFFFFFF);
        for (int line = 0; line < 120; line++) {
            int y0 = 12 + line * 21 + (line % 7);
            for (int y = y0; y < y0 + 11 && y < page.h; y++)
                for (int x = 10; x < 280 - (line % 5) * 30; x++) if ((x * 7 + y * 3 + line) % 5 < 3) page.at(x, y) = 0xFF202020;
        }
        auto list = [](const std::vector<int>& v) { std::string s; for (int c : v) s += FormatA(" %d", c); return s; };
        for (int ph : { 400, 777, 3000, 10 }) add(FormatA("breaks %d:", ph) + list(ed::SmartBreaks(page, ph)));
        add("breaks s40:" + list(ed::SmartBreaks(page, 500, 40)));
        for (int parts : { 1, 2, 5, 9 }) add(FormatA("cuts %d:", parts) + list(ed::SmartCuts(page, parts)));
        int k = 0;
        for (auto& part : ed::SplitImage(page, { 900, 100, 5000, -4, 1300 }, false)) add(Fp(FormatA("split%d", k++).c_str(), part));
        for (auto& part : ed::SplitImage(P, { 30, 60 }, true)) add(Fp(FormatA("splitV%d", k++).c_str(), part));
        auto pdf = [&](const char* name, const Image& img, const std::vector<ed::PdfLink>& links) {
            std::wstring f = tmpDir + L"\\fingerprint (a).pdf", err;
            bool ok = ed::SavePdf(f, img, err, links);
            std::vector<char> d;
            if (ok) { std::ifstream in(f, std::ios::binary); d.assign(std::istreambuf_iterator<char>(in), {}); }
            DeleteFileW(f.c_str());
            add(FormatA("pdf %s ok=%d %zu %016llx err=%s", name, ok, d.size(), (unsigned long long)Fnv1a(d.data(), d.size()), WToUtf8(err).c_str()));
        };
        std::vector<ed::PdfLink> links = { { Gdiplus::RectF(10, 20, 80, 15), L"https://example.com/a(b)?q=ação" },
                                           { Gdiplus::RectF(5, 1290, 100, 30), L"mailto:x@y.z" },
                                           { Gdiplus::RectF(5, 5, 10, 10), L"file:///C:/x" },
                                           { Gdiplus::RectF(0, 2590, 300, 40), L"http://edge" } };
        pdf("page", page, links);
        pdf("P", P, {});
        pdf("wide", Pattern(400, 100, 2), { { Gdiplus::RectF(0, 0, 400, 100), L"https://w" } });
        pdf("A", A, {});
        pdf("empty", Image(), {});
    }
    return L;
}

// an object read from its XML element, and an object written and read back (through the Editor's clipboard format,
// which holds the same elements as the .snap)
static std::shared_ptr<snap::ed::DrawObj> ReadObjXml(const XmlNode& n) {
    auto v = snap::ed::ObjectsFromClip("<SnapnabObjects><Objects>" + XmlSerialize(n, false) + "</Objects></SnapnabObjects>");
    return v.empty() ? nullptr : std::make_shared<snap::ed::DrawObj>(v[0]);
}
static std::shared_ptr<snap::ed::DrawObj> ObjRoundTrip(const snap::ed::DrawObj& o) {
    auto v = snap::ed::ObjectsFromClip(snap::ed::ObjectsToClip({ o }, L""));
    return v.empty() ? nullptr : std::make_shared<snap::ed::DrawObj>(v[0]);
}

static std::wstring Tmp(const wchar_t* name) {
    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t);
    return std::wstring(t) + L"snapnab_test_" + name;
}

// The drawing of every object type and style variation on a generated image, with the geometry the editor asks the
// painters for (bounds, text and label boxes, shape outlines, stamps). tests/data/render-cpp.txt holds what the C++
// painters gave before they moved to Rust ("SnapnabTests rendergolden" prints those of the current build).
static std::vector<std::string> RenderFingerprints() {
    using namespace snap::ed;
    using Gdiplus::PointF;
    using Gdiplus::RectF;
    std::vector<std::string> L;
    Image base(200, 150, 0);
    for (int y = 0; y < base.h; y++)
        for (int x = 0; x < base.w; x++)
            base.at(x, y) = ARGB(255, (x * 5 + y) & 255, (y * 7) & 255, ((x / 10 + y / 10) % 2) ? 230 : 40);
    auto hashImg = [](const Image& im) { return (unsigned long long)Fnv1a(im.px.data(), im.px.size() * 4); };
    auto box = [](RectF r) { return FormatA("%.3f,%.3f,%.3f,%.3f", r.X, r.Y, r.Width, r.Height); };
    auto render = [&](const std::string& name, const std::vector<DrawObj>& objs, bool hideFirst = false) {
        Document d;
        d.SetBase(base);
        for (auto& o : objs) d.st.objs.push_back(std::make_shared<DrawObj>(o));
        Image out = d.Render(hideFirst ? d.st.objs[0].get() : nullptr);
        L.push_back(FormatA("%s %016llx b=%s", name.c_str(), hashImg(out), box(objs[0].Bounds()).c_str()));
    };
    auto add = [&](const std::string& name, const DrawObj& o) { render(name, { o }); };
    auto obj = [](ObjType t, float x1, float y1, float x2, float y2) {
        DrawObj o;
        o.type = t;
        o.p1 = { x1, y1 };
        o.p2 = { x2, y2 };
        o.ctrl = { (x1 + x2) / 2, (y1 + y2) / 2 - 40 };
        return o;
    };
    auto pathFp = [](Gdiplus::GraphicsPath& gp) {
        Gdiplus::PathData pd;
        gp.GetPathData(&pd);
        uint64_t h = Fnv1a(pd.Points, pd.Count * sizeof(PointF)) * 31 + Fnv1a(pd.Types, pd.Count);
        return FormatA("%d %016llx", pd.Count, (unsigned long long)h);
    };
    std::vector<PointF> zig;
    for (int i = 0; i < 24; i++) zig.push_back({ 20.0f + i * 7, 75.0f + (float)std::sin(i * 0.7) * 40 });

    // lines and arrows: every pair of ends, dashes, curves, hand-drawn strokes and labels
    for (int a = 0; a < CapCount; a++)
        for (int b = 0; b < CapCount; b++) {
            DrawObj o = obj(ObjType::Arrow, 30, 110, 170, 40);
            o.style.arrowStart = a; o.style.arrowEnd = b;
            o.style.startSize = 1.0f + a % 4; o.style.endSize = 2.0f + b % 3; o.style.width = 2.0f + (a + b) % 6;
            o.style.shadow = (a + b) % 2 == 0; o.style.shadowDir = (a * 3 + b) % 9;
            o.style.bezier = (a + b) % 3 == 0;
            add(FormatA("arrow %d %d", a, b), o);
        }
    for (int d = 0; d < DashCount; d++)
        for (int c = 0; c < 2; c++) {
            DrawObj o = obj(ObjType::Line, 20, 20, 180, 130);
            o.style.dash = d; o.style.width = 5; o.style.bezier = c == 1; o.style.arrowStart = c ? CapRound : CapFlat; o.style.arrowEnd = CapFlat;
            o.style.stroke = 0xFF2060E0; o.style.opacity = 80 - d * 5;
            add(FormatA("line dash %d %d", d, c), o);
        }
    for (int c = 0; c < 4; c++) {
        DrawObj o = obj(ObjType::Arrow, 25, 120, 175, 30);
        o.style.sketch = true; o.style.sketchSeed = 1 + c * 6; o.style.bezier = c % 2 == 1; o.style.width = 3.0f + c * 2;
        o.style.arrowStart = c == 3 ? CapArrowOpen : CapFlat;
        add(FormatA("arrow sketch %d", c), o);
    }
    {
        DrawObj o = obj(ObjType::Arrow, 100, 75, 104, 78);
        add("arrow short", o);
        o.p2 = { 100.2f, 75.1f };
        add("arrow tiny", o);
    }
    const wchar_t* labels[] = { L"Look here", L"Two\nlines", L"A much longer label that wraps around", L"Ends\n" };
    for (int i = 0; i < 4; i++)
        for (int c = 0; c < 2; c++) {
            DrawObj o = obj(ObjType::Arrow, 120, 60 + i * 10.0f, 40, 120);
            o.text = labels[i]; o.style.bezier = c == 1; o.style.fontSize = 14.0f + i * 3; o.style.arrowStart = i % 2 ? CapArrow : CapFlat;
            o.style.italic = i == 2; o.style.underline = i == 3; o.style.bold = i != 1;
            add(FormatA("arrow label %d %d", i, c), o);
            Gdiplus::RectF lr = ArrowLabelRect(o);
            L.push_back(FormatA("arrow label rect %d %d %s", i, c, box(lr).c_str()));
        }
    for (int i = 0; i < 4; i++) {
        DrawObj o = obj(i < 2 ? ObjType::Line : ObjType::Arrow, i % 2 ? 180.0f : 20.0f, 100, i % 2 ? 20.0f : 180.0f, 60);
        o.text = L"Text along a path"; o.style.textOnPath = true; o.style.bezier = i >= 2; o.style.fontSize = 16; o.style.sketch = i == 3;
        add(FormatA("text on path %d", i), o);
    }

    // shapes: every outline, fills, gradients, dashes and hand-drawn outlines
    for (int k = 0; k < ShapeCount; k++)
        for (int v = 0; v < 3; v++) {
            DrawObj o = obj(ObjType::Shape, 30, 25, 165 - v * 20.0f, 125);
            o.style.shapeKind = k; o.style.width = v == 2 ? 0.0f : 3.0f + v * 3; o.style.fill = v ? 0x9020C0F0 : 0;
            o.style.dash = (k + v) % DashCount; o.style.sketch = v == 1 && k % 2 == 0; o.style.sketchSeed = k + 2;
            add(FormatA("shape %d %d", k, v), o);
        }
    {
        ObjType old[] = { ObjType::Rect, ObjType::Ellipse, ObjType::RoundRect };
        for (int i = 0; i < 3; i++) { DrawObj o = obj(old[i], 160, 120, 40, 30); o.style.fill = 0xFFFFFFFF; add(FormatA("old shape %d", i), o); }
    }
    for (int g = 0; g < 6; g++) {
        DrawObj o = obj(ObjType::Shape, 20, 20, 180, 130);
        o.style.shapeKind = g % 3 == 2 ? ShapeEllipse : ShapeRect;
        o.style.grad.type = g < 4 ? 1 : 2;
        o.style.grad.angle = g * 37.0f;
        o.style.grad.stops = { { 0.0f, 0xFFFF0000 }, { 1.0f, 0x800000FF } };
        if (g % 2) o.style.grad.stops = { { 0.2f, 0xFF00FF00 }, { 0.5f, 0xFFFFFF00 }, { 0.7f, 0x40FF00FF }, { 0.9f, 0xFF000000 } };
        o.style.opacity = 100 - g * 7;
        add(FormatA("gradient %d", g), o);
        auto br = FillBrush(o.style.fill, o.style.grad, o.Rect(), 70 + g * 5);
        Image im(80, 60, 0);
        {
            auto bmp = im.WrapGdip();
            Gdiplus::Graphics gg(bmp.get());
            gg.FillRectangle(br.get(), 0, 0, 80, 60);
        }
        L.push_back(FormatA("fill brush %d %016llx", g, hashImg(im)));
        std::vector<uint8_t> mask(base.px.size());
        for (size_t i = 0; i < mask.size(); i++) mask[i] = (uint8_t)(i * 7 % 256);
        Image fm = base;
        FillGradientMask(fm, g % 2 ? mask : std::vector<uint8_t>{}, { 10, 5, 190, 140 }, o.style.grad, 90);
        L.push_back(FormatA("gradient mask %d %016llx", g, hashImg(fm)));
    }

    // pen strokes
    {
        DrawObj o = obj(ObjType::Pen, 0, 0, 0, 0);
        o.pts = zig; o.style.width = 6; o.style.stroke = 0xFF10A040;
        add("pen", o);
        o.style.dash = DashDot; add("pen dash", o);
        o.style.dash = DashSolid;
        for (size_t i = 0; i < o.pts.size(); i++) o.pressure.push_back((float)(i % 7) / 6);
        add("pen pressure", o);
        o.pressure.clear();
        o.pts = { { 100, 75 } }; o.style.width = 12;
        add("pen dot", o);
    }

    // highlighters, hides and spotlights: rectangles, every area outline, polygons, freehand, words only, old strokes
    ObjType areas[] = { ObjType::Highlighter, ObjType::Blur, ObjType::Pixelate, ObjType::Spotlight };
    std::vector<PointF> poly = { { 30, 30 }, { 170, 40 }, { 150, 130 }, { 60, 110 }, { 20, 70 } };
    for (int t = 0; t < 4; t++)
        for (int s = 0; s < AreaShapeCount + 1; s++) {
            DrawObj o = obj(areas[t], 35, 30, 165, 120);
            o.style.stroke = 0xFFFFE000; o.style.opacity = t ? 100 : 55; o.style.amount = 4 + s * 2; o.style.shadow = s % 2 == 1;
            if (s < AreaShapeCount) {
                o.style.areaShape = s;
                if (IsPointArea(s)) o.pts = poly;
            } else {
                o.style.textOnly = true;
                o.textBoxes = { { 40, 40, 50, 14 }, { 100, 42, 60, 12 }, { 40, 80, 90, 16 } };
            }
            add(FormatA("area %d %d", t, s), o);
        }
    {
        DrawObj o = obj(ObjType::Highlighter, 0, 0, 0, 0);
        o.pts = zig; o.style.width = 14; o.style.stroke = 0xFFFFE000; o.style.opacity = 70;
        add("highlighter freehand", o);
        o.pts = { { 80, 80 } };
        add("highlighter freehand dot", o);
    }

    // texts: alignments, line spacing, vertical, hand-drawn, outline and the old background box
    for (int h = 0; h < 3; h++)
        for (int v = 0; v < 3; v++) {
            DrawObj o = obj(ObjType::Text, 15, 10, 185, 140);
            o.text = L"Snapnab text\nsecond line"; o.style.alignH = h; o.style.alignV = v; o.style.fontSize = 18.0f + h * 4;
            o.style.textColor = 0xFF102030; o.style.width = (float)v; o.style.stroke = 0xFFFFFFFF;
            add(FormatA("text %d %d", h, v), o);
        }
    const wchar_t* texts[] = { L"Spacing\nbetween\nlines", L"Ends with enter\n\n", L"Supercalifragilisticexpialidocious word", L"" };
    for (int i = 0; i < 4; i++)
        for (int v = 0; v < 3; v++) {
            DrawObj o = obj(ObjType::Text, 20, 15, 180, 135);
            o.text = texts[i]; o.style.lineSpacing = 0.8f + v * 0.9f; o.style.alignV = v; o.style.alignH = 2 - v; o.style.fontSize = 17;
            o.style.font = v == 1 ? L"Georgia" : v == 2 ? L"No Such Font Here" : L"Segoe UI";
            add(FormatA("text spacing %d %d", i, v), o);
            for (float w : { 30.0f, 90.0f, 170.0f }) L.push_back(FormatA("text height %d %d %.0f %.4f", i, v, w, TextBlockHeight(o, w)));
        }
    {
        DrawObj o = obj(ObjType::Text, 20, 10, 180, 140);
        o.text = L"Vertical text"; o.style.vertical = true; o.style.fontSize = 22;
        add("text vertical", o);
        L.push_back(FormatA("text vertical height %.4f", TextBlockHeight(o, 120)));
        o.style.vertical = false; o.style.sketch = true; o.style.italic = true; o.style.underline = true;
        add("text sketch", o);
        o.style.sketch = false; o.style.fill = 0xC0FFFFFF; o.style.padding = 12;
        add("text box", o);
        o.style.opacity = 40; o.style.shadow = false;
        add("text faded", o);
    }

    // callouts: the 24 shapes, tips everywhere, shaded, frosted glass, leaders, text boxes
    for (int c = 0; c < kCalloutShapes; c++)
        for (int t = 0; t < 4; t++) {
            DrawObj o = obj(ObjType::Callout, 50, 40, 150, 100);
            PointF tips[] = { { 190, 140 }, { 100, 70 }, { 10, 70 }, { 100, 5 } };
            o.tail = tips[t]; o.text = L"Callout text here"; o.style.calloutShape = c; o.style.fontSize = 13;
            o.style.fill = 0xFFFFFFF0; o.style.stroke = 0xFF303030; o.style.width = t == 3 ? 0.0f : 2.0f + t;
            o.style.shaded = t == 1; o.style.arrowEnd = t; o.style.tailWidth = 3.0f + t;
            add(FormatA("callout %d %d", c, t), o);
            L.push_back(FormatA("callout text rect %d %d %s", c, t, box(CalloutTextRect(o)).c_str()));
        }
    {
        DrawObj o = obj(ObjType::Callout, 40, 30, 160, 110);
        o.tail = { 190, 140 }; o.text = L"Frosted"; o.style.bgBlur = true; o.style.bgBlurAmount = 6; o.style.fill = 0x80FFFFFF;
        add("callout frosted", o);
        o.style.vertical = true; o.style.bgBlur = false; o.style.lineSpacing = 1.4f;
        add("callout vertical", o);
        render("callout hidden text", { o }, true);
    }
    for (int c = 0; c < kCalloutShapes; c++) {
        Gdiplus::GraphicsPath gp;
        CalloutBodyPath(gp, c, RectF(10, 20, 120, 70));
        L.push_back(FormatA("callout body %d %s", c, pathFp(gp).c_str()));
    }
    for (int k = 0; k < ShapeCount; k++) {
        Gdiplus::GraphicsPath gp;
        ShapePath(gp, k, RectF(5, 10, 90, 60));
        L.push_back(FormatA("shape path %d %s", k, pathFp(gp).c_str()));
    }
    for (int s = 0; s < AreaShapeCount; s++) {
        DrawObj o = obj(ObjType::Spotlight, 10, 10, 90, 70);
        o.style.areaShape = s;
        if (IsPointArea(s)) o.pts = poly;
        Gdiplus::GraphicsPath gp;
        AreaPath(gp, o);
        L.push_back(FormatA("area path %d %s", s, pathFp(gp).c_str()));
    }

    // steps: shapes, number styles, outlines
    for (int s = 0; s < 4; s++)
        for (int t = 0; t < StepTypeCount; t++) {
            DrawObj o = obj(ObjType::Step, 100, 75, 100, 75);
            int vals[] = { 1, 12, 123, 27, 3999 };
            o.stepValue = vals[t]; o.style.stepShape = s; o.style.stepType = t; o.style.fontSize = 20.0f + s * 6;
            o.style.textColor = 0xFFFFFFFF; o.style.stepOutlineWidth = t % 2 ? 3.0f : 0.0f;
            add(FormatA("step %d %d", s, t), o);
        }
    for (int v : { 1, 26, 27, 52, 702, 703, 1994, 3999, 5000 })
        for (int t = 0; t < StepTypeCount; t++) L.push_back(FormatA("step label %d %d %s", v, t, WToUtf8(StepLabel(v, t)).c_str()));
    for (float w : { 1.0f, 4.0f, 9.0f }) for (float s : { 0.5f, 3.0f, 6.0f }) L.push_back(FormatA("cap length %.1f %.1f %.4f", w, s, CapLength(w, s)));

    // stamps: emoji, labels (Latin, CJK, Devanagari) and every cursor
    std::vector<std::wstring> stamps = { std::wstring{ (wchar_t)0xD83D, (wchar_t)0xDE00 }, std::wstring(1, (wchar_t)0x2705),
                                         L"label:2E7D32:Before", L"label:C62828:Wrong", std::wstring(L"label:1565C0:") + (wchar_t)0x4E2D + (wchar_t)0x6587,
                                         std::wstring(L"label:6A1B9A:") + (wchar_t)0x0939 + (wchar_t)0x093F, L"label:bad" };
    for (auto& c : { L"arrow", L"arrowdark", L"hand", L"ibeam", L"wait", L"cross", L"move", L"resize", L"click", L"clickright" })
        stamps.push_back(std::wstring(L"cursor:") + c);
    for (size_t i = 0; i < stamps.size(); i++) {
        DrawObj o = obj(ObjType::Stamp, 40, 20, 160, 130);
        o.text = stamps[i]; o.style.shadow = i % 2 == 0;
        add(FormatA("stamp %zu", i), o);
        Image si = StampImage(stamps[i], 70);
        L.push_back(FormatA("stamp image %zu %dx%d %016llx aspect %.4f", i, si.w, si.h, hashImg(si), StampAspect(stamps[i])));
    }

    // magnifiers: shapes, connected, loupes
    for (int m = 0; m < MagnifyKindCount; m++)
        for (int v = 0; v < 4; v++) {
            DrawObj o = obj(ObjType::Magnify, 100, 50, 180, 130);
            o.tail = { 50, 50 }; o.style.magnifyShape = m; o.style.magnifyScale = 150 + v * 100; o.style.magnifyLink = v % 2 == 1;
            o.style.loupe = v >= 2; o.style.loupeHandle = v == 2; o.style.width = 2.0f + v; o.style.stroke = 0xFF404040 + v * 0x200000;
            add(FormatA("magnify %d %d", m, v), o);
            L.push_back(FormatA("magnifier extra %d %d %s", m, v, box(MagnifierExtra(o)).c_str()));
        }

    // embedded pictures, groups, several objects together
    {
        auto pic = std::make_shared<Image>(30, 20, 0);
        for (int y = 0; y < 20; y++) for (int x = 0; x < 30; x++) pic->at(x, y) = ARGB(200 + y, x * 8, y * 12, 128);
        DrawObj o = obj(ObjType::Picture, 30, 30, 150, 110);
        o.picture = pic; o.style.width = 0;
        add("picture", o);
        o.style.width = 3; o.style.stroke = 0xFF000000; o.style.opacity = 70;
        add("picture framed", o);
        DrawObj g = obj(ObjType::Group, 0, 0, 0, 0);
        g.children = { obj(ObjType::Arrow, 20, 20, 120, 100), obj(ObjType::Shape, 90, 40, 180, 140) };
        g.children[1].style.fill = 0x8000FF00;
        g.UpdateGroupRect();
        add("group", g);
        DrawObj b = obj(ObjType::Blur, 0, 0, 200, 70);
        b.style.amount = 8;
        render("stack", { g.children[0], b, g.children[1], o });
    }

    // the line end samples of the menus
    for (int c = 0; c < CapCount; c++)
        for (int s = 0; s < 2; s++) {
            Image im(60, 24, 0xFFFFFFFF);
            {
                auto bmp = im.WrapGdip();
                Gdiplus::Graphics gg(bmp.get());
                gg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                DrawCapGlyph(gg, c, RectF(0, 0, 60, 24), Gdiplus::Color(255, 20, 20, 20), s == 1);
            }
            L.push_back(FormatA("cap glyph %d %d %016llx", c, s, hashImg(im)));
        }
    return L;
}

// The editor's objects as the .snap and the clipboard store them (XML), read back, from hand-written and older files,
// with hit-testing on a grid and a document saved and loaded. tests/data/doc-cpp.txt holds what the C++ code gave
// before it moved to Rust ("SnapnabTests docgolden" prints those of the current build).
static std::vector<std::string> DocFingerprints() {
    using namespace snap::ed;
    using Gdiplus::PointF;
    using Gdiplus::RectF;
    std::vector<std::string> L;
    auto oneLine = [](std::string s) {
        std::string o;
        for (char c : s) o += c == '\n' ? std::string("\\n") : c == '\r' ? std::string("\\r") : std::string(1, c);
        return o;
    };
    // an object's XML as the clipboard (and the .snap) store it; a picture's PNG by its hash
    auto xmlOf = [&](const DrawObj& o) {
        std::string s = ObjectsToClip({ o }, L"");
        size_t a = s.find("<Objects>"), b = s.find("</Objects>");
        std::string x = a != std::string::npos && b != std::string::npos ? s.substr(a + 9, b - a - 9) : s;
        if (size_t e = s.find("<Embedded"); e != std::string::npos)
            x += FormatA(" [%016llx]", (unsigned long long)Fnv1a(s.data() + e, s.size() - e));
        return oneLine(x);
    };
    auto readXml = [](const std::string& objXml) {
        auto v = ObjectsFromClip("<SnapnabObjects Source=\"\"><Objects>" + objXml + "</Objects></SnapnabObjects>");
        return v.empty() ? ObjPtr() : std::make_shared<DrawObj>(v[0]);
    };
    auto extra = [](const DrawObj& o) {
        return FormatA("t=%d hk=%d ms=%d sv=%d aa=%d ctrl=%.3f,%.3f tail=%.3f,%.3f p=%.3f,%.3f,%.3f,%.3f kids=%zu pic=%d",
                       (int)o.type, o.style.hideKind, o.style.magnifyScale, o.stepValue, o.style.antialias, o.ctrl.X, o.ctrl.Y,
                       o.tail.X, o.tail.Y, o.p1.X, o.p1.Y, o.p2.X, o.p2.Y, o.children.size(),
                       o.picture ? o.picture->w * 1000 + o.picture->h : -1);
    };
    auto obj = [](ObjType t, float x1, float y1, float x2, float y2) {
        DrawObj o;
        o.type = t;
        o.p1 = { x1, y1 };
        o.p2 = { x2, y2 };
        o.ctrl = { (x1 + x2) / 2, (y1 + y2) / 2 - 40 };
        return o;
    };

    // a set of objects that touches every attribute
    std::vector<std::pair<std::string, DrawObj>> S;
    {
        DrawObj o = obj(ObjType::Arrow, 30, 110, 170, 40);
        o.style.bezier = true; o.style.dash = 3; o.style.arrowStart = CapDiamond; o.style.startSize = 2.5f; o.style.width = 6;
        o.text = L"Label <&> \"q\" 'a'\nnext\tline"; o.style.font = L"Segoe UI"; o.style.italic = true; o.style.fontSize = 13.25f;
        S.push_back({ "arrow label", o });
        DrawObj l = obj(ObjType::Line, -12.5f, 0.00005f, 1e7f, -0.00015f);
        l.style.stroke = 0x80FF0000; l.style.opacity = 35; l.style.shadow = false; l.name = L"Línea ✓"; l.locked = true;
        S.push_back({ "line", l });
    }
    for (int k = 0; k < ShapeCount; k++) {
        DrawObj o = obj(ObjType::Shape, 30, 25, 165, 125);
        o.style.shapeKind = k; o.style.fill = k % 2 ? 0x9020C0F0 : 0; o.style.width = 1.0f + k;
        if (k == 2) o.style.grad = { 1, 37.5f, { { 0, 0xFF112233 }, { 0.4f, 0x80445566 }, { 1, 0xFFFFFFFF } } };
        if (k == 5) o.style.grad = { 2, 90, { { 0.125f, 0xFF000000 }, { 0.875f, 0x00FFFFFF } } };
        if (k == 3) o.style.sketch = true, o.style.sketchSeed = 42;
        S.push_back({ FormatA("shape %d", k), o });
    }
    S.push_back({ "old rect", obj(ObjType::Rect, 160, 120, 40, 30) });
    S.push_back({ "old ellipse", obj(ObjType::Ellipse, 10, 10, 90, 60) });
    S.push_back({ "old roundrect", obj(ObjType::RoundRect, 50, 50, 150, 100) });
    {
        DrawObj o = obj(ObjType::Pen, 0, 0, 0, 0);
        for (int i = 0; i < 12; i++) { o.pts.push_back({ 20.0f + i * 13.3f, 75.0f + (float)std::sin(i * 0.7) * 40 }); o.pressure.push_back(i / 11.0f); }
        o.p1 = o.pts.front(); o.p2 = o.pts.back();
        o.style.arrowEnd = CapArrow;
        S.push_back({ "pen pressure", o });
        o.pressure.pop_back();
        S.push_back({ "pen pressure mismatch", o });
    }
    {
        DrawObj o = obj(ObjType::Highlighter, 20, 30, 120, 60);
        o.style.stroke = 0x80FFFF00;
        S.push_back({ "highlighter", o });
        o.style.areaShape = AreaPolygon;
        o.pts = { { 20, 30 }, { 120, 40 }, { 90, 90 }, { 25, 70 } };
        S.push_back({ "highlighter polygon", o });
    }
    {
        DrawObj o = obj(ObjType::Text, 20, 20, 180, 120);
        o.text = L"Héllo wörld — 日本語 😀\r\nline two"; o.name = L"Title"; o.hidden = true; o.floating = true;
        o.style.lineSpacing = 1.35f; o.style.alignH = 2; o.style.alignV = 1; o.style.padding = 7.5f; o.style.underline = true;
        o.style.textOnly = true; o.textBoxes = { RectF(20, 20, 50.25f, 12), RectF(30.5f, 40, 60, 14.75f) };
        o.style.snapText = true; o.style.vertical = true; o.style.textOnPath = true;
        S.push_back({ "text", o });
        DrawObj e = obj(ObjType::Text, 5, 5, 6, 6);
        S.push_back({ "text empty", e });
    }
    for (int c : { 0, 13, 16, 22, 23 }) {
        DrawObj o = obj(ObjType::Callout, 40, 30, 160, 90);
        o.text = L"Callout"; o.tail = { 20, 130 }; o.style.calloutShape = c; o.style.shaded = c == 13;
        o.style.tailColor = 0xFF00AA00; o.style.tailWidth = 4.5f; o.style.bgBlur = c == 22; o.style.bgBlurAmount = 12;
        o.style.grad = { 1, 0, { { 0, 0xFF000000 }, { 1, 0xFFFFFFFF } } };
        S.push_back({ FormatA("callout %d", c), o });
    }
    for (int i = 0; i < 3; i++) {
        DrawObj o = obj(ObjType::Step, 60, 60, 92, 92);
        o.stepValue = 3 + i * 10; o.style.stepShape = i; o.style.stepType = i + 1;
        if (i == 1) { o.style.stepOutline = 0xFF123456; o.style.stepOutlineWidth = 2.5f; }
        S.push_back({ FormatA("step %d", i), o });
    }
    {
        DrawObj o = obj(ObjType::Stamp, 50, 50, 98, 98);
        o.text = L"label:FF8800:Before";
        S.push_back({ "stamp", o });
    }
    {
        ObjType hide[] = { ObjType::Blur, ObjType::Pixelate, ObjType::Spotlight };
        for (int i = 0; i < 3; i++) {
            DrawObj o = obj(hide[i], 30, 30, 150, 110);
            o.style.amount = 4 + i * 7; o.style.areaShape = i == 0 ? ShapeEllipse : i == 1 ? AreaFreehand : ShapeRect;
            if (i == 1) { o.pts = { { 30, 30 }, { 150, 40 }, { 140, 110 }, { 40, 100 }, { 35, 60 } }; o.style.hideKind = 1; }
            S.push_back({ FormatA("hide %d", i), o });
        }
    }
    {
        DrawObj o = obj(ObjType::Magnify, 100, 20, 180, 100);
        o.tail = { 40, 120 }; o.style.magnifyShape = 1; o.style.magnifyScale = 250; o.style.magnifyLink = true; o.style.amount = 6;
        o.style.loupe = true; o.style.loupeHandle = false;
        S.push_back({ "magnify", o });
        o.style.loupe = false;
        S.push_back({ "magnify plain", o });
    }
    {
        DrawObj o = obj(ObjType::Picture, 10, 10, 40, 30);
        Image pic(3, 2, 0);
        for (int i = 0; i < 6; i++) pic.px[i] = 0xFF000000u | (uint32_t)(i * 0x2A1B0C);
        o.picture = std::make_shared<Image>(pic);
        S.push_back({ "picture", o });
    }
    {
        DrawObj g = obj(ObjType::Group, 0, 0, 0, 0);
        g.children.push_back(S[0].second);
        g.children.push_back(S[14].second);
        DrawObj inner = obj(ObjType::Group, 0, 0, 0, 0);
        inner.children.push_back(obj(ObjType::Line, 5, 5, 25, 45));
        inner.UpdateGroupRect();
        g.children.push_back(inner);
        g.UpdateGroupRect();
        g.name = L"Group 1";
        S.push_back({ "group", g });
    }
    {
        DrawObj o = obj(ObjType::Link, 10, 10, 90, 30);
        o.text = L"https://example.com/a?b=1&c=é";
        S.push_back({ "link", o });
    }

    for (auto& [name, o] : S) L.push_back(FormatA("xml %s %s", name.c_str(), xmlOf(o).c_str()));

    // read back: the XML of what was read, and what the XML does not show
    for (auto& [name, o] : S) {
        auto r = ObjectsFromClip(ObjectsToClip({ o }, L""));
        L.push_back(FormatA("read %s %zu", name.c_str(), r.size()));
        if (r.empty()) continue;
        auto& rr = r[0];
        L.push_back(FormatA("read %s %s %s", name.c_str(), extra(rr).c_str(), xmlOf(rr).c_str()));
    }

    // hand-written and older files
    const char* hand[] = {
        "<Rect X1=\"10\" Y1=\"20\" X2=\"5\" Y2=\"7\"/>",
        "<Ellipse X1=\"1\" Y1=\"2\" X2=\"30\" Y2=\"40\" PenColor=\"#123456\"/>",
        "<RoundRect/>",
        "<Highlight X1=\"1\" Y1=\"1\" X2=\"50\" Y2=\"9\" AreaShape=\"99\"/>",
        "<Callout CalloutShape=\"2\"/>",
        "<Callout CalloutShape=\"9\"/>",
        "<Callout CalloutShape=\"-1\" CalloutKind=\"30\"/>",
        "<Callout CalloutKind=\"17\" TailX=\"3.5\" TailY=\"-4\"/>",
        "<Pixelate Amount=\"5\"/>",
        "<Link X1=\"0\" Url=\"\"/>",
        "<Link X1=\"4\" Y1=\"5\" X2=\"6\" Y2=\"7\" Url=\"http://x\"/>",
        "<Group/>",
        "<Group><Bogus/></Group>",
        "<Group Name=\"g\"><Arrow X1=\"1\" Y1=\"2\" X2=\"30\" Y2=\"40\"/><Text X1=\"0\" Y1=\"0\" X2=\"10\" Y2=\"10\" Text=\"t\"/></Group>",
        "<Bogus X1=\"1\"/>",
        "<Image StreamName=\"missing.png\"/>",
        "<Image/>",
        "<Arrow PenColor=\"#1\" FillColor=\"zz\" TextColor=\"#0x7FABCDEF\" ShadowColor=\"#FFFFFFFFFF\" TailColor=\"#-1\"/>",
        "<Arrow PenColor=\"#abcdeg\" FillColor=\"#\" TextColor=\"# 12\" ShadowColor=\"#+0000FF\"/>",
        "<Pen Points=\"1,2 3,4 junk 5,6\" Pressure=\"0.5 1.5 -1 x 0.25\"/>",
        "<Pen Points=\" 1,2\" Pressure=\" 0.5\"/>",
        "<Pen Points=\"1 ,2 3,4\"/>",
        "<Pen Points=\"1, 2   3,\t4\"/>",
        "<Pen Points=\"1,2\"/>",
        "<Pen Points=\"0x10,2 1e2,-3.5e-1 .5,5. +7,-0 \"/>",
        "<Pen Points=\"1.25e,3 4,5\"/>",
        "<Pen Points=\"inf,1 2,3\"/>",
        "<Text TextBoxes=\"1,2,3,4 5,6,7 8\" LineSpacing=\"9\" AlignH=\"-3\" AlignV=\"7\" Bold=\"0\" TextOnly=\"1\"/>",
        "<Text TextBoxes=\"1,2,3,4 5,6,7,8\" LineSpacing=\"0.1\"/>",
        "<Shape FillGradient=\"1\" GradientAngle=\"45.5\" GradientStops=\"1:#FF0000FF;0:#00FF00;0.5:bad;2:#FFFFFFFF;x\" ToolShape=\"99\"/>",
        "<Shape FillGradient=\"2\" GradientStops=\"0:#FF000000\"/>",
        "<Shape FillGradient=\"7\" GradientStops=\";;0.3:#FF0000FF;-1:#FF00FF00;\"/>",
        "<Shape FillGradient=\"-2\" GradientAngle=\"abc\" GradientStops=\"0.25:#FF0000FF;0.25:#FF00FF00;0.1:\"/>",
        "<Magnify Amount=\"3\"/>",
        "<Magnify Amount=\"40\" MagnifyShape=\"12\" LoupeHandle=\"0\"/>",
        "<Magnify MagnifyScale=\"5000\"/>",
        "<Step StepValue=\"-4\" StepStyle=\"7\" StepSequenceType=\"-1\" StepOutlineWidth=\"99\"/>",
        "<Arrow X1=\" 12.5abc\" Y1=\"+7x\" X2=\"1e2\" Y2=\".5\" PenWidth=\"-.e3\" Opacity=\" -12 \" ShadowDir=\"12\" Shadow=\"0\"/>",
        "<Arrow X1=\"0x1A\" Y1=\"inf\" X2=\"1e400\" Y2=\"-0\" PenWidth=\"0x1p3\" Opacity=\"99999999999\"/>",
        "<Arrow ArrowStartCap=\"20\" ArrowEndCap=\"-5\" DashType=\"9\" Bezier=\"2\" StartSize=\"3.25\"/>",
        "<Arrow X1=\"10\" Y1=\"20\" X2=\"30\" Y2=\"40\" Bezier=\"1\"/>",
        "<Text FaceName=\"Consolas\" FontSize=\"0\" Italic=\"1\" Underline=\"1\" TextPadding=\"-2\" Text=\"a&amp;b&#10;c\"/>",
        "<Callout BackgroundBlur=\"1\" BackgroundBlurAmount=\"99\" Shaded=\"1\"/>",
        "<Callout BackgroundBlur=\"1\" BackgroundBlurAmount=\"0\"/>",
        "<Blur AreaShape=\"9\" Points=\"1,1 50,1 25,40 \"/>",
        "<Spotlight Amount=\"-3\"/>",
        "<Stamp Text=\"cursor:arrow\" Hidden=\"1\" Locked=\"5\" Floating=\"0\" Name=\"s\"/>",
        "<Text Sketch=\"1\" SketchSeed=\"77\" VerticalText=\"1\" TextOnPath=\"1\" SnapText=\"1\"/>",
    };
    for (const char* h : hand) {
        ObjPtr r = readXml(h);
        L.push_back(r ? FormatA("hand %s -> %s %s", h, extra(*r).c_str(), xmlOf(*r).c_str()) : FormatA("hand %s -> null", h));
    }

    // gradient stops as the quick styles store them
    {
        Gradient gs[] = { {}, { 1, 0, { { 0, 0xFF000000 } } }, { 1, 0, { { 0.0005f, 0x01020304 }, { 0.9995f, 0xFFFFFFFF }, { 1, 0 } } },
                          { 2, -45, { { 0.3333333f, 0xFFABCDEF }, { 0.6666667f, 0x00000000 } } } };
        for (auto& g : gs) {
            XmlNode n("G");
            GradToXml(n, g);
            Gradient back = GradFromXml(n);
            L.push_back(FormatA("grad [%s] %s back=%d", GradStopsToString(g).c_str(), oneLine(XmlSerialize(n, false)).c_str(), back == g));
        }
    }

    // hit-testing on a grid, at two tolerances
    for (auto& [name, o] : S) {
        for (float tol : { 0.0f, 4.0f }) {
            std::string bits;
            int hits = 0;
            for (int y = -10; y < 160; y += 5)
                for (int x = -10; x < 210; x += 5) {
                    bool h = o.HitTest(PointF((float)x + 0.5f, (float)y + 0.25f), tol);
                    bits += h ? '1' : '0';
                    hits += h;
                }
            L.push_back(FormatA("hit %s %.0f %d %016llx", name.c_str(), tol, hits, (unsigned long long)Fnv1a(bits.data(), bits.size())));
        }
    }

    // the clipboard: written, read back; and texts that are not objects
    {
        std::vector<DrawObj> objs;
        for (auto& [name, o] : S) objs.push_back(o);
        std::string clip = ObjectsToClip(objs, L"C:\\Docs\\é 1.snap");
        L.push_back(FormatA("clip %zu %016llx", clip.size(), (unsigned long long)Fnv1a(clip.data(), clip.size())));
        std::wstring src;
        auto back = ObjectsFromClip(clip, &src);
        std::string all;
        for (auto& o : back) all += extra(o) + xmlOf(o) + "\n";
        L.push_back(FormatA("clip back %zu %s %016llx", back.size(), WToUtf8(src).c_str(), (unsigned long long)Fnv1a(all.data(), all.size())));
        const char* bad[] = { "", "<Other/>", "<SnapnabObjects><Objects><Arrow X1=\"1\"/><Nope/></Objects></SnapnabObjects>",
                              "<SnapnabObjects Source=\"s\"><Objects><Image StreamName=\"Image_1.png\"/></Objects>"
                              "<Embedded Name=\"Image_1.png\" Data=\"!!!\"/></SnapnabObjects>",
                              "<SnapnabObjects Source=\"x\">" };
        for (const char* b : bad) {
            std::wstring s = L"unset";
            auto r = ObjectsFromClip(b, &s);
            std::string x;
            for (auto& o : r) x += xmlOf(o) + ";";
            L.push_back(FormatA("clip bad %zu [%s] %s", r.size(), WToUtf8(s).c_str(), x.c_str()));
        }
    }

    // a document saved as .snap and read back
    {
        std::wstring file = Tmp(L"docgolden.snap");
        Document d;
        Image base(40, 30, 0);
        for (int i = 0; i < 40 * 30; i++) base.px[i] = 0xFF000000u | (uint32_t)(i * 2654435761u >> 8);
        d.SetBase(base);
        d.path = file;
        d.captureMode = L"Região"; d.windowTitle = L"Window — “title”"; d.application = L"app.exe"; d.created = L"2026-01-02T03:04:05";
        d.tags = L"a,b,ç"; d.st.nextStep = 7; d.guidesX = { 10.5f, 20 }; d.guidesY = { 0.125f };
        for (auto& [name, o] : S) d.st.objs.push_back(std::make_shared<DrawObj>(o));
        bool saved = d.Save();
        XmlNode doc;
        bool docRead = LoadSnapDocument(file, doc);
        std::string dx = XmlSerialize(doc, false);
        L.push_back(FormatA("doc saved %d %d %zu %016llx", saved, docRead, dx.size(), (unsigned long long)Fnv1a(dx.data(), dx.size())));
        auto r = Document::Load(file);
        if (r) {
            std::string all;
            for (auto& o : r->st.objs) all += extra(*o) + xmlOf(*o) + "\n";
            std::string g;
            for (float x : r->guidesX) g += FormatA("x%.3f ", x);
            for (float y : r->guidesY) g += FormatA("y%.3f ", y);
            L.push_back(FormatA("doc loaded %s|%s|%s|%s|%s|%d|%s|%dx%d %016llx|%zu %016llx", WToUtf8(r->captureMode).c_str(),
                                WToUtf8(r->windowTitle).c_str(), WToUtf8(r->application).c_str(), WToUtf8(r->created).c_str(),
                                WToUtf8(r->tags).c_str(), r->st.nextStep, g.c_str(), r->w(), r->h(),
                                (unsigned long long)Fnv1a(r->st.base->px.data(), r->st.base->px.size() * 4), r->st.objs.size(),
                                (unsigned long long)Fnv1a(all.data(), all.size())));
            L.push_back(FormatA("doc title %s", WToUtf8(r->title).c_str()));
        } else {
            L.push_back("doc loaded null");
        }
        Document e;
        e.SetBase(Image(4, 3, 0xFF00FF00));
        e.path = Tmp(L"docgolden2.snap");
        bool s2 = e.Save();
        auto r2 = Document::Load(e.path);
        L.push_back(FormatA("doc empty %d %s %d %zu %zu", s2, r2 ? WToUtf8(r2->title).c_str() : "null", r2 ? r2->st.nextStep : -1,
                            r2 ? r2->st.objs.size() : 0, r2 ? r2->guidesX.size() + r2->guidesY.size() : 9));
        DeleteFileW(file.c_str());
        DeleteFileW(e.path.c_str());
        L.push_back(FormatA("doc missing %d", Document::Load(Tmp(L"docgolden_missing.snap")) != nullptr));
    }
    return L;
}

// The capture presets as the C++ capture app writes and reads them (tests/data/presets-cpp.txt): the Rust capture app
// (rust/snapcore/src/presets.rs) must write the same Presets1.xml ("SnapnabTests presetgolden" prints them).
static std::vector<std::string> PresetFingerprints() {
    std::vector<std::string> L;
    auto put = [&](const std::string& label, const XmlNode& n) {
        L.push_back("== " + label);
        std::string s = XmlSerialize(n, false);
        size_t a = 0;
        for (size_t b; (b = s.find('\n', a)) != std::string::npos; a = b + 1) L.push_back(s.substr(a, b - a));
        if (a < s.size()) L.push_back(s.substr(a));
    };
    put("default", Preset().ToXml());
    Preset p;
    p.name = L"Região ç → 中文 😀 \"<&>\"";
    p.kind = CaptureKind::Video; p.selection = Selection::Monitor; p.fixedW = 1280; p.fixedH = 720;
    p.regionFixed = true; p.regionUsePos = true; p.regionX = -50; p.regionY = 30;
    p.captureCursor = true; p.delaySeconds = 5; p.windowObjects = true; p.captureLinks = false;
    p.previewInEditor = false; p.copyToClipboard = true; p.saveToFile = true; p.fileFormat = FileFormat::WEBP;
    p.filePrefix = L"Shot"; p.folder = L"C:\\Caps\\ü"; p.openInProgram = true; p.programPath = L"C:\\Tools\\x.exe";
    p.grabText = true; p.pinToScreen = true; p.readQr = true; p.office = 2; p.upload = true;
    auto& f = p.effects;
    f.border = true; f.borderWidth = 9; f.borderColor = RGB(1, 2, 3); f.filter = true; f.filterKind = 2;
    f.shadow = true; f.shadowOffset = 11; f.shadowBlur = 12; f.shadowOpacity = 77;
    f.caption = true; f.captionText = L"{date} ✓"; f.captionTop = true;
    f.replaceColor = true; f.replaceFrom = RGB(10, 20, 30); f.replaceTo = RGB(200, 100, 0); f.replaceTolerance = 33;
    f.scale = true; f.scalePercent = 150; f.edge = true; f.edgeStyle = 3; f.edgeSize = 20; f.edgeSides = 9;
    f.trim = true; f.trimTolerance = 4; f.adjust = true; f.brightness = -20; f.contrast = 15; f.hue = -90; f.saturation = 40;
    f.rotate = true; f.rotateKind = 4; f.watermark = true; f.watermarkText = L"© Me"; f.watermarkOpacity = 60; f.watermarkTiled = false;
    f.resolution = true; f.dpi = 600; f.beautify = true;
    f.beautifyOpt.background = 3; f.beautifyOpt.color1 = RGB(5, 6, 7); f.beautifyOpt.imagePath = L"C:\\bg.png";
    f.beautifyOpt.padding = 100; f.beautifyOpt.aspect = 2; f.beautifyOpt.autoBalance = false;
    p.microphone = false; p.systemAudio = false; p.micVolume = 150; p.sysVolume = 20; p.autoGain = false; p.fps = 60; p.quality = 3;
    p.downscale1080 = false; p.hardwareEncode = false; p.audioOnly = true; p.audioFormat = 1; p.webcam = true;
    p.webcamDevice = L"usb#vid&pid"; p.webcamName = L"Cam"; p.webcamShape = 3; p.chromaKey = true; p.chromaColor = RGB(0, 255, 0);
    p.chromaTolerance = 70; p.builtin = L"grabtext"; p.hotkeyMods = MOD_CONTROL | MOD_SHIFT; p.hotkeyVk = 'P';
    put("custom", p.ToXml());
    put("custom read", Preset::FromXml(p.ToXml()).ToXml());
    const char* reads[] = {
        "<CaptureSettings DisplayName=\"Old\" SelectionMode=\"4\" RegionWidth=\"300\"/>",
        "<CaptureSettings SelectionMode=\"99\" CaptureMode=\"Video\"><CaptureEffects Grayscale=\"1\" Scale=\"150\" ShadowOffset=\"-5\" "
        "EdgeSides=\"31\" Dpi=\"5\"/></CaptureSettings>",
        "<CaptureSettings SelectionMode=\"-3\" DelayDuration=\" 12x\" HotKey=\"abc\"><ShareSettingsFile Enabled=\"1\" OutputFileType=\"99\"/>"
        "<ShareSettingsOffice Target=\"7\"/><Video FrameRate=\"1000\" Quality=\"-1\" MicrophoneVolume=\"999\" WebcamShape=\"9\" "
        "AudioFormat=\"5\"/></CaptureSettings>",
        "<CaptureSettings><CaptureEffects Beautify=\"1\"><Beautify Background=\"9\" Angle=\"400\" Padding=\"-5\" Color1=\"#ZZ\" "
        "Color2=\"#00FF00\"/></CaptureEffects></CaptureSettings>",
        "<CaptureSettings/>",
        "<CaptureSettings><CaptureEffects Scale=\"100\" BorderColor=\"#123456\" Filter=\"0\" Grayscale=\"1\"/>"
        "<ShareSettingsProgram Enabled=\"1\"/><ShareSettingsFile Prefix=\"\" FolderName=\"D:\\x\"/></CaptureSettings>",
        "<CaptureSettings SelectionMode=\"13\" CaptureMode=\"image\"><Video EnableMicrophone=\"0\"/></CaptureSettings>",
        "<CaptureSettings DisplayName=\"x\" RegionWidth=\"0x10\" RegionHeight=\"-0\" CaptureCursor=\"2\" PreviewInEditor=\"\"/>",
    };
    for (size_t i = 0; i < std::size(reads); i++) {
        XmlNode n;
        bool ok = XmlParse(reads[i], n);
        put(FormatA("read %zu %d", i, ok), Preset::FromXml(n).ToXml());
    }
    PresetStore s;
    s.presets = { Preset(), p };
    s.selected = 1;
    std::wstring file = Tmp(L"presetgolden.xml");
    bool saved = s.Export(file);
    XmlNode root;
    bool loaded = XmlLoadFile(file, root);
    DeleteFileW(file.c_str());
    put(FormatA("file %d %d", saved, loaded), root);
    return L;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc > 1 && std::wstring(argv[1]) == L"presetgolden") {   // the capture presets' XML in this build
        for (auto& l : PresetFingerprints()) printf("%s\n", l.c_str());
        return 0;
    }
    if (argc > 2 && std::wstring(argv[1]) == L"dump") {      // debugging: prints the XML of a .snap
        std::vector<SnapFrame> fr;
        if (!LoadSnapFile(argv[2], fr)) { printf("failed to open\n"); return 1; }
        printf("%s", XmlSerialize(fr[0].doc).c_str());
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc > 1 && std::wstring(argv[1]) == L"webpgolden") {    // the WebP fingerprints of this build
        for (auto& l : WebPFingerprints()) printf("%s\n", l.c_str());
        return 0;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"barcodegolden") {   // what this build reads in the barcode images
        for (auto& l : BarcodeResults(BarcodeImages())) printf("%s\n", l.c_str());
        return 0;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"miscgolden") {    // the color, HDR and OCR fingerprints of this build
        GdiplusSession gdip;
        for (auto& l : MiscFingerprints()) printf("%s\n", l.c_str());
        return 0;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"rendergolden") {  // the drawing of the editor objects in this build
        GdiplusSession gdip;
        for (auto& l : RenderFingerprints()) printf("%s\n", l.c_str());
        return 0;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"docgolden") {     // the editor objects' XML, files and hit-testing in this build
        GdiplusSession gdip;
        for (auto& l : DocFingerprints()) printf("%s\n", l.c_str());
        return 0;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"fxgolden") {      // the pixel fingerprints of this build
        GdiplusSession gdip;
        for (auto& l : PixelFingerprints(TestTempDir())) printf("%s\n", l.c_str());
        return 0;
    }
    if (argc > 2 && std::wstring(argv[1]) == L"comparesnap") {   // reads every .snap of a folder with both containers
        int files = 0, same = 0, bothFail = 0;
        double tRead[2] = {}, tWrite[2] = {};
        auto now = [] { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart / f.QuadPart; };
        std::wstring out = Tmp(L"compare.snap");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((std::wstring(argv[2]) + L"\\*.snap").c_str(), &fd);
        for (BOOL more = h != INVALID_HANDLE_VALUE; more; more = FindNextFileW(h, &fd)) {
            std::wstring p = std::wstring(argv[2]) + L"\\" + fd.cFileName;
            std::vector<SnapFrame> a, b;
            double t0 = now();
            SetSnapBackend(SnapBackend::Rust);
            bool okA = LoadSnapFile(p, a);
            double t1 = now();
            SetSnapBackend(SnapBackend::Windows);
            bool okB = LoadSnapFile(p, b);
            tRead[0] += t1 - t0; tRead[1] += now() - t1;
            if (okA) {                          // and writes it back with each one (to a temporary file)
                for (int k = 0; k < 2; k++) {
                    SetSnapBackend(k ? SnapBackend::Windows : SnapBackend::Rust);
                    double w0 = now(); SaveSnapFile(out, a); tWrite[k] += now() - w0;
                }
                DeleteFileW(out.c_str());
            }
            bool eq = okA == okB && a.size() == b.size();
            for (size_t i = 0; eq && i < a.size(); i++) {
                eq = a[i].base.w == b[i].base.w && a[i].base.px == b[i].base.px &&
                     XmlSerialize(a[i].doc) == XmlSerialize(b[i].doc) && a[i].embedded.size() == b[i].embedded.size();
                for (auto& e : a[i].embedded) eq = eq && b[i].embedded.count(e.first) && b[i].embedded.at(e.first).px == e.second.px;
            }
            files++;
            if (eq) same++;
            if (!okA && !okB) bothFail++;
            if (!eq) printf("  DIFFERENT %ls (Rust %s, Windows %s)\n", fd.cFileName, okA ? "ok" : "failed", okB ? "ok" : "failed");
        }
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
        printf("%d .snap files: %d read the same by both containers (%d refused by both)\n", files, same, bothFail);
        printf("read:  Rust %.0f ms, Windows %.0f ms (PNG decoding included)\n", tRead[0] * 1000, tRead[1] * 1000);
        printf("write: Rust %.0f ms, Windows %.0f ms (PNG encoding included)\n", tWrite[0] * 1000, tWrite[1] * 1000);
        return same == files ? 0 : 1;
    }
    if (argc > 2 && std::wstring(argv[1]) == L"audio") {    // debugging: reads only the audio track
        MFStartup(MF_VERSION);
        IMFSourceReader* rd = nullptr;
        HRESULT hr = MFCreateSourceReaderFromURL(argv[2], nullptr, &rd);
        printf("open 0x%08X\n", hr);
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
        IMFMediaType* mt = nullptr; MFCreateMediaType(&mt);
        mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM); mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        printf("settype 0x%08X\n", rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mt));
        int peak = 0;
        for (int i = 0; i < 10000; i++) {
            DWORD idx, flags; LONGLONG ts; IMFSample* smp = nullptr;
            hr = rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &idx, &flags, &ts, &smp);
            if (FAILED(hr)) { printf("sample %d FAILED 0x%08X\n", i, hr); break; }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { printf("EOS after %d samples, last ts %.3f\n", i, ts / 1e7); break; }
            if (i < 3 || i % 50 == 0) printf("sample %d ts %.3f flags %u\n", i, ts / 1e7, flags);
            if (smp) {
                IMFMediaBuffer* b = nullptr; BYTE* p; DWORD len;
                if (SUCCEEDED(smp->ConvertToContiguousBuffer(&b)) && SUCCEEDED(b->Lock(&p, nullptr, &len))) {
                    for (DWORD k = 0; k + 1 < len; k += 2) peak = std::max(peak, std::abs((int)*(const int16_t*)(p + k)));
                    b->Unlock();
                }
                if (b) b->Release();
                smp->Release();
            }
        }
        printf("audio peak: %d of 32767 (%.1f%%)\n", peak, peak * 100.0 / 32767);
        return 0;
    }
    if (argc > 3 && std::wstring(argv[1]) == L"frame") {    // debugging: saves the first frame of an MP4 as PNG
        GdiplusSession gdip;
        ed::VideoReader rd;
        Image f; LONGLONG ts;
        if (!rd.Open(argv[2]) || !rd.ReadFrame(f, ts)) { printf("failed to read the video\n"); return 1; }
        printf("%dx%d -> %s\n", f.w, f.h, SaveImageFile(argv[3], f) ? "saved" : "failed");
        return 0;
    }
    GdiplusSession gdip;

    printf("[1] Phase correlation (scroll stitching)\n");
    {
        Image page = Pattern(700, 1600, 3);
        for (int s : { 37, 120, 250 }) {
            Image a = page.Crop({ 0, 100, 700, 600 });
            Image b = page.Crop({ 0, 100 + s, 700, 600 + s });       // content moved up by s px (scrolled down)
            double conf = 0;
            POINT t = cv::DetectTranslation(a, b, &conf);
            CHECK(-t.y == s && t.x == 0, "scroll of %d px detected as %d (dx=%d, conf %.1f)", s, -t.y, t.x, conf);
        }
    }

    printf("[2] Magic wand / inpainting\n");
    {
        Image img(100, 80, 0xFFFFFFFF);
        img.FillRect({ 20, 20, 60, 50 }, 0xFFFF0000);
        cv::Mask m = cv::DoWand(img, { 30, 30 }, 10, false);
        RECT b = cv::MaskBounds(m, img.w, img.h);
        CHECK(b.left == 20 && b.top == 20 && b.right == 60 && b.bottom == 50, "wand selects the red rectangle (%ld,%ld)-(%ld,%ld)", b.left, b.top, b.right, b.bottom);
        cv::Mask all = cv::DoWand(img, { 5, 5 }, 100, false);
        CHECK(!cv::MaskEmpty(all) && all.size() == 8000 && std::all_of(all.begin(), all.end(), [](uint8_t v) { return v == 255; }), "tolerance 100 = whole layer");
        Image f = img;
        cv::SmartFill(f, m);
        CHECK(f.at(40, 35) == 0xFFFFFFFF, "Smart fill paints the hole with the dominant surrounding color (0x%08X)", f.at(40, 35));
        float tol = cv::SmartMoveAutoTolerance(img);
        CHECK(tol >= 2 && tol <= 75, "Smart Move automatic tolerance in the range [2;75]: %.2f", tol);
    }

    printf("[3] .snap format (structured storage with a header)\n");
    {
        std::wstring p = Tmp(L"doc.snap");
        SnapFrame f; f.base = Pattern(321, 123, 9);
        f.doc.setW("CaptureMode", L"Region ç → 中文");    // non-ASCII on purpose: checks UTF-8 metadata
        XmlNode& objs = f.doc.add("Objects");
        objs.add("Arrow").setd("X1", 1.5).setd("Y1", 2).setd("X2", 100).setd("Y2", 50).set("PenColor", "#FFE5322D");
        f.embedded["Image_1.png"] = Pattern(10, 10, 1);
        objs.add("Image").set("StreamName", "Image_1.png");
        bool saved = SaveSnapFile(p, { f });
        std::vector<SnapFrame> in;
        bool loaded = LoadSnapFile(p, in);
        CHECK(saved && loaded && IsSnapFile(p), "save/open .snap");
        CHECK(loaded && in[0].base.px == f.base.px, "base image identical after a round trip");
        CHECK(loaded && in[0].doc.getW("CaptureMode") == L"Region ç → 中文", "UTF-8 metadata preserved");
        CHECK(loaded && in[0].embedded.count("Image_1.png") && in[0].embedded["Image_1.png"].w == 10, "embedded image by StreamName");
        auto d = ed::Document::Load(p);
        CHECK(d && d->st.objs.size() == 2 && d->st.objs[0]->type == ed::ObjType::Arrow, "Document loads objects (%zu)", d ? d->st.objs.size() : 0);
        DeleteFileW(p.c_str());

        // the two containers (snapcore in Rust and the Windows structured storage API) read each other's files: two
        // frames, metadata and an embedded image, written by one and read by the other
        SnapBackend def = GetSnapBackend();
        SnapFrame f2; f2.base = Pattern(64, 48, 3);
        f2.doc.add("Objects").add("Text").setW("Text", L"segunda moldura ✓");
        auto same = [&](const std::vector<SnapFrame>& v) {
            return v.size() == 2 && v[0].base.px == f.base.px && v[1].base.px == f2.base.px &&
                   v[0].doc.getW("CaptureMode") == L"Region ç → 中文" && v[0].embedded.count("Image_1.png") &&
                   v[0].embedded.at("Image_1.png").px == f.embedded["Image_1.png"].px &&
                   v[1].doc.child("Objects") && v[1].doc.child("Objects")->children[0].getW("Text") == L"segunda moldura ✓";
        };
        for (SnapBackend writer : { SnapBackend::Rust, SnapBackend::Windows }) {
            SetSnapBackend(writer);
            bool ok = SaveSnapFile(p, { f, f2 });
            SetSnapBackend(writer == SnapBackend::Rust ? SnapBackend::Windows : SnapBackend::Rust);
            std::vector<SnapFrame> v;
            ok = ok && LoadSnapFile(p, v);
            XmlNode doc0;
            ok = ok && same(v) && LoadSnapDocument(p, doc0) && doc0.getW("CaptureMode") == L"Region ç → 中文";
            CHECK(ok, "a .snap written by %s opens with %s (2 frames, metadata, embedded image)",
                  writer == SnapBackend::Rust ? "snapcore (Rust)" : "the Windows API", writer == SnapBackend::Rust ? "the Windows API" : "snapcore (Rust)");
        }
        // damaged files: the right header over garbage, and a file cut in half, are refused without crashing
        SetSnapBackend(SnapBackend::Rust);
        SaveSnapFile(p, { f, f2 });
        std::string bytes;
        {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            DWORD sz = GetFileSize(h, nullptr), rd = 0;
            bytes.resize(sz); ReadFile(h, bytes.data(), sz, &rd, nullptr); CloseHandle(h);
        }
        auto writeBytes = [&](const std::string& b) {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD wr = 0; WriteFile(h, b.data(), (DWORD)b.size(), &wr, nullptr); CloseHandle(h);
        };
        std::string garbage = bytes.substr(0, 24);
        for (int i = 0; i < 8192; i++) garbage += (char)((i * 2654435761u) >> 13);
        bool refused = true;
        for (const std::string& bad : { garbage, bytes.substr(0, bytes.size() / 2) }) {
            writeBytes(bad);
            for (SnapBackend b : { SnapBackend::Rust, SnapBackend::Windows }) {
                SetSnapBackend(b);
                std::vector<SnapFrame> v;
                refused = refused && IsSnapFile(p) && !LoadSnapFile(p, v);
            }
        }
        CHECK(refused, "damaged .snap files (garbage after the header, cut in half) are refused by both containers");
        SetSnapBackend(def);
        DeleteFileW(p.c_str());
    }

    printf("[4] Rendering of every object type + PDF\n");
    {
        auto d = ed::Document::FromImage(Pattern(400, 300, 4), L"t");
        for (int t = 0; t < (int)ed::ObjType::Count; t++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = (ed::ObjType)t; o->p1 = { 20.0f + t * 5, 20 }; o->p2 = { 200.0f, 150.0f + t * 3 };
            o->text = L"Hello"; o->tail = { 250, 250 }; o->pts = { { 10, 10 }, { 50, 60 }, { 90, 20 } };
            if (o->type == ed::ObjType::Picture) o->picture = std::make_shared<Image>(Pattern(30, 30, 2));
            d->st.objs.push_back(o);
        }
        Image r = d->Render();
        CHECK(r.w == 400 && r.h == 300 && r.px != d->st.base->px, "render of %zu objects", d->st.objs.size());
        // tool variations: 24 callouts, ends, dash patterns, shapes, steps, connected magnifier and quick styles
        auto v = ed::Document::FromImage(Pattern(600, 400, 3), L"v");
        for (int k = 0; k < ed::kCalloutShapes; k++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = ed::ObjType::Callout; o->style.calloutShape = k; o->style.fill = 0xFF2080E0; o->style.width = 2;
            o->p1 = { 10.0f + (k % 6) * 95, 10.0f + (k / 6) * 90 }; o->p2 = { o->p1.X + 70, o->p1.Y + 45 }; o->tail = { o->p1.X + 90, o->p1.Y + 80 };
            o->text = L"Hello"; o->style.shaded = k % 2 == 0;
            v->st.objs.push_back(o);
        }
        for (int k = 0; k < ed::CapCount; k++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = ed::ObjType::Arrow; o->style.arrowStart = k; o->style.arrowEnd = (k + 3) % ed::CapCount; o->style.dash = k % ed::DashCount;
            o->style.bezier = k % 2 == 1; o->p1 = { 20.0f, 30.0f + k * 40 }; o->p2 = { 300.0f, 50.0f + k * 40 }; o->ctrl = { 160.0f, 10.0f + k * 40 };
            v->st.objs.push_back(o);
        }
        for (int k = 0; k < ed::ShapeCount; k++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = ed::ObjType::Shape; o->style.shapeKind = k; o->style.fill = k % 2 ? 0xFF40A040 : 0; o->style.shadowDir = k % 9;
            o->p1 = { 320.0f + (k % 4) * 65, 20.0f + (k / 4) * 70 }; o->p2 = { o->p1.X + 55, o->p1.Y + 50 };
            v->st.objs.push_back(o);
        }
        for (int k = 0; k < ed::StepTypeCount * 4; k++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = ed::ObjType::Step; o->style.stepShape = k % 4; o->style.stepType = k / 4; o->stepValue = k + 1;
            o->p1 = o->p2 = { 330.0f + (k % 5) * 50, 200.0f + (k / 5) * 45 };
            v->st.objs.push_back(o);
        }
        auto mg = std::make_shared<ed::DrawObj>();
        mg->type = ed::ObjType::Magnify; mg->style.magnifyLink = true; mg->p1 = { 450, 300 }; mg->p2 = { 560, 390 }; mg->tail = { 380, 340 };
        v->st.objs.push_back(mg);
        auto hl = std::make_shared<ed::DrawObj>();
        hl->type = ed::ObjType::Highlighter; hl->style.stroke = 0xFFFFEB3B; hl->style.opacity = 50; hl->p1 = { 10, 360 }; hl->p2 = { 300, 390 };
        v->st.objs.push_back(hl);
        Image vr = v->Render();
        if (const wchar_t* dump = _wgetenv(L"SNAPNAB_DUMP")) {
            SaveImageFile(std::wstring(dump) + L"\\variacoes.png", vr);
            Image ic(96, 96, 0xFFFFFFFF);                 // enlarged Steps icon, to check the drawing
            { auto bmp = ic.WrapGdip(); Gdiplus::Graphics gi(bmp.get()); icons::Draw(gi, icons::Step, Gdiplus::RectF(0, 0, 96, 96), Gdiplus::Color(255, 56, 56, 62)); }
            SaveImageFile(std::wstring(dump) + L"\\icone_passos.png", ic);
            Image fi(150, 96, 0xFFFFFFFF);                // Fill enlarged and at the toolbar sizes (24 and 20 px)
            {
                auto bmp = fi.WrapGdip(); Gdiplus::Graphics gi(bmp.get());
                Gdiplus::Color ink(255, 56, 56, 62);
                icons::Draw(gi, icons::Fill, Gdiplus::RectF(0, 0, 96, 96), ink);
                icons::Draw(gi, icons::Fill, Gdiplus::RectF(108, 20, 24, 24), ink);
                icons::Draw(gi, icons::Fill, Gdiplus::RectF(110, 60, 20, 20), ink);
            }
            SaveImageFile(std::wstring(dump) + L"\\icone_preencher.png", fi);
        }
        uint32_t under = vr.at(150, 375), base = v->st.base->at(150, 375);
        CHECK(vr.w == 600 && under != base && CA(under) == 255, "%zu object variations rendered; translucent rectangular highlighter", v->st.objs.size());
        {   // group (CPaintOpGroup): renders the children and comes back unchanged from the .snap
            auto gd = ed::Document::FromImage(Pattern(300, 200, 5), L"g");
            auto grp = std::make_shared<ed::DrawObj>();
            grp->type = ed::ObjType::Group;
            ed::DrawObj a; a.type = ed::ObjType::Shape; a.p1 = { 10, 10 }; a.p2 = { 80, 60 };
            ed::DrawObj b2; b2.type = ed::ObjType::Arrow; b2.p1 = { 100, 150 }; b2.p2 = { 200, 40 };
            grp->children = { a, b2 };
            grp->UpdateGroupRect();
            gd->st.objs.push_back(grp);
            gd->path = Tmp(L"group.snap");
            bool saved = gd->Save();
            auto back = ed::Document::Load(gd->path);
            bool ok = saved && back && back->st.objs.size() == 1 && back->st.objs[0]->type == ed::ObjType::Group &&
                      back->st.objs[0]->children.size() == 2 && back->st.objs[0]->children[1].type == ed::ObjType::Arrow;
            Image gr = back ? back->Render() : Image();
            CHECK(ok && gr.px != gd->st.base->px && back->st.objs[0]->HitTest({ 150, 95 }, 3), "group with 2 objects written, read and drawn");
            DeleteFileW(gd->path.c_str());
        }
        int previews = 0;
        for (int t = 0; t < (int)ed::Tool::Count; t++)
            for (auto& st : ed::GenerateStyles((ed::Tool)t, { 0xFFEF4136, 0xFF6A2C91, 0xFF15A589, 0xFFF7A823 }))
                previews += ed::QuickStylePreview((ed::Tool)t, st, 48).w == 48;
        CHECK(previews > 100, "%d quick style thumbnails", previews);
        std::wstring pdf = Tmp(L"out.pdf"), err;
        bool ok = ed::SavePdf(pdf, r, err);
        CHECK(ok && FileSize(pdf) > 1000, "PDF written (%llu bytes)", (unsigned long long)FileSize(pdf));
        DeleteFileW(pdf.c_str());
    }

    printf("[5] Animated GIF (LZW, delta frames, 4 and 8 bpp)\n");
    for (int bpp : { 8, 4 }) {
        std::wstring p = Tmp(L"anim.gif");
        GifWriter gw;
        gw.Open(p, 120, 90, 10, bpp, true, true);
        for (int i = 0; i < 6; i++) { Image fr = Pattern(120, 90, 0); fr.FillRect({ i * 15, 20, i * 15 + 20, 60 }, 0xFF00AA00); gw.AddFrame(fr); }
        bool closed = gw.Close();
        Image dec;
        bool decoded = LoadImageFile(p, dec);
        CHECK(closed && decoded && dec.w == 120 && dec.h == 90, "GIF %d bpp valid and decodable by WIC (%llu bytes)", bpp, (unsigned long long)FileSize(p));
        DeleteFileW(p.c_str());
    }

    printf("[6] Effects\n");
    {
        Image s = Pattern(80, 60, 5);
        CHECK(fx::Border(s, 5, 0xFF000000).w == 90, "border");
        CHECK(fx::Rotate90(s, true).w == 60, "rotate 90");
        Image t(50, 40, 0xFFFFFFFF); t.FillRect({ 10, 10, 30, 25 }, 0xFF123456);
        Image tr = fx::AutoTrim(t);
        CHECK(tr.w == 20 && tr.h == 15, "auto trim (%dx%d)", tr.w, tr.h);
        Image e = fx::EdgeEffect(s, fx::Edge::Torn, 10, fx::All, true);
        CHECK(!e.empty() && e.HasTransparency(), "torn edge with shadow");
        // all effects of the capture window's "Effects" menu at once
        fx::CaptureEffects all;
        all.border = all.filter = all.shadow = all.caption = all.replaceColor = all.scale = true;
        all.edge = all.trim = all.adjust = all.rotate = all.watermark = all.resolution = true;
        all.scalePercent = 50; all.rotateKind = 0; all.dpi = 300; all.filterKind = 1;
        Image fxAll = fx::ApplyCaptureEffects(s, all);
        CHECK(!fxAll.empty() && fxAll.dpi == 300.0f, "12 capture effects applied (%dx%d)", fxAll.w, fxAll.h);
        std::wstring dp = Tmp(L"dpi.png");
        Image back;
        CHECK(SaveImageFile(dp, fxAll) && LoadImageFile(dp, back) && back.w == fxAll.w, "PNG with stored resolution");
        DeleteFileW(dp.c_str());
    }

    printf("[7] Shared memory IPC (sender → receiver with ack)\n");
    {
        HANDLE fakeEditor = CreateMutexW(nullptr, FALSE, kEditorMutex);
        ipc::Notify editor, capture;
        std::atomic<int> got{ 0 }; std::wstring text;
        editor.Start(ipc::kEditor, [&](const ipc::Message& m) { if (m.code == ipc::OpenFile) text = m.text; got++; });
        capture.Start(ipc::kCapture, nullptr);
        bool s1 = capture.SendInt(ipc::kEditor, ipc::ShowEditor, 1, true);
        bool s2 = capture.SendString(ipc::kEditor, ipc::OpenFile, L"C:\\x\\captura.snap", true);
        for (int i = 0; i < 50 && got < 2; i++) Sleep(20);
        CHECK(s1 && s2 && got == 2 && text == L"C:\\x\\captura.snap", "2 messages delivered (int and string)");
        editor.Stop(); capture.Stop();
        CloseHandle(fakeEditor);
    }

    printf("[8] MP4 recording (H.264/AAC) and frame orientation\n");
    std::wstring mp4 = Tmp(L"rec.mp4");
    {
        // test window with a known vertical gradient, to check the orientation
        WNDCLASSW wc{}; wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_PAINT) {
                // left half: vertical gradient (orientation); right half: white (misaligned rows show up)
                PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r);
                for (int y = 0; y < r.bottom; y += 4) { RECT b{ 0, y, r.right / 2, y + 4 }; HBRUSH br = CreateSolidBrush(RGB(y * 255 / r.bottom, 40, 255 - y * 255 / r.bottom)); FillRect(dc, &b, br); DeleteObject(br); }
                RECT wr{ r.right / 2, 0, r.right, r.bottom }; FillRect(dc, &wr, (HBRUSH)GetStockObject(WHITE_BRUSH));
                EndPaint(h, &ps); return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SnapnabTestWnd";
        RegisterClassW(&wc);
        // 854 px: width that is not a multiple of 16 (the decoder delivers padded rows)
        HWND w = CreateWindowExW(WS_EX_TOPMOST, L"SnapnabTestWnd", L"t", WS_POPUP | WS_VISIBLE, 100, 100, 854, 360, nullptr, nullptr, wc.hInstance, nullptr);
        UpdateWindow(w);
        MSG msg; for (int i = 0; i < 20; i++) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(20); }
        Recorder rec;
        RecordSettings rs; rs.rect = { 100, 100, 954, 460 }; rs.fps = 15; rs.path = mp4; rs.hardware = false;
        rs.microphone = false; rs.systemAudio = true;
        rs.highlightCursor = true; rs.clickEffects = true; rs.keyMode = 1;      // overlays drawn in the video (C07)
        rs.leftClickColor = RGB(0, 255, 0);                                     // a color the test pattern does not have
        rs.crashSafe = true;                                                    // fragmented MP4 remuxed at the end (C14)
        bool started = rec.Start(rs, nullptr, 0);
        CHECK(started, "recorder started %s", started ? "" : WToUtf8(rec.lastError).c_str());
        for (int i = 0; i < 30; i++) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(25); }
        {   // a click on the test window (C07): it must leave a ripple in the video
            POINT old; GetCursorPos(&old);
            SetCursorPos(300, 200);
            INPUT in[2] = {};
            in[0].type = in[1].type = INPUT_MOUSE;
            in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(2, in, sizeof(INPUT));
            Sleep(30);
            SetCursorPos(old.x, old.y);
        }
        for (int i = 0; i < 30; i++) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(25); }
        rec.Pause(); Sleep(500); rec.Resume();
        for (int i = 0; i < 30; i++) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(25); }
        double el = rec.Elapsed();
        bool stopped = rec.Stop();
        DestroyWindow(w);
        {
            // nothing of this recording is left in the recovery folder (C14)
            bool left = false;
            WIN32_FIND_DATAW fd;
            HANDLE fh = FindFirstFileW((Recorder::RecoveryDir() + L"\\*.target").c_str(), &fd);
            if (fh != INVALID_HANDLE_VALUE) {
                do {
                    std::wstring tp = Recorder::RecoveryDir() + L"\\" + fd.cFileName;
                    HANDLE t = CreateFileW(tp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                    char b[2048]; DWORD n = 0;
                    if (t != INVALID_HANDLE_VALUE) { ReadFile(t, b, sizeof b, &n, nullptr); CloseHandle(t); }
                    if (Utf8ToW(std::string(b, n)) == mp4) left = true;
                } while (FindNextFileW(fh, &fd));
                FindClose(fh);
            }
            CHECK(!left, "crash-safe recording: the fragmented file was remuxed and removed from the recovery folder");
        }
        CHECK(stopped && FileSize(mp4) > 2000, "MP4 finalized (%llu bytes, %.2f s recorded, pause excluded)", (unsigned long long)FileSize(mp4), el);
        ed::VideoReader rd;
        Image f; LONGLONG ts;
        bool opened = rd.Open(mp4) && rd.ReadFrame(f, ts);
        CHECK(opened && rd.width == 854 && rd.height == 360, "MP4 decodable (%dx%d, %.2f s, %.1f fps)", rd.width, rd.height, rd.duration / 1e7, rd.fps);
        if (opened) {
            // the top must be blue (high B, low R); the bottom red
            uint32_t top = f.at(200, 10), bot = f.at(200, 350);
            CHECK(CB(top) > CR(top) && CR(bot) > CB(bot), "correct orientation (top B=%d R=%d, bottom R=%d B=%d)", CB(top), CR(top), CR(bot), CB(bot));
            // aligned rows: the gradient | white boundary stays in the middle of the image at every height
            bool aligned = true;
            for (int y : { 20, 180, 340 }) aligned &= CG(f.at(400, y)) < 120 && CG(f.at(460, y)) > 200;
            CHECK(aligned, "frame rows aligned with a width that is not a multiple of 16 (y=340: left G=%d, right G=%d)", CG(f.at(400, 340)), CG(f.at(460, 340)));
            CHECK(rd.duration > 1.0e7 && rd.duration < 3.5e7, "duration consistent with the recorded time without the pause (%.2f s)", rd.duration / 1e7);
            // the click ripple (green here) shows up in some frame around the click point (200, 100 in the video)
            int ripple = 0;
            Image fr2; LONGLONG ts2;
            ed::VideoReader rd2;
            if (rd2.Open(mp4))
                while (rd2.ReadFrame(fr2, ts2) && !ripple)
                    for (int y = 40; y < 160 && y < fr2.h; y++)
                        for (int x = 140; x < 260 && x < fr2.w; x++) { uint32_t p = fr2.at(x, y); if (CG(p) > 150 && CR(p) < 90 && CB(p) < 90) ripple++; }
            CHECK(ripple > 0, "click ripple drawn in the video (%d green pixels)", ripple);
        }
    }

    printf("[9] Video cutting (ToFileWithCuts) and GIF from video\n");
    {
        std::vector<ed::Interval> cuts = { { 3000000, 8000000 } };    // removes 0.3 s – 0.8 s
        CHECK(ed::PrecedingCutsDuration(cuts, 9000000) == 5000000 && ed::IsInsideAnyCut(cuts, 5000000) && !ed::IsInsideAnyCut(cuts, 9000000),
              "PrecedingCutsDuration / IsInsideAnyCut");
        std::wstring out = Tmp(L"cut.mp4"), err;
        bool ok = ed::TranscodeWithCuts(mp4, out, cuts, nullptr, err);
        ed::VideoReader a, b;
        a.Open(mp4); b.Open(out);
        double diff = (a.duration - b.duration) / 1e7;
        CHECK(ok && std::fabs(diff - 0.5) < 0.2, "cut MP4 is %.2f s shorter (expected 0.5) %s", diff, WToUtf8(err).c_str());
        a.Close(); b.Close();
        std::wstring gif = Tmp(L"from_video.gif");
        bool gok = ed::CreateGifFromVideo(mp4, gif, 0, 0, 8, 320, 8, true, true, nullptr, err);
        Image g;
        CHECK(gok && LoadImageFile(gif, g) && g.w == 320, "GIF from video (%llu bytes)", (unsigned long long)FileSize(gif));
        DeleteFileW(out.c_str()); DeleteFileW(gif.c_str());
        if (!ok) CopyFileW(mp4.c_str(), Tmp(L"failed.mp4").c_str(), FALSE);
    }
    DeleteFileW(mp4.c_str());

    printf("[10] OCR (Windows.Media.Ocr)\n");
    {
        Image img(600, 120, 0xFFFFFFFF);
        auto bmp = img.WrapGdip();
        { Gdiplus::Graphics gr(bmp.get()); gr.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
          Gdiplus::FontFamily ff(L"Segoe UI"); Gdiplus::Font fnt(&ff, 48, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
          Gdiplus::SolidBrush br(Gdiplus::Color(255, 0, 0, 0)); gr.DrawString(L"Snapnab OCR 2026", -1, &fnt, Gdiplus::PointF(20, 25), &br); }
        std::wstring text, err;
        bool ok = RunOcr(img, text, err);
        bool match = text.find(L"Snapnab") != std::wstring::npos || text.find(L"OCR") != std::wstring::npos;
        if (!ok) printf("  info OCR unavailable: %s\n", WToUtf8(err).c_str());
        else CHECK(match, "recognized text: \"%s\"", WToUtf8(Trim(text)).c_str());
        // the engine drops a line holding "1111"; the second pass reads it word by word (the test card for C11)
        // (200 px tall: the image is enlarged for the engine; 520 px: it is not, and the pieces are enlarged on their own)
        for (int cardH : { 200, 520 }) if (ok) {
            Image card(900, cardH, 0xFFFFFFFF);
            auto cb = card.WrapGdip();
            { Gdiplus::Graphics gr(cb.get()); gr.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
              Gdiplus::FontFamily ff(L"Segoe UI"); Gdiplus::Font fnt(&ff, 27, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
              Gdiplus::SolidBrush br(Gdiplus::Color(255, 0, 0, 0));
              gr.DrawString(L"Telefone: +55 (11) 98765-4321", -1, &fnt, Gdiplus::PointF(30, 30), &br);
              gr.DrawString(L"Cartao: 4111 1111 1111 1111", -1, &fnt, Gdiplus::PointF(30, 90), &br); }
            std::vector<OcrLine> lines;
            RunOcrLines(card, lines, err);
            std::wstring found, all;
            for (auto& it : ed::FindSensitive(lines)) if (it.kind == Tr(L"Cartão")) found = it.text;
            for (auto& l : lines) all += L"[" + l.text + L"]";
            CHECK(DigitsOfTest(found) == L"4111111111111111", "card number with \"1111\" found by the second OCR pass (%d px tall image): \"%s\" in %s",
                  cardH, WToUtf8(found).c_str(), WToUtf8(all).c_str());
        }
    }

    printf("[11] Panoramic scrolling (frame stitching)\n");
    {
        auto noise = [](int w, int h) {
            Image img(w, h);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    uint32_t v = (uint32_t)(x * 73856093u ^ y * 19349663u);
                    v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15;
                    img.at(x, y) = 0xFF000000 | (v & 0xFFFFFF);
                }
            return img;
        };
        // vertical, with a move back up in the middle (the move back must not duplicate content)
        Image page = noise(300, 1000);
        PanoramaStitcher v;
        for (int y : { 0, 37, 90, 150, 240, 200, 300, 380, 470, 560, 640, 720, 800 }) v.Add(page.Crop({ 0, y, 300, y + 200 }));
        CHECK(v.axis() == 1 && v.result().w == 300 && v.result().h == 1000 && v.result().px == page.px,
              "vertical: %dx%d rebuilt identical to the page", v.result().w, v.result().h);
        // horizontal
        Image wide = noise(1200, 150);
        PanoramaStitcher hz;
        for (int x = 0; x <= 900; x += 60) hz.Add(wide.Crop({ x, 0, x + 300, 150 }));
        CHECK(hz.axis() == 2 && hz.result().w == 1200 && hz.result().px == wide.px, "horizontal: %dx%d", hz.result().w, hz.result().h);
        // a repeated frame (nothing scrolled) adds nothing
        PanoramaStitcher same;
        same.Add(page.Crop({ 0, 0, 300, 200 }));
        bool added = same.Add(page.Crop({ 0, 0, 300, 200 }));
        CHECK(!added && same.result().h == 200, "an unchanged frame is ignored");
    }

    printf("[12] Webcam chroma key\n");
    {
        COLORREF green = RGB(0, 177, 64);
        uint8_t bg = ChromaAlpha(0xFF00B140, green, 40), bgDark = ChromaAlpha(0xFF00702A, green, 40);
        uint8_t skin = ChromaAlpha(0xFFE0AC90, green, 40), shirt = ChromaAlpha(0xFF2050C0, green, 40);
        CHECK(bg == 0 && bgDark == 0, "green background (lit and in shadow) removed (alpha %d, %d)", bg, bgDark);
        CHECK(skin == 255 && shirt == 255, "skin and blue shirt kept (alpha %d, %d)", skin, shirt);
    }

    printf("[14] Editor batch: shapes, gradients, labels, layers, clipboard, PDF links\n");
    {
        using ed::DrawObj; using ed::ObjType;
        auto solid = [](int w, int h, uint32_t c) { return Image(w, h, c); };
        auto render1 = [&](const Image& base, std::function<void(DrawObj&)> make) {
            auto d = ed::Document::FromImage(base, L"b");
            auto o = std::make_shared<DrawObj>();
            make(*o);
            d->st.objs.push_back(o);
            return d->Render();
        };
        auto similar = [](uint32_t a, uint32_t b, int tol) {
            return std::abs(CR(a) - CR(b)) <= tol && std::abs(CG(a) - CG(b)) <= tol && std::abs(CB(a) - CB(b)) <= tol;
        };
        // highlighter as an ellipse: center tinted, corner of the box untouched
        Image white = solid(200, 160, 0xFFFFFFFF);
        Image he = render1(white, [](DrawObj& o) { o.type = ObjType::Highlighter; o.style.stroke = 0xFFFF0000; o.style.opacity = 100;
                                                     o.style.shadow = false; o.style.areaShape = ed::ShapeEllipse; o.p1 = { 20, 20 }; o.p2 = { 180, 140 }; });
        CHECK(similar(he.at(100, 80), 0xFFFF0000, 10) && similar(he.at(24, 24), 0xFFFFFFFF, 2), "highlighter ellipse: fills the ellipse, not the box corner");
        // polygon highlighter (clicked corners): inside tinted, outside untouched
        Image hp = render1(white, [](DrawObj& o) { o.type = ObjType::Highlighter; o.style.stroke = 0xFF0000FF; o.style.shadow = false;
                                                     o.style.areaShape = ed::AreaPolygon; o.pts = { { 20, 20 }, { 180, 20 }, { 20, 140 } }; o.p1 = { 20, 20 }; o.p2 = { 180, 140 }; });
        CHECK(similar(hp.at(40, 40), 0xFF0000FF, 10) && similar(hp.at(170, 130), 0xFFFFFFFF, 2), "polygon highlighter (triangle) drawn only inside");
        // blur in an ellipse: inside changes, outside the ellipse stays as it was
        Image pat = Pattern(200, 160, 9);
        Image be = render1(pat, [](DrawObj& o) { o.type = ObjType::Blur; o.style.amount = 12; o.style.areaShape = ed::ShapeEllipse; o.p1 = { 20, 20 }; o.p2 = { 180, 140 }; });
        CHECK(be.at(100, 80) != pat.at(100, 80) && be.at(22, 22) == pat.at(22, 22), "blur in an ellipse: corner of the box unchanged");
        // freehand pixelate keeps the outside
        Image pf = render1(pat, [](DrawObj& o) { o.type = ObjType::Pixelate; o.style.amount = 10; o.style.areaShape = ed::AreaFreehand;
                                                   o.pts = { { 60, 30 }, { 140, 40 }, { 150, 120 }, { 70, 130 }, { 40, 80 } }; o.p1 = { 40, 30 }; o.p2 = { 150, 130 }; });
        CHECK(pf.at(5, 5) == pat.at(5, 5) && pf.at(100, 80) != pat.at(100, 80), "freehand pixelate: only inside the outline");
        // round spotlight: inside as is, outside darker
        Image sp = render1(white, [](DrawObj& o) { o.type = ObjType::Spotlight; o.style.amount = 8; o.style.areaShape = ed::ShapeEllipse; o.p1 = { 40, 30 }; o.p2 = { 160, 130 }; });
        CHECK(similar(sp.at(100, 80), 0xFFFFFFFF, 2) && CR(sp.at(42, 32)) < 230, "round spotlight darkens outside the ellipse (also the box corner)");
        // linear gradient fill (0° = left to right) in a shape
        Image gs = render1(white, [](DrawObj& o) { o.type = ObjType::Shape; o.style.width = 0; o.style.shadow = false; o.style.fill = 0xFFFF0000;
                                                     o.style.grad.type = 1; o.style.grad.angle = 0; o.style.grad.stops = { { 0, 0xFFFF0000 }, { 1, 0xFF0000FF } };
                                                     o.p1 = { 10, 10 }; o.p2 = { 190, 150 }; });
        CHECK(CR(gs.at(14, 80)) > 200 && CB(gs.at(14, 80)) < 60 && CB(gs.at(186, 80)) > 200 && CR(gs.at(186, 80)) < 60, "shape with a linear gradient (red → blue)");
        Image fillG = solid(100, 100, 0);
        ed::Gradient rg; rg.type = 2; rg.stops = { { 0, 0xFFFFFFFF }, { 1, 0xFF000000 } };
        ed::FillGradientMask(fillG, {}, { 0, 0, 100, 100 }, rg, 100);
        CHECK(CR(fillG.at(50, 50)) > 240 && CR(fillG.at(1, 1)) < 30, "radial gradient fill (Fill tool / canvas color): white center, dark corners");
        // step with an outline: the ring outside the circle takes the outline color
        Image so = render1(solid(120, 120, 0xFF000000), [](DrawObj& o) { o.type = ObjType::Step; o.style.shadow = false; o.style.stroke = 0xFF2060FF;
                                                                            o.style.fontSize = 40; o.style.stepOutline = 0xFFFFFFFF; o.style.stepOutlineWidth = 6; o.p1 = o.p2 = { 60, 60 }; });
        CHECK(similar(so.at(60, 60 - 29), 0xFFFFFFFF, 40), "step with a white outline (contrast on a dark background)");
        // line spacing: double spacing makes the text block taller
        DrawObj tx; tx.type = ObjType::Text; tx.text = L"one\ntwo\nthree"; tx.style.fontSize = 20;
        float h1 = ed::TextBlockHeight(tx, 300);
        tx.style.lineSpacing = 2.0f;
        float h2 = ed::TextBlockHeight(tx, 300);
        CHECK(h2 > h1 * 1.6f, "line spacing 2× (%.0f px → %.0f px)", h1, h2);
        // arrow label at the tail: drawn beyond the tail, part of the bounds and of the hit test
        DrawObj ar; ar.type = ObjType::Arrow; ar.p1 = { 150, 80 }; ar.p2 = { 280, 80 }; ar.text = L"Label"; ar.style.fontSize = 24; ar.style.shadow = false;
        Gdiplus::RectF lb = ed::ArrowLabelRect(ar);
        CHECK(lb.GetRight() <= 150 && ar.Bounds().X <= lb.X + 1 && ar.HitTest({ lb.X + lb.Width / 2, lb.Y + lb.Height / 2 }, 2), "arrow label beyond the tail (bounds and hit test)");
        Image al = render1(solid(300, 160, 0xFFFFFFFF), [&](DrawObj& o) { o = ar; });
        int inked = 0;
        for (int y = (int)lb.Y; y < (int)lb.GetBottom(); y++) for (int x = (int)lb.X; x < (int)lb.GetRight(); x++) inked += !similar(al.at(x, y), 0xFFFFFFFF, 30);
        CHECK(inked > 30, "arrow label text drawn (%d pixels)", inked);
        // frosted callout: what is behind the body gets blurred even with a transparent fill
        Image fc = render1(pat, [](DrawObj& o) { o.type = ObjType::Callout; o.style.calloutShape = 16; o.style.fill = 0; o.style.width = 0; o.style.shadow = false;
                                                   o.style.bgBlur = true; o.style.bgBlurAmount = 10; o.p1 = { 40, 30 }; o.p2 = { 160, 130 }; });
        CHECK(fc.at(100, 80) != pat.at(100, 80) && fc.at(5, 5) == pat.at(5, 5), "callout background blur (frosted glass)");
        // loupe with a handle: bounds include the handle, which is drawn outside the lens
        DrawObj lp; lp.type = ObjType::Magnify; lp.style.loupe = true; lp.style.loupeHandle = true; lp.style.stroke = 0xFF303030; lp.style.shadow = false;
        lp.p1 = { 20, 20 }; lp.p2 = { 100, 100 };
        Gdiplus::RectF lpb = lp.Bounds();
        Image lr = render1(solid(200, 200, 0xFFFFFFFF), [&](DrawObj& o) { o = lp; });
        CHECK(lpb.GetRight() > 120 && !similar(lr.at(105, 105), 0xFFFFFFFF, 30), "loupe: handle outside the lens drawn and inside the bounds");
        // label stamp: wide badge with the text
        float asp = ed::StampAspect(L"label:2E9E5B:Depois");
        Image ls = ed::StampImage(L"label:2E9E5B:Depois", 96);
        CHECK(asp > 1.5f && !ls.empty() && similar(ls.at(6, ls.h * 3 / 4), 0xFF2E9E5B, 50), "label stamp (aspect %.1f)", asp);
        // hidden objects are not rendered nor exported; locked/name/visibility, guides and links survive the .snap
        auto d = ed::Document::FromImage(solid(120, 90, 0xFFFFFFFF), L"l");
        auto hid = std::make_shared<DrawObj>(); hid->type = ObjType::Shape; hid->style.fill = 0xFF000000; hid->style.shadow = false; hid->p1 = { 10, 10 }; hid->p2 = { 60, 60 };
        hid->hidden = true; hid->locked = true; hid->name = L"Fundo azul ç";
        auto lk = std::make_shared<DrawObj>(); lk->type = ObjType::Link; lk->text = L"https://example.com/a b"; lk->p1 = { 70, 10 }; lk->p2 = { 110, 30 };
        auto pt = std::make_shared<DrawObj>(); pt->type = ObjType::Highlighter; pt->style.areaShape = ed::AreaPolygon; pt->pts = { { 5, 70 }, { 40, 60 }, { 30, 85 } };
        pt->p1 = { 5, 60 }; pt->p2 = { 40, 85 }; pt->floating = true; pt->style.lineSpacing = 1.5f;
        auto st = std::make_shared<DrawObj>(); st->type = ObjType::Step; st->style.stepOutlineWidth = 4; st->style.stepOutline = 0xFF112233; st->p1 = st->p2 = { 90, 70 };
        auto sh = std::make_shared<DrawObj>(); sh->type = ObjType::Shape; sh->style.grad.type = 2; sh->style.grad.stops = { { 0, 0xFFFFFFFF }, { 0.5f, 0xFF00FF00 }, { 1, 0xFF000000 } };
        sh->p1 = { 0, 0 }; sh->p2 = { 5, 5 };
        auto co = std::make_shared<DrawObj>(); co->type = ObjType::Callout; co->style.bgBlur = true; co->style.bgBlurAmount = 13; co->style.lineSpacing = 2.5f; co->text = L"x";
        co->p1 = { 0, 0 }; co->p2 = { 10, 10 };
        auto mg2 = std::make_shared<DrawObj>(); mg2->type = ObjType::Magnify; mg2->style.loupe = true; mg2->style.loupeHandle = false; mg2->style.magnifyShape = ed::MagStar;
        auto arw = std::make_shared<DrawObj>(); arw->type = ObjType::Arrow; arw->text = L"Olá"; arw->style.fontSize = 31; arw->p1 = { 1, 1 }; arw->p2 = { 50, 1 };
        d->st.objs = { hid, lk, pt, st, sh, co, mg2, arw };
        d->guidesX = { 12.5f }; d->guidesY = { 40 };
        Image hr = d->Render();
        CHECK(similar(hr.at(30, 30), 0xFFFFFFFF, 2) && similar(hr.at(80, 15), 0xFFFFFFFF, 2), "hidden object and link areas are not drawn");
        d->path = Tmp(L"batch.snap");
        bool saved = d->Save();
        auto back = ed::Document::Load(d->path);
        bool ok = saved && back && back->st.objs.size() == 8;
        if (ok) {
            auto& o = back->st.objs;
            ok = o[0]->hidden && o[0]->locked && o[0]->name == L"Fundo azul ç" && o[1]->type == ObjType::Link && o[1]->text == L"https://example.com/a b" &&
                 o[2]->style.areaShape == ed::AreaPolygon && o[2]->pts.size() == 3 && o[2]->floating && o[3]->style.stepOutlineWidth == 4 &&
                 o[3]->style.stepOutline == 0xFF112233 && o[4]->style.grad.type == 2 && o[4]->style.grad.stops.size() == 3 &&
                 o[4]->style.grad.stops[1].color == 0xFF00FF00 && o[5]->style.bgBlur && o[5]->style.bgBlurAmount == 13 &&
                 std::fabs(o[5]->style.lineSpacing - 2.5f) < 0.01f && o[6]->style.loupe && !o[6]->style.loupeHandle && o[6]->style.magnifyShape == ed::MagStar &&
                 o[7]->text == L"Olá" && o[7]->style.fontSize == 31 && back->guidesX.size() == 1 && back->guidesY.size() == 1 && back->guidesY[0] == 40;
        }
        CHECK(ok, ".snap round trip of the new properties (name, hidden, locked, link, polygon, outline, gradient, blur, spacing, loupe, label, guides)");
        DeleteFileW(d->path.c_str());
        // objects written by older versions: the new properties take their defaults
        XmlNode old("Rect");
        old.setd("X1", 1).setd("Y1", 2).setd("X2", 30).setd("Y2", 40).set("PenColor", "#FFFF0000").seti("Opacity", 100);
        auto oo = ReadObjXml(old);
        XmlNode oldHl("Highlight");
        oldHl.setd("X1", 1).setd("Y1", 2).setd("X2", 30).setd("Y2", 40).set("Points", "1.0,2.0 10.0,20.0 30.0,40.0 ");
        auto oh = ReadObjXml(oldHl);
        CHECK(oo && !oo->hidden && !oo->locked && oo->name.empty() && !oo->style.grad.Active() && oo->style.lineSpacing == 1.0f &&
              oo->style.areaShape == ed::ShapeRect && oo->style.stepOutlineWidth == 0 && !oo->style.bgBlur && !oo->style.loupe &&
              oh && oh->IsPath() && !oh->IsPointShape(), "old objects open with the defaults (old freehand highlighter still a path)");
        // quick styles keep the new properties
        ed::Style qs; qs.lineSpacing = 1.8f; qs.stepOutlineWidth = 2; qs.areaShape = ed::ShapeRoundRect; qs.loupe = true; qs.bgBlur = true;
        qs.grad.type = 1; qs.grad.angle = 45; qs.grad.stops = { { 0, 0xFF000000 }, { 1, 0xFFFFFFFF } };
        ed::Style qb = ed::StyleFromXml(ed::StyleToXml(qs));
        CHECK(ed::SameStyle(qs, qb) && qb.areaShape == ed::ShapeRoundRect && qb.grad.angle == 45, "quick style round trip with spacing, outline, area shape, loupe, blur and gradient");
        // clipboard: objects (with an embedded image) copied in one image and pasted in another
        DrawObj pic; pic.type = ObjType::Picture; pic.picture = std::make_shared<Image>(Pattern(12, 9, 4)); pic.p1 = { 33, 44 }; pic.p2 = { 45, 53 };
        DrawObj tt; tt.type = ObjType::Text; tt.text = L"Copiado"; tt.p1 = { 5, 6 }; tt.p2 = { 100, 40 };
        std::wstring src;
        auto clip = ed::ObjectsFromClip(ed::ObjectsToClip({ pic, tt }, L"C:\\doc.snap"), &src);
        CHECK(clip.size() == 2 && src == L"C:\\doc.snap" && clip[0].picture && clip[0].picture->px == pic.picture->px && clip[0].p1.X == 33 &&
              clip[1].text == L"Copiado", "objects on the clipboard: positions, text and embedded image preserved");
        // source formatting of copied text (paste keeping the formatting)
        ed::Style rf;
        bool rtf = ed::ParseRtfFormat("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Arial;}{\\f1\\fmodern Consolas;}}{\\colortbl;\\red255\\green0\\blue0;}"
                                      "\\pard\\f1\\fs32\\b\\cf1 Hello}", rf);
        CHECK(rtf && rf.font == L"Consolas" && std::fabs(rf.fontSize - 21.33f) < 0.1f && rf.bold && rf.textColor == 0xFFFF0000, "RTF: font, size, bold and color of the first run");
        ed::Style hf;
        bool html = ed::ParseHtmlFormat("Version:0.9\r\n<html><body><!--StartFragment--><span style=\"font-family: 'Fira Code', monospace; font-size: 14px; color: #569cd6\"><i>int</i></span><!--EndFragment--></body></html>", hf);
        CHECK(html && hf.font == L"Fira Code" && hf.fontSize == 14 && hf.textColor == 0xFF569CD6 && hf.italic, "HTML: font, size, color and italic of the first text");
        // PDF: the captured links become /Link annotations with the address
        std::wstring lpdf = Tmp(L"links.pdf"), lerr;
        bool pok = ed::SavePdf(lpdf, solid(200, 100, 0xFFFFFFFF), lerr, { { Gdiplus::RectF(10, 10, 50, 20), L"https://example.com/x(1)" } });
        std::string body;
        { HANDLE fh = CreateFileW(lpdf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
          if (fh != INVALID_HANDLE_VALUE) { DWORD n = GetFileSize(fh, nullptr), rd = 0; body.resize(n); ReadFile(fh, body.data(), n, &rd, nullptr); CloseHandle(fh); } }
        CHECK(pok && body.find("/Subtype /Link") != std::string::npos && body.find("(https://example.com/x\\(1\\))") != std::string::npos, "PDF with a clickable link");
        DeleteFileW(lpdf.c_str());
        // the new icons draw something
        int drawn = 0;
        for (auto id : { icons::Lock, icons::Unlock, icons::Eye, icons::EyeOff, icons::Pin, icons::AlignLeft, icons::AlignBottom, icons::DistributeH,
                         icons::DistributeV, icons::Ruler, icons::Link, icons::Group, icons::Sliders }) {
            Image ic(24, 24, 0);
            { auto bmp = ic.WrapGdip(); Gdiplus::Graphics gi(bmp.get()); icons::Draw(gi, id, Gdiplus::RectF(0, 0, 24, 24), Gdiplus::Color(255, 0, 0, 0)); }
            drawn += std::count_if(ic.px.begin(), ic.px.end(), [](uint32_t p) { return CA(p) > 0; }) > 20;
        }
        CHECK(drawn == 13, "new icons (lock, eye, pin, align, distribute, ruler, link, group, volume): %d of 13", drawn);
        // a polygon area whose corners do not exist yet (shape just switched) is drawn as its box, not skipped
        Image hn = render1(white, [](DrawObj& o) { o.type = ObjType::Highlighter; o.style.stroke = 0xFFFF0000; o.style.opacity = 100;
                                                     o.style.shadow = false; o.style.areaShape = ed::AreaPolygon; o.p1 = { 20, 20 }; o.p2 = { 180, 140 }; });
        CHECK(similar(hn.at(30, 30), 0xFFFF0000, 10), "polygon highlighter without corners drawn as its box");
        // settings import with merge: the current items stay, the missing presets, favorites and pinned stamps are added
        {
            std::string cur = "<SavedPresets Version=\"1\"><Presets><CaptureSettings DisplayName=\"A\" SelectionMode=\"0\"/></Presets></SavedPresets>";
            std::string imp = "<SavedPresets Version=\"1\"><Presets><CaptureSettings DisplayName=\"A\" SelectionMode=\"2\"/>"
                              "<CaptureSettings DisplayName=\"B\" SelectionMode=\"1\"/></Presets></SavedPresets>";
            XmlNode m; XmlParse(settings::MergeXml(cur, imp), m);
            const XmlNode* ps = m.child("Presets");
            CHECK(ps && ps->children.size() == 2 && ps->children[0].get("SelectionMode") == "0" && ps->children[1].get("DisplayName") == "B",
                  "settings merge: preset B added inside <Presets>, preset A keeps the current values");
            std::string fcur = "<Favorites><Favorite ToolMode=\"1\"><Style Foreground=\"#FF000000\"/></Favorite></Favorites>";
            std::string fimp = "<Favorites><Favorite ToolMode=\"1\"><Style Foreground=\"#FF000000\"/></Favorite>"
                               "<Favorite ToolMode=\"2\"><Style Foreground=\"#FFFF0000\"/></Favorite></Favorites>";
            XmlNode f; XmlParse(settings::MergeXml(fcur, fimp), f);
            std::string scur = "<StampOrder><Pinned><Item Id=\"a\"/></Pinned></StampOrder>";
            std::string simp = "<StampOrder><Category Key=\"x\"><Item Id=\"b\"/></Category><Pinned><Item Id=\"a\"/><Item Id=\"c\"/></Pinned></StampOrder>";
            XmlNode s; XmlParse(settings::MergeXml(scur, simp), s);
            const XmlNode* pin = s.child("Pinned");
            CHECK(f.children.size() == 2 && s.children.size() == 2 && pin && pin->children.size() == 2,
                  "settings merge: new favorite added once, stamp category and pinned stamp added");
        }
        if (const wchar_t* dump = _wgetenv(L"SNAPNAB_DUMP")) {
            SaveImageFile(std::wstring(dump) + L"\\lote_formas.png", gs);
            SaveImageFile(std::wstring(dump) + L"\\lote_lupa.png", lr);
            SaveImageFile(std::wstring(dump) + L"\\lote_rotulo.png", ls);
            SaveImageFile(std::wstring(dump) + L"\\lote_seta.png", al);
            SaveImageFile(std::wstring(dump) + L"\\lote_passo.png", so);
        }
    }

    printf("[15] Competitor batch: formats, metadata, capture and editor tools\n");
    {
        // C03: lossless WebP keeps every visible pixel; lossy WebP decodes to the same size
        Image src(64, 48, 0);
        for (int y = 0; y < src.h; y++)
            for (int x = 0; x < src.w; x++) src.at(x, y) = ARGB(x < 8 ? 0 : 255 - y, x * 4, y * 5, (x + y) * 2);
        SaveOptions lo; lo.webpLossless = true; lo.stripMetadata = true;
        std::vector<BYTE> wl, wq;
        Image back;
        bool encL = EncodeToMemoryEx(src, ImageFormat::WEBP, wl, lo) && DecodeFromMemory(wl.data(), wl.size(), back);
        bool same = encL && back.w == src.w && back.h == src.h;
        for (size_t i = 0; same && i < src.px.size(); i++)
            if (CA(src.px[i]) && src.px[i] != back.px[i]) same = false;
        CHECK(same, "WebP lossless round trip (%zu bytes), alpha and colors kept", wl.size());
        SaveOptions qo = lo; qo.webpLossless = false; qo.webpQuality = 60;
        Image backQ;
        bool encQ = EncodeToMemoryEx(src, ImageFormat::WEBP, wq, qo) && DecodeFromMemory(wq.data(), wq.size(), backQ);
        CHECK(encQ && backQ.w == 64 && backQ.h == 48 && wq.size() > 0, "WebP lossy (quality 60): %zu bytes", wq.size());
        std::wstring wpath = TestTempDir() + L"\\snapnab_test.webp";
        bool wfile = SaveImageFile(wpath, src) && LoadImageFile(wpath, back) && back.w == 64;
        CHECK(wfile && FormatFromExt(L".WEBP") == ImageFormat::WEBP && FormatFromExt(L".heic") == ImageFormat::HEIC,
              "WebP file saved and opened by extension");
        DeleteFileW(wpath.c_str());
        if (CanEncode(ImageFormat::AVIF)) {
            std::vector<BYTE> av; Image ab;
            bool ok = EncodeToMemoryEx(src, ImageFormat::AVIF, av, qo) && DecodeFromMemory(av.data(), av.size(), ab);
            CHECK(ok && ab.w == 64 && ab.h == 48, "AVIF round trip through the Windows codec (%zu bytes)", av.size());
        } else printf("  skip AVIF: no AV1 encoder on this computer\n");
        // C29: metadata written when kept (camera, date, GPS) and absent when stripped
        Image photo = Pattern(80, 60, 3);
        photo.meta = std::make_shared<ImageMeta>();
        photo.meta->make = L"TestCam"; photo.meta->model = L"Model X"; photo.meta->dateTaken = L"2024:05:06 07:08:09";
        photo.meta->gps = true; photo.meta->lat = -23.5; photo.meta->lon = -46.625;
        std::wstring jp = TestTempDir() + L"\\snapnab_meta.jpg";
        SaveOptions keep; keep.stripMetadata = false;
        Image rj;
        bool mk = SaveImageFileEx(jp, photo, keep) && LoadImageFile(jp, rj) && rj.meta;
        CHECK(mk && rj.meta->make == L"TestCam" && rj.meta->model == L"Model X" && rj.meta->dateTaken.substr(0, 10) == L"2024:05:06" &&
              rj.meta->gps && std::fabs(rj.meta->lat + 23.5) < 0.01 && std::fabs(rj.meta->lon + 46.625) < 0.01,
              "metadata kept in JPEG: camera, date and GPS (%ls | %ls | %ls | %ls | %d %.3f %.3f)", mk ? rj.meta->software.c_str() : L"-", mk ? rj.meta->make.c_str() : L"-", mk ? rj.meta->model.c_str() : L"-", mk ? rj.meta->dateTaken.c_str() : L"-", mk ? (int)rj.meta->gps : -1, mk ? rj.meta->lat : 0.0, mk ? rj.meta->lon : 0.0);
        SaveOptions strip; strip.stripMetadata = true;
        Image rs;
        bool st = SaveImageFileEx(jp, photo, strip) && LoadImageFile(jp, rs);
        CHECK(st && (!rs.meta || (rs.meta->make.empty() && !rs.meta->gps && rs.meta->software.empty())), "metadata removed when stripping");
        DeleteFileW(jp.c_str());
        // [15] next
    }

    printf("[16] Competitor batch: editor tools, canvas, outputs\n");
    {
        using namespace snap::ed;
        // C11: sensitive data patterns
        CHECK(SensitiveKind(L"maria.silva@example.com.br") == Tr(L"E-mail"), "e-mail recognized");
        CHECK(SensitiveKind(L"4111 1111 1111 1111") == Tr(L"Cartão") && SensitiveKind(L"4111 1111 1111 1112").empty(), "card number with Luhn check");
        CHECK(SensitiveKind(L"529.982.247-25") == L"CPF" && SensitiveKind(L"111.111.111-11").empty(), "CPF with check digits");
        CHECK(SensitiveKind(L"GB82 WEST 1234 5698 7654 32") == L"IBAN", "IBAN with mod 97");
        CHECK(SensitiveKind(L"192.168.10.254") == Tr(L"Endereço IP") && SensitiveKind(L"+55 (11) 98765-4321") == Tr(L"Telefone"), "IP address and phone");
        CHECK(SensitiveKind(L"https://example.com/reset?token=abc123XYZ") == Tr(L"Link com token") &&
              SensitiveKind(L"sk-abcdefghijklmnop1234567890") == Tr(L"Chave de API"), "URL with a token and an API key");
        CHECK(SensitiveKind(L"Relatório anual").empty() && SensitiveKind(L"2026-10-31").empty(), "plain text and dates are not sensitive");
        {
            OcrLine ln;
            auto word = [&](const wchar_t* t, LONG x0, LONG x1) { OcrWord w; w.text = t; w.box = { x0, 10, x1, 30 }; ln.words.push_back(w); };
            word(L"Fone:", 0, 40); word(L"(11)", 50, 80); word(L"98765-4321", 85, 170); word(L"ok", 180, 200);
            auto items = FindSensitive({ ln });
            CHECK(items.size() == 1 && items[0].box.left <= 50 && items[0].box.right >= 170 && items[0].box.left > 40,
                  "phone spread over OCR words becomes one box over those words (%zu item(s))", items.size());
        }
        // C09: combine side by side / stacked / grid
        Image a(100, 50, 0xFFFF0000), b(60, 80, 0xFF00FF00), c(40, 40, 0xFF0000FF);
        CombineOptions co; co.gap = 10; co.margin = 5;
        co.layout = 0; Image h = CombineImages({ a, b, c }, co);
        co.layout = 1; Image v = CombineImages({ a, b, c }, co);
        co.layout = 2; co.columns = 2; Image gr = CombineImages({ a, b, c }, co);
        CHECK(h.w == 5 + 100 + 10 + 60 + 10 + 40 + 5 && h.h == 5 + 80 + 5 && v.w == 5 + 100 + 5 && v.h == 5 + 50 + 10 + 80 + 10 + 40 + 5,
              "combine side by side %dx%d and stacked %dx%d", h.w, h.h, v.w, v.h);
        CHECK(gr.w == 5 + 100 + 10 + 60 + 5 && gr.h == 5 + 80 + 10 + 40 + 5 && gr.at(5 + 50, 5 + 15 + 25) == 0xFFFF0000, "combine as a grid %dx%d, centered", gr.w, gr.h);
        // C17: difference
        Image d1(50, 40, 0xFFFFFFFF), d2 = d1;
        d2.FillRect({ 10, 10, 20, 15 }, 0xFF000000);
        int changed = 0; RECT box{};
        Image diff = DiffImage(d1, d2, 10, &changed, &box);
        CHECK(changed == 50 && box.left == 10 && box.right == 20 && box.top == 10 && box.bottom == 15 && diff.at(12, 12) == 0xFFE5322D,
              "difference image: %d changed pixels in the right box", changed);
        // C23: draw & hold recognition
        {
            std::vector<Gdiplus::PointF> line, rect, ell, tri, arrow;
            for (int i = 0; i <= 40; i++) line.push_back(Gdiplus::PointF(10.0f + i * 5, 20.0f + i * 2 + (i % 2 ? 1.0f : -1.0f)));
            for (int i = 0; i <= 40; i++) rect.push_back(Gdiplus::PointF(10.0f + i * 5, 10));
            for (int i = 0; i <= 20; i++) rect.push_back(Gdiplus::PointF(210, 10.0f + i * 5));
            for (int i = 0; i <= 40; i++) rect.push_back(Gdiplus::PointF(210.0f - i * 5, 110));
            for (int i = 0; i <= 19; i++) rect.push_back(Gdiplus::PointF(10, 110.0f - i * 5));
            for (int i = 0; i <= 60; i++) { double t = i * 2 * 3.14159265 / 60; ell.push_back(Gdiplus::PointF((float)(150 + 100 * cos(t)), (float)(100 + 50 * sin(t)))); }
            Gdiplus::PointF tp[3] = { { 100, 10 }, { 190, 150 }, { 10, 150 } };
            for (int s = 0; s < 3; s++) for (int i = 0; i < 20; i++) {
                Gdiplus::PointF p = tp[s], q = tp[(s + 1) % 3];
                tri.push_back(Gdiplus::PointF(p.X + (q.X - p.X) * i / 20, p.Y + (q.Y - p.Y) * i / 20));
            }
            tri.push_back(tp[0]);
            for (int i = 0; i <= 40; i++) arrow.push_back(Gdiplus::PointF(10.0f + i * 5, 50));
            for (int i = 1; i <= 6; i++) arrow.push_back(Gdiplus::PointF(210.0f - i * 4, 50.0f - i * 4));
            Gdiplus::PointF pa, pb;
            int kl = RecognizeStroke(line, pa, pb), kr = RecognizeStroke(rect, pa, pb), ke = RecognizeStroke(ell, pa, pb);
            int kt = RecognizeStroke(tri, pa, pb), ka = RecognizeStroke(arrow, pa, pb);
            CHECK(kl == RecLine && kr == RecRect && ke == RecEllipse && kt == RecTriangle, "rough strokes: line %d, rectangle %d, ellipse %d, triangle %d", kl, kr, ke, kt);
            CHECK(ka == RecArrow && std::fabs(pb.X - 210) < 1, "a line ending in a hook is an arrow (%d, tip x %.0f)", ka, pb.X);
            // a square drawn by hand: rounded corners, wobbly sides, the end short of the start (was taken for an ellipse)
            std::vector<Gdiplus::PointF> sq;
            const double side = 150, rad = 30, pi = 3.14159265;
            for (int c = 0; c < 4; c++) {                           // corners: top right, bottom right, bottom left, top left
                double cx = c < 2 ? side - rad : rad, cy = c == 1 || c == 2 ? side - rad : rad, a0 = -pi / 2 + c * pi / 2;
                double ex = c == 0 ? rad : c == 1 ? side : c == 2 ? side - rad : 0, ey = c == 0 ? 0 : c == 1 ? rad : c == 2 ? side : side - rad;
                double fx = cx + rad * cos(a0), fy = cy + rad * sin(a0);
                for (int i = 0; i <= 12; i++)                       // the wobbly side leading to the corner, then its arc
                    sq.push_back(Gdiplus::PointF((float)(ex + (fx - ex) * i / 12 + (i % 3) - 1), (float)(ey + (fy - ey) * i / 12 + (i % 2) * 2 - 1)));
                for (int i = 1; i <= 8; i++) { double t = a0 + i * (pi / 2) / 8; sq.push_back(Gdiplus::PointF((float)(cx + rad * cos(t)), (float)(cy + rad * sin(t)))); }
            }
            sq.resize(sq.size() - 3);
            // the same square tilted by 9 degrees (its box no longer fits it: low fill, corners away from the box's)
            std::vector<Gdiplus::PointF> tilt;
            for (auto& p : sq) {
                double x = p.X - side / 2, y = p.Y - side / 2, c = cos(9 * pi / 180), s = sin(9 * pi / 180);
                tilt.push_back(Gdiplus::PointF((float)(200 + x * c - y * s), (float)(200 + x * s + y * c)));
            }
            // an uneven ellipse: egg-shaped, wobbly, not closed
            std::vector<Gdiplus::PointF> egg;
            for (int i = 0; i <= 56; i++) {
                double t = i * 2 * pi / 60, rx = (cos(t) > 0 ? 110 : 90) + 4 * sin(5 * t);
                egg.push_back(Gdiplus::PointF((float)(150 + rx * cos(t)), (float)(100 + (55 + 3 * cos(7 * t)) * sin(t))));
            }
            int ks = RecognizeStroke(sq, pa, pb), kt2 = RecognizeStroke(tilt, pa, pb), kg = RecognizeStroke(egg, pa, pb);
            CHECK(ks == RecRect && kt2 == RecRect && kg == RecEllipse,
                  "hand-drawn: rounded square %d, tilted square %d, uneven ellipse %d (squares were taken for ellipses)", ks, kt2, kg);
        }
        // C34: smart page breaks fall between "text lines"
        {
            Image page(200, 1000, 0xFFFFFFFF);
            for (int y = 0; y < 1000; y += 40) page.FillRect({ 10, y + 8, 190, y + 30 }, 0xFF202020);   // a line of text every 40 px
            auto cuts = SmartBreaks(page, 300);
            bool quiet = !cuts.empty();
            for (int ct : cuts) quiet &= (ct % 40) < 8 || (ct % 40) >= 30;
            auto parts = SplitImage(page, cuts, false);
            int total = 0; for (auto& p : parts) total += p.h;
            CHECK(quiet && cuts.size() >= 3 && total == 1000, "smart breaks between text lines (%zu cuts), parts add up to the height", cuts.size());
        }
        // C26: date and time stamp
        {
            SYSTEMTIME t{}; t.wYear = 2026; t.wMonth = 10; t.wDay = 31; t.wHour = 14; t.wMinute = 5; t.wSecond = 9;
            CHECK(DateStampText(L"yyyy-MM-dd HH:mm:ss", t) == L"2026-10-31 14:05:09" && DateStampText(L"'Hora' h:mm tt", t) == L"Hora 2:05 PM",
                  "date stamp formats (%ls)", DateStampText(L"'Hora' h:mm tt", t).c_str());
            CHECK(!DateStampText(L"", t).empty() && !DateStampText(L"dddd MMMM", t).empty(), "date stamp with the Windows short format and names");
        }
        // C36: contrast
        CHECK(std::fabs(color::ContrastRatio(RGB(0, 0, 0), RGB(255, 255, 255)) - 21.0) < 0.01 &&
              ContrastReport(RGB(119, 119, 119), RGB(255, 255, 255)).find(L"4.48:1") != std::wstring::npos,
              "WCAG contrast ratio (black/white 21:1, #777 on white 4.48:1)");
        {
            Image sel(40, 20, 0xFFFFFFFF);
            sel.FillRect({ 5, 5, 35, 15 }, 0xFF102040);
            COLORREF fg = 0, bg = 0;
            DominantColors(sel, fg, bg);
            CHECK(bg == RGB(255, 255, 255) && color::ContrastRatio(fg, bg) > 10, "dominant colors of a selection: background and text");
        }
        // C27: background removal keeps the subject and clears the background
        {
            Image photo(120, 90, 0xFFF4F4F4);
            for (int y = 0; y < photo.h; y++) for (int x = 0; x < photo.w; x++) photo.at(x, y) = ARGB(255, 240 - y / 6, 242 - y / 6, 245);
            for (int y = 25; y < 65; y++) for (int x = 35; x < 85; x++) photo.at(x, y) = 0xFFC03020;
            Image cut = RemoveBackground(photo, 30, 0);
            CHECK(CA(cut.at(2, 2)) == 0 && CA(cut.at(118, 88)) == 0 && CA(cut.at(60, 45)) == 255, "background removed, subject kept (corner a=%d, center a=%d)", CA(cut.at(2, 2)), CA(cut.at(60, 45)));
        }
        // C25: cursor stamps are drawn with transparency
        {
            Image cur = RenderCursorStamp(L"cursor:arrow", 64), hand = RenderCursorStamp(L"cursor:hand", 64);
            CHECK(!cur.empty() && CA(cur.at(0, cur.h - 1)) == 0 && CA(cur.at(cur.w / 4, cur.h / 3)) > 200 && !hand.empty(), "cursor stamps (%dx%d)", cur.w, cur.h);
        }
        // C12: words in an area, highlighter snapped to the lines
        {
            OcrLine l1, l2;
            OcrWord w1; w1.text = L"Hello"; w1.box = { 10, 10, 60, 30 }; l1.words.push_back(w1);
            OcrWord w2; w2.text = L"world"; w2.box = { 70, 10, 130, 30 }; l1.words.push_back(w2);
            OcrWord w3; w3.text = L"next"; w3.box = { 10, 50, 50, 70 }; l2.words.push_back(w3);
            auto inArea = WordsInArea({ l1, l2 }, Gdiplus::RectF(0, 0, 90, 40));
            auto snapped = SnapToTextLines({ l1, l2 }, Gdiplus::RectF(30, 15, 120, 45));
            CHECK(inArea.size() == 1 && snapped.size() == 2 && snapped[0].X < 12 && snapped[0].GetRight() > 128, "text-only words in an area (%zu), highlighter snapped to %zu line(s)", inArea.size(), snapped.size());
        }
        // C05: beautify adds the padding around the capture
        {
            fx::BeautifyOptions bo; bo.padding = 30; bo.autoBalance = false; bo.aspect = 0; bo.background = 0;
            POINT org{};
            Image bt = fx::Beautify(Image(100, 60, 0xFF3366CC), bo, &org);
            CHECK(bt.w == 160 && bt.h == 120 && org.x == 30 && org.y == 30, "beautify: %dx%d with the capture at %ld,%ld", bt.w, bt.h, org.x, org.y);
        }
        // C21: the upload settings and results through snapcore (Signature V4, JSON and the custom uploader syntax are
        // checked by cargo test against tests/data/net-cpp.txt, what the C++ code gave); nothing here goes to the network
        {
            upload::Settings none;
            upload::Result r0 = upload::UploadFile(none, L"C:\\nothing.png", L"");
            std::wstring f = TestTempDir() + L"\\snapnab_upload.png";
            HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD wr = 0;
            if (h != INVALID_HANDLE_VALUE) { WriteFile(h, "PNG", 3, &wr, nullptr); CloseHandle(h); }
            upload::Settings bad;
            bad.dest = upload::Dest::Custom;
            bad.customJson = L"{";
            upload::Result r1 = upload::UploadFile(bad, f, L"");
            DeleteFileW(f.c_str());
            CHECK(!r0.ok && !r0.error.empty() && !r1.ok && r1.error != r0.error && !r1.error.empty() && net::LoadSecret(L"Tests/None").empty(),
                  "upload refusals without a destination or with a broken custom uploader");
        }
        // C34: a long image becomes a PDF of several pages
        {
            Image tall(600, 4000, 0xFFFFFFFF);
            for (int y = 0; y < 4000; y += 40) tall.FillRect({ 20, y + 10, 580, y + 30 }, 0xFF303030);
            std::wstring pdf = TestTempDir() + L"\\snapnab_long.pdf", err;
            bool ok = SavePdf(pdf, tall, err);
            std::string data;
            if (ok) {
                HANDLE f = CreateFileW(pdf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                DWORD sz = GetFileSize(f, nullptr), n = 0;
                data.resize(sz); ReadFile(f, data.data(), sz, &n, nullptr); CloseHandle(f);
            }
            size_t cp = data.find("/Count ");
            int pages = cp != std::string::npos ? atoi(data.c_str() + cp + 7) : 0;
            CHECK(ok && (pages >= 3 || !SplitLongPages()), "long image split across PDF pages (%d pages)", pages);
            DeleteFileW(pdf.c_str());
        }
        // C35 / C37 / C43: sketch, vertical text, text along a line and pen pressure are saved and read back
        {
            DrawObj o; o.type = ObjType::Line; o.p1 = { 10, 10 }; o.p2 = { 200, 60 }; o.text = L"along";
            o.style.sketch = true; o.style.sketchSeed = 42; o.style.textOnPath = true;
            DrawObj p; p.type = ObjType::Pen; p.pts = { { 1, 1 }, { 5, 5 }, { 9, 4 } }; p.pressure = { 0.2f, 0.6f, 1.0f };
            DrawObj t; t.type = ObjType::Text; t.p1 = { 0, 0 }; t.p2 = { 80, 200 }; t.text = L"縦書き"; t.style.vertical = true;
            DrawObj hl; hl.type = ObjType::Blur; hl.p1 = { 0, 0 }; hl.p2 = { 100, 40 }; hl.style.textOnly = true; hl.textBoxes = { Gdiplus::RectF(5, 5, 30, 12) };
            auto r1 = ObjRoundTrip(o), r2 = ObjRoundTrip(p), r3 = ObjRoundTrip(t), r4 = ObjRoundTrip(hl);
            CHECK(r1 && r1->style.sketch && r1->style.sketchSeed == 42 && r1->style.textOnPath && r1->HasLabel() && r1->text == L"along" &&
                  r2 && r2->pressure.size() == 3 && std::fabs(r2->pressure[1] - 0.6f) < 0.01f && r3 && r3->style.vertical &&
                  r4 && r4->style.textOnly && r4->textBoxes.size() == 1, "new object properties survive the .snap round trip");
            Image canvas(240, 220, 0xFFFFFFFF);
            auto bmp = canvas.WrapGdip();
            Gdiplus::Graphics gg(bmp.get());
            RenderObject(gg, canvas, o); RenderObject(gg, canvas, p); RenderObject(gg, canvas, t); RenderObject(gg, canvas, hl);
            gg.Flush(Gdiplus::FlushIntentionSync);
            int dark = 0; for (uint32_t px : canvas.px) if (CR(px) < 200) dark++;
            CHECK(dark > 200, "sketch line, text along it, pressure pen and vertical text are drawn (%d px)", dark);
        }
    }

    printf("[13] Translations (lang\\*.po)\n");
    {
        // sequence of conversions of a printf string (%1$s counts by position): the translation must have the same
        auto specs = [](const std::wstring& s) {
            std::vector<std::pair<int, wchar_t>> v;
            for (size_t i = 0; i < s.size(); i++) {
                if (s[i] != L'%') continue;
                size_t j = i + 1;
                if (j < s.size() && s[j] == L'%') { i = j; continue; }
                int pos = 0;
                size_t k = j;
                while (k < s.size() && iswdigit(s[k])) k++;
                if (k < s.size() && s[k] == L'$' && k > j) { pos = _wtoi(s.substr(j, k - j).c_str()); j = k + 1; }
                while (j < s.size() && wcschr(L"-+ #0", s[j])) j++;
                while (j < s.size() && (iswdigit(s[j]) || s[j] == L'*' || s[j] == L'.')) j++;
                if (s.compare(j, 3, L"I64") == 0) j += 3;
                while (j < s.size() && wcschr(L"hlzjtL", s[j])) j++;
                if (j >= s.size()) break;
                wchar_t c = towlower(s[j]);
                v.push_back({ pos, c == L'i' ? L'd' : c });
                i = j;
            }
            std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first < b.first; });
            std::wstring out;
            for (auto& p : v) out += p.second;
            return out;
        };
        CHECK(Format(L"%2$s, %1$s", L"world", L"hello") == L"hello, world", "Format accepts positional parameters (%%1$s)");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((ExeDir() + L"\\lang\\*.po").c_str(), &fd);
        size_t full = 0;
        std::vector<std::pair<std::wstring, size_t>> counts;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::vector<std::pair<std::wstring, std::wstring>> e;
                bool ok = i18n::LoadPo(ExeDir() + L"\\lang\\" + fd.cFileName, e);
                int bad = 0;
                for (auto& [id, str] : e) if (specs(id) != specs(str)) { if (!bad) printf("       %ls: \"%ls\"\n", fd.cFileName, id.c_str()); bad++; }
                CHECK(ok && !e.empty() && !bad, "%ls: %zu strings, %d with %%d/%%s markers different from the original", fd.cFileName, e.size(), bad);
                counts.push_back({ fd.cFileName, e.size() });
                full = std::max(full, e.size());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        CHECK(!counts.empty(), "%zu catalogs in the lang folder", counts.size());
        for (auto& [name, n] : counts) CHECK(n == full, "%ls complete (%zu of %zu)", name.c_str(), n, full);
    }

    printf("[17] XML: Rust and the former C++\n");
    {
        // the Rust reader and writer must give the trees and the bytes the C++ ones gave. Strings that are not valid
        // UTF-8 (only garbage input makes them) are compared by shape: Rust turns the bad bytes into U+FFFD.
        auto validUtf8 = [](const std::string& t) {
            return t.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, t.data(), (int)t.size(), nullptr, 0) > 0;
        };
        auto sameStr = [&](const std::string& ref, const std::string& rust) {
            return validUtf8(ref) ? ref == rust : validUtf8(rust);
        };
        auto same = [&](auto& self, const XmlNode& a, const XmlNode& b) -> bool {
            if (!sameStr(a.name, b.name) || !sameStr(a.text, b.text) || a.attrs.size() != b.attrs.size() ||
                a.children.size() != b.children.size()) return false;
            for (size_t i = 0; i < a.attrs.size(); i++)
                if (!sameStr(a.attrs[i].first, b.attrs[i].first) || !sameStr(a.attrs[i].second, b.attrs[i].second)) return false;
            for (size_t i = 0; i < a.children.size(); i++) if (!self(self, a.children[i], b.children[i])) return false;
            return true;
        };
        auto valid = [&](auto& self, const XmlNode& n) -> bool {
            if (!validUtf8(n.name) || !validUtf8(n.text)) return false;
            for (auto& [k, v] : n.attrs) if (!validUtf8(k) || !validUtf8(v)) return false;
            for (auto& c : n.children) if (!self(self, c)) return false;
            return true;
        };
        // one text through both readers; false (and the text shown) when they differ. 'thrown' counts the texts the
        // former reader could not handle (it threw on "<a><b/" at the very end; the Rust one reports malformed)
        int thrown = 0;
        auto compare = [&](const std::string& t) {
            XmlNode a, b;
            bool okA;
            try { okA = xmlref::Parse(t, a); } catch (...) { thrown++; return !XmlParse(t, b); }
            bool okB = XmlParse(t, b);
            bool eq = okA == okB && same(same, a, b);
            if (eq && valid(valid, a))
                eq = xmlref::Serialize(a, true) == XmlSerialize(b) && xmlref::Serialize(a, false) == XmlSerialize(a, false);
            if (!eq) printf("       differs: \"%.200s\"\n", t.c_str());
            return eq;
        };

        // real documents: the XML files of the user's Snapnab folder (presets, Names.xml…) and the index and frame
        // documents inside the .snap files of the library, only read
        std::vector<std::string> real;
        auto walk = [&](auto& self, const std::wstring& dir) -> void {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                std::wstring n = fd.cFileName, path = dir + L"\\" + n;
                if (n == L"." || n == L"..") continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { self(self, path); continue; }
                std::wstring ext = n.size() > 4 ? n.substr(n.size() - 4) : L"";
                for (auto& c : ext) c = towlower(c);
                if (ext == L".xml") {
                    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (f == INVALID_HANDLE_VALUE) continue;
                    std::string buf(GetFileSize(f, nullptr), '\0');
                    DWORD rd = 0;
                    ReadFile(f, buf.data(), (DWORD)buf.size(), &rd, nullptr);
                    CloseHandle(f);
                    buf.resize(rd);
                    real.push_back(std::move(buf));
                } else if (ext == L"snap") {
                    SnapcoreReader* r = snapcore_open(path.c_str(), path.size());
                    if (!r) continue;
                    auto read = [&](const std::string& stream) {
                        SnapcoreBuf b{};
                        if (!snapcore_read(r, stream.data(), stream.size(), &b)) return false;
                        real.emplace_back(reinterpret_cast<const char*>(b.data), b.len);
                        snapcore_buf_free(b);
                        return true;
                    };
                    if (read("Index"))
                        for (int i = 0; i < 64 && read("Frame" + std::to_string(i) + "/Document"); i++) {}
                    snapcore_close(r);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        };
        walk(walk, AppDataDir());
        wchar_t extra[MAX_PATH];         // more documents to compare (a copy of another library, for example)
        if (GetEnvironmentVariableW(L"SNAPNAB_XML_CORPUS", extra, MAX_PATH)) walk(walk, extra);
        int realBad = 0;
        for (auto& t : real) if (!compare(t)) realBad++;
        CHECK(!realBad, "%zu real XML documents (Snapnab folder and .snap files) read and written alike, %d differ", real.size(), realBad);

        // generated text: pieces of XML, malformed on purpose most of the time
        static const char* pieces[] = { "<a", "<b", "<Frame", ">", "/>", "</a>", "</b>", "</x>", " ", "\n", "\t", "k=",
            "\"v\"", "'w'", "=", "\"", "'", "&amp;", "&lt;", "&#65;", "&#x1F600;", "&#xD800;", "&#-1;", "&#x0x41;",
            "&bogus;", "&", ";", "text", "\xC3\xA7", "\xE4\xB8\xAD", "<!-- c -->", "<?pi?>", "<!DOCTYPE d>",
            "<![CDATA[x<y&z]]>", "\xEF\xBB\xBF", "\r\n" };
        uint32_t seed = 12345;
        auto rnd = [&](uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; };
        int genBad = 0, genOk = 0;
        const int kTexts = 30000;
        for (int k = 0; k < kTexts; k++) {
            std::string t = rnd(4) ? "<a" : "";
            for (uint32_t n = 1 + rnd(40); n--;) t += pieces[rnd(_countof(pieces))];
            if (!compare(t)) genBad++;
            XmlNode tmp;
            try { if (xmlref::Parse(t, tmp)) genOk++; } catch (...) {}
        }
        CHECK(!genBad, "%d generated texts (%d well formed, %d the former reader threw on): %d differ", kTexts, genOk, thrown, genBad);

        // generated trees: written alike, and read back the same by both readers
        static const char* words[] = { "a", "Frame", "x-y", "\xC3\xA7\xE4\xB8\xAD", "&<>\"'", "\r\n\t", " sp ", "", "1.5" };
        int treeBad = 0;
        for (int k = 0; k < 3000; k++) {
            auto make = [&](auto& self, XmlNode& n, int depth) -> void {
                n.name = std::string("N") + words[rnd(3)];
                for (uint32_t i = rnd(4); i--;)
                    n.attrs.emplace_back("k" + std::to_string(rnd(9)), std::string(words[rnd(_countof(words))]) + words[rnd(_countof(words))]);
                if (rnd(3) == 0) n.text = std::string(words[rnd(_countof(words))]) + "x";
                if (depth < 4) for (uint32_t i = rnd(4); i--;) self(self, n.children.emplace_back(), depth + 1);
            };
            XmlNode root;
            make(make, root, 0);
            std::string a = xmlref::Serialize(root, true), b = XmlSerialize(root);
            XmlNode ra, rb;
            if (!(a == b && xmlref::Parse(a, ra) && XmlParse(b, rb) && same(same, ra, rb))) treeBad++;
        }
        CHECK(!treeBad, "3000 generated trees written byte for byte alike and read back alike (%d differ)", treeBad);

        // files: written by Rust, read back by Rust and by the former reader
        std::wstring xp = Tmp(L"xml.xml");
        XmlNode doc("Presets");
        doc.add("Preset").setW("Name", L"Região ç → 中文 😀").set("Hotkey", "Ctrl+\"<&>\"");
        XmlNode back, refBack, untouched("Keep");
        bool saved = XmlSaveFile(xp, doc), loaded = XmlLoadFile(xp, back);
        std::string bytes;
        HANDLE f = CreateFileW(xp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            bytes.resize(GetFileSize(f, nullptr));
            DWORD rd = 0;
            ReadFile(f, bytes.data(), (DWORD)bytes.size(), &rd, nullptr);
            CloseHandle(f);
        }
        DeleteFileW(xp.c_str());
        bool missing = !XmlLoadFile(xp, untouched) && untouched.name == "Keep";
        CHECK(saved && loaded && bytes == xmlref::Serialize(doc, true) && xmlref::Parse(bytes, refBack) && same(same, refBack, back) &&
              back.child("Preset") && back.child("Preset")->getW("Name") == L"Região ç → 中文 😀" && missing,
              "XML file written and read back; a missing file leaves the tree as it was");
    }

    printf("[18] Image files: Rust and the former C++\n");
    {
        // the Rust codecs (snapcore, through WIC) must write the bytes and read the images the C++ ones did
        auto sameImage = [](const Image& a, const Image& b) {
            if (a.w != b.w || a.h != b.h || a.px != b.px || !a.meta != !b.meta) return false;
            if (!a.meta) return true;
            const ImageMeta &x = *a.meta, &y = *b.meta;
            return x.dateTaken == y.dateTaken && x.make == y.make && x.model == y.model && x.software == y.software &&
                   x.gps == y.gps && x.lat == y.lat && x.lon == y.lon;
        };
        std::vector<std::pair<const char*, Image>> imgs;
        imgs.push_back({ "opaque 37x21", Pattern(37, 21, 1) });
        Image alpha = Pattern(64, 48, 2);
        for (size_t i = 0; i < alpha.px.size(); i++) alpha.px[i] = (alpha.px[i] & 0xFFFFFF) | ((uint32_t)(i * 7 % 256) << 24);
        imgs.push_back({ "alpha 64x48", alpha });
        imgs.push_back({ "1x1", Image(1, 1, 0xFF123456) });
        Image dpi = Pattern(50, 40, 3); dpi.dpi = 300;
        imgs.push_back({ "300 dpi", dpi });
        imgs.push_back({ "640x480", Pattern(640, 480, 4) });
        Image photo = Pattern(32, 24, 5);
        auto meta = std::make_shared<ImageMeta>();
        meta->dateTaken = L"2024:05:06 07:08:09"; meta->make = L"Maker ç"; meta->model = L"Model 中";
        meta->gps = true; meta->lat = -23.5505; meta->lon = -46.6333;
        photo.meta = meta;
        imgs.push_back({ "photo with EXIF", photo });

        std::vector<std::pair<const char*, ImageFormat>> fmts = { { "PNG", ImageFormat::PNG }, { "JPEG", ImageFormat::JPEG },
            { "BMP", ImageFormat::BMP }, { "GIF", ImageFormat::GIF }, { "TIFF", ImageFormat::TIFF } };
        bool avif = CanEncode(ImageFormat::AVIF);
        if (avif) fmts.push_back({ "AVIF", ImageFormat::AVIF });
        int encodings = 0, encBad = 0, decBad = 0;
        for (bool strip : { true, false })
            for (int q : { 90, 40 })
                for (auto& [iname, img] : imgs)
                    for (auto& [fname, fmt] : fmts) {
                        if (q == 40 && fmt != ImageFormat::JPEG && fmt != ImageFormat::AVIF) continue;
                        SaveOptions o; o.stripMetadata = strip; o.jpegQuality = q; o.avifQuality = q;
                        std::vector<BYTE> a, b;
                        bool okA = imgref::EncodeToMemoryEx(img, fmt, a, o), okB = EncodeToMemoryEx(img, fmt, b, o);
                        // JPEG and TIFF carry the time of writing: a second turning between the two calls changes it
                        if (okA && okB && a != b && !strip) {
                            okA = imgref::EncodeToMemoryEx(img, fmt, a, o);
                            okB = EncodeToMemoryEx(img, fmt, b, o);
                        }
                        encodings++;
                        // the AV1 encoder of the graphics card need not give the same bytes twice: AVIF is compared decoded
                        bool sameBytes = fmt == ImageFormat::AVIF || a == b;
                        if (okA != okB || !okA || !sameBytes) {
                            encBad++;
                            printf("       %s, %s, quality %d%s: %s\n", fname, iname, q, strip ? "" : ", metadata",
                                   okA != okB || !okA ? "written by one only" : "different bytes");
                        }
                        Image da, db;
                        bool dA = imgref::DecodeFromMemory(b.data(), b.size(), da), dB = DecodeFromMemory(b.data(), b.size(), db);
                        if (dA != dB || !dA || !sameImage(da, db)) {
                            decBad++;
                            printf("       %s, %s: read differently\n", fname, iname);
                        }
                    }
        CHECK(!encBad, "%d encodings (PNG, JPEG, BMP, GIF, TIFF%s; with and without metadata) byte for byte the same, %d differ",
              encodings, avif ? ", AVIF" : "", encBad);
        CHECK(!decBad, "the same files read alike (pixels and metadata), %d differ", decBad);

        // photos turned by their EXIF orientation, read alike and upright
        Image src = Pattern(30, 20, 6);
        int orientBad = 0;
        for (int o = 1; o <= 8; o++) {
            std::vector<BYTE> jpg;
            Image a, b;
            if (!imgref::EncodeJpegWithOrientation(src, o, jpg) || !imgref::DecodeFromMemory(jpg.data(), jpg.size(), a) ||
                !DecodeFromMemory(jpg.data(), jpg.size(), b) || !sameImage(a, b) || (o >= 5 ? a.w != 20 : a.w != 30)) orientBad++;
        }
        CHECK(!orientBad, "JPEG with the 8 EXIF orientations turned upright alike (%d differ)", orientBad);

        // real images: the repository's, the library thumbnails and the base images of the .snap files (only read)
        int realFiles = 0, realBad = 0;
        auto compareFile = [&](const std::wstring& path) {
            Image a, b;
            bool okA = imgref::LoadImageFile(path, a), okB = LoadImageFile(path, b);
            realFiles++;
            if (okA != okB || !sameImage(a, b)) { realBad++; printf("       %ls: read differently\n", path.c_str()); }
        };
        std::wstring repo = ExeDir() + L"\\..\\..\\..\\..";
        for (const wchar_t* f : { L"\\src\\resources", L"\\packaging\\installer", L"\\docs\\images" }) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((repo + f + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                std::wstring ext = FileExt(fd.cFileName);
                for (auto& c : ext) c = towlower(c);
                if (ext == L".png" || ext == L".ico" || ext == L".jpg" || ext == L".bmp" || ext == L".gif") compareFile(repo + f + L"\\" + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        int snaps = 0;
        {
            WIN32_FIND_DATAW fd;
            std::wstring thumbs = LibraryDir() + L"\\Thumbnails";
            HANDLE h = FindFirstFileW((thumbs + L"\\*.png").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                int n = 0;
                do { compareFile(thumbs + L"\\" + fd.cFileName); } while (++n < 150 && FindNextFileW(h, &fd));
                FindClose(h);
            }
            h = FindFirstFileW((LibraryDir() + L"\\*.snap").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    std::wstring p = LibraryDir() + L"\\" + fd.cFileName;
                    SnapcoreReader* r = snapcore_open(p.c_str(), p.size());
                    if (!r) continue;
                    SnapcoreBuf buf{};
                    const char kStream[] = "Frame0/ImageStorage/Image_0.png";
                    if (snapcore_read(r, kStream, sizeof kStream - 1, &buf)) {
                        Image a, b;
                        bool okA = imgref::DecodeFromMemory(buf.data, buf.len, a), okB = DecodeFromMemory(buf.data, buf.len, b);
                        snaps++;
                        if (okA != okB || !sameImage(a, b)) { realBad++; printf("       %ls: base image read differently\n", fd.cFileName); }
                        snapcore_buf_free(buf);
                    }
                    snapcore_close(r);
                } while (snaps < 150 && FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
        CHECK(realFiles + snaps > 0 && !realBad, "%d image files and %d .snap base images read alike, %d differ", realFiles, snaps, realBad);

        // what the user is told when a file does not open
        std::wstring junk = Tmp(L"junk.xyz");
        { HANDLE h = CreateFileW(junk.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
          DWORD wr; WriteFile(h, "plain text, not an image", 24, &wr, nullptr); CloseHandle(h); }
        Image ja, jb;
        bool la = imgref::LoadImageFile(junk, ja), lb = LoadImageFile(junk, jb);
        std::wstring ea = imgref::Error(), eb = ImageIoError();
        std::wstring heic = Tmp(L"junk.heic");
        MoveFileExW(junk.c_str(), heic.c_str(), MOVEFILE_REPLACE_EXISTING);
        bool ha = imgref::LoadImageFile(heic, ja), hb = LoadImageFile(heic, jb);
        std::wstring hea = imgref::Error(), heb = ImageIoError();
        DeleteFileW(heic.c_str());
        CHECK(!la && !lb && !ea.empty() && ea == eb && !ha && !hb && hea == heb && hea == CodecHint(ImageFormat::HEIC),
              "a file that is not an image gives the same message (\"%ls\"), a bad .heic the codec hint", eb.c_str());

        // saving to a file: the same bytes as the encoder gives
        std::wstring sp = Tmp(L"save.png");
        SaveOptions so; so.stripMetadata = true;
        std::vector<BYTE> expect;
        bool saved = SaveImageFileEx(sp, alpha, so) && imgref::EncodeToMemoryEx(alpha, ImageFormat::PNG, expect, so);
        std::string onDisk;
        HANDLE hf = CreateFileW(sp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf != INVALID_HANDLE_VALUE) {
            onDisk.resize(GetFileSize(hf, nullptr));
            DWORD rd = 0; ReadFile(hf, onDisk.data(), (DWORD)onDisk.size(), &rd, nullptr); CloseHandle(hf);
        }
        DeleteFileW(sp.c_str());
        CHECK(saved && onDisk == std::string(expect.begin(), expect.end()) && GetFileAttributesW((sp + L".tmp").c_str()) == INVALID_FILE_ATTRIBUTES,
              "SaveImageFileEx writes the encoder's bytes and leaves no .tmp behind");
    }

    printf("[19] .snap documents: Rust and the former C++\n");
    {
        // snapcore (snap.rs) must write the bytes and read the documents the C++ code did, over the same container
        SnapBackend def = GetSnapBackend();
        SetSnapBackend(SnapBackend::Rust);
        auto readAll = [](const std::wstring& path) {
            std::string b;
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) return b;
            b.resize(GetFileSize(h, nullptr));
            DWORD rd = 0; ReadFile(h, b.data(), (DWORD)b.size(), &rd, nullptr); CloseHandle(h);
            return b;
        };
        // the compound file records when each storage was created and modified: those times are cleared before comparing
        // (the directory entries of 128 bytes, found through the FAT sectors the header lists)
        auto withoutTimes = [](std::string b) {
            auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= b.size()) memcpy(&v, b.data() + o, 4); return v; };
            const size_t base = 24;                                         // the .snap header
            if (b.size() < base + 512) return b;
            uint16_t shift = 0; memcpy(&shift, b.data() + base + 0x1E, 2);
            if (shift != 9 && shift != 12) return b;
            size_t sec = (size_t)1 << shift;
            auto at = [&](uint32_t n) { return base + (size_t)(n + 1) * sec; };
            std::vector<uint32_t> fat;
            for (int i = 0; i < 109; i++) {
                uint32_t fs = u32(base + 0x4C + i * 4);
                if (fs >= 0xFFFFFFFA) break;
                for (size_t k = 0; k < sec / 4; k++) fat.push_back(u32(at(fs) + k * 4));
            }
            for (uint32_t n = u32(base + 0x30), guard = 0; n < fat.size() && guard < 100000; n = fat[n], guard++)
                for (size_t e = 0; e < sec / 128; e++) {
                    size_t o = at(n) + e * 128 + 100;
                    if (o + 16 <= b.size()) memset(b.data() + o, 0, 16);
                }
            return b;
        };
        auto sameImage = [](const Image& a, const Image& b) {
            if (a.w != b.w || a.h != b.h || a.px != b.px || !a.meta != !b.meta) return false;
            if (!a.meta) return true;
            const ImageMeta &x = *a.meta, &y = *b.meta;
            return x.dateTaken == y.dateTaken && x.make == y.make && x.model == y.model && x.software == y.software &&
                   x.gps == y.gps && x.lat == y.lat && x.lon == y.lon;
        };
        auto sameFrames = [&](const std::vector<SnapFrame>& a, const std::vector<SnapFrame>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); i++) {
                if (!sameImage(a[i].base, b[i].base) || XmlSerialize(a[i].doc) != XmlSerialize(b[i].doc) ||
                    a[i].embedded.size() != b[i].embedded.size()) return false;
                for (auto& [k, v] : a[i].embedded)
                    if (!b[i].embedded.count(k) || !sameImage(v, b[i].embedded.at(k))) return false;
            }
            return true;
        };

        // generated documents: frames, objects, embedded images, photo metadata (GPS values on the rounding ties of
        // "%.4f" too), a Metadata element already there, an empty base image
        std::vector<std::pair<std::string, std::vector<SnapFrame>>> docs;
        {
            SnapFrame a; a.base = Pattern(40, 30, 11);
            a.doc.setW("CaptureMode", L"Region ç → 中文").set("Tags", "a;b & <c>");
            XmlNode& objs = a.doc.add("Objects");
            objs.add("Arrow").seti("X1", -5).setd("Width", 2.5);
            objs.add("Image").set("StreamName", "Image_1.png");
            objs.add("Group").add("Image").set("StreamName", "Image_2.png");
            objs.add("Image").set("StreamName", "Missing.png");
            a.embedded["Image_1.png"] = Pattern(10, 8, 12);
            a.embedded["Image_2.png"] = Image(3, 2, 0x80FF0000);
            a.embedded["Unreferenced.png"] = Pattern(5, 5, 13);
            docs.push_back({ "objects and embedded images", { a } });
            SnapFrame b; b.base = Pattern(64, 48, 14);
            b.doc.add("Objects").add("Text").setW("Text", L"segunda moldura ✓");
            docs.push_back({ "two frames", { a, b } });
        }
        const double ties[] = { 0.03125, -0.03125, 0.09375, 0.15625, -0.21875, 12.40625, -23.5505, -46.6333, 0, 179.99995, 1e-9, -0.00005 };
        for (size_t i = 0; i < std::size(ties); i++) {
            SnapFrame f; f.base = Pattern(16, 12, (int)i);
            auto m = std::make_shared<ImageMeta>();
            m->dateTaken = L"2024:05:06 07:08:09"; m->make = L"Maker ç"; m->model = L"Model 中";
            m->gps = true; m->lat = ties[i]; m->lon = ties[std::size(ties) - 1 - i];
            f.base.meta = m;
            docs.push_back({ FormatA("photo, GPS %.6f %.6f", m->lat, m->lon), { f } });
        }
        {
            SnapFrame f; f.base = Pattern(16, 12, 20);
            auto m = std::make_shared<ImageMeta>(); m->software = L"Camera app";
            f.base.meta = m;
            docs.push_back({ "photo without GPS", { f } });
            f.doc.add("Metadata").set("Make", "kept as it is");
            docs.push_back({ "Metadata element already there", { f } });
        }
        {
            SnapFrame f;
            docs.push_back({ "empty base image", { f } });
            docs.push_back({ "no frames", {} });
        }
        std::wstring pa = Tmp(L"ref_a.snap"), pb = Tmp(L"ref_b.snap");
        int writeBad = 0, readBad = 0;
        for (auto& [name, frames] : docs) {
            DeleteFileW(pa.c_str()); DeleteFileW(pb.c_str());
            bool okA = snapref::SaveSnapFile(pa, frames), okB = SaveSnapFile(pb, frames);
            std::string ba = withoutTimes(readAll(pa)), bb = withoutTimes(readAll(pb));
            if (okA != okB || ba != bb) {
                writeBad++;
                // which stream differs (or the container itself)
                std::string where = "the container";
                std::vector<std::string> streams = { "Index" };
                for (size_t i = 0; i < frames.size(); i++) {
                    std::string fr = FormatA("Frame%zu/", i);
                    streams.push_back(fr + "Document");
                    streams.push_back(fr + "ImageStorage/Image_0.png");
                    for (auto& e : frames[i].embedded) streams.push_back(fr + "ImageStorage/" + e.first);
                }
                SnapcoreReader *xa = snapcore_open(pa.c_str(), pa.size()), *xb = snapcore_open(pb.c_str(), pb.size());
                for (auto& st : streams) {
                    SnapcoreBuf a{}, b{};
                    bool ra = xa && snapcore_read(xa, st.data(), st.size(), &a), rb = xb && snapcore_read(xb, st.data(), st.size(), &b);
                    bool differ = ra != rb || (ra && std::string((const char*)a.data, a.len) != std::string((const char*)b.data, b.len));
                    if (differ && where == "the container") {
                        where = st;
                        if (ra && rb && st.find("Document") != std::string::npos)
                            printf("         C++:  %.*s\n         Rust: %.*s\n", (int)a.len, (const char*)a.data, (int)b.len, (const char*)b.data);
                    }
                    if (ra) snapcore_buf_free(a);
                    if (rb) snapcore_buf_free(b);
                }
                snapcore_close(xa); snapcore_close(xb);
                printf("       %s: %s\n", name.c_str(), okA != okB ? "saved by one only" : ("different bytes in " + where).c_str());
            }
            std::vector<SnapFrame> ra, rb;
            XmlNode da, db;
            bool lA = snapref::LoadSnapFile(pb, ra), lB = LoadSnapFile(pb, rb);
            bool dA = snapref::LoadSnapDocument(pb, da), dB = LoadSnapDocument(pb, db);
            if (lA != lB || (lA && !sameFrames(ra, rb)) || dA != dB || XmlSerialize(da) != XmlSerialize(db)) {
                readBad++;
                printf("       %s: read differently\n", name.c_str());
            }
        }
        CHECK(!writeBad, "%zu generated documents saved byte for byte the same, but for the storage times (%d differ)", docs.size(), writeBad);
        CHECK(!readBad, "and read the same (frames, documents, images, metadata), %d differ", readBad);

        // odd files made stream by stream: a malformed document, another base image name, a frame without its
        // document, a malformed index, an index without frames, damaged and missing files, a file that is not a .snap
        std::vector<BYTE> png0, png1;
        EncodeToMemory(Pattern(20, 10, 21), ImageFormat::PNG, png0);
        EncodeToMemory(Pattern(6, 6, 22), ImageFormat::PNG, png1);
        using Streams = std::vector<std::pair<std::string, std::string>>;
        auto make = [&](const Streams& streams) {
            SnapcoreWriter* w = snapcore_create(pa.c_str(), pa.size());
            for (auto& [k, v] : streams) snapcore_write(w, k.data(), k.size(), (const uint8_t*)v.data(), v.size());
            snapcore_finish(w);
        };
        std::string p0(png0.begin(), png0.end()), p1(png1.begin(), png1.end());
        const std::string idx1 = "<Index><FrameList><Frame Name=\"Frame0\"/></FrameList></Index>";
        const std::string idx2 = "<Index><FrameList><Frame Name=\"Frame0\"/><Frame Name=\"Frame1\"/></FrameList></Index>";
        std::vector<std::pair<const char*, Streams>> odd = {
            { "malformed document", { { "Index", idx1 }, { "Frame0/Document", "<Document A=\"1\"><Objects><Image StreamName=\"x.png\"/>" },
                                      { "Frame0/ImageStorage/Image_0.png", p0 }, { "Frame0/ImageStorage/x.png", p1 } } },
            { "another base image", { { "Index", idx1 }, { "Frame0/Document", "<Document BaseImage=\"b.png\"/>" },
                                      { "Frame0/ImageStorage/b.png", p0 } } },
            { "base image missing", { { "Index", idx1 }, { "Frame0/Document", "<Document/>" } } },
            { "second frame without its document", { { "Index", idx2 }, { "Frame0/Document", "<Document/>" },
                                      { "Frame0/ImageStorage/Image_0.png", p0 } } },
            { "second frame without its base image", { { "Index", idx2 }, { "Frame0/Document", "<Document/>" },
                                      { "Frame0/ImageStorage/Image_0.png", p0 }, { "Frame1/Document", "<Document/>" } } },
            { "malformed index", { { "Index", "<Index><FrameList>" }, { "Frame0/Document", "<Document/>" },
                                   { "Frame0/ImageStorage/Image_0.png", p0 } } },
            { "index without frames", { { "Index", "<Index/>" } } },
            { "metadata with odd values", { { "Index", idx1 },
                                      { "Frame0/Document", "<Document><Metadata Gps=\" 7x\" Latitude=\"-1.5e1z\" Longitude=\".\" Make=\"&amp;\"/></Document>" },
                                      { "Frame0/ImageStorage/Image_0.png", p0 } } },
            { "empty metadata", { { "Index", idx1 }, { "Frame0/Document", "<Document><Metadata Gps=\"0\"/></Document>" },
                                  { "Frame0/ImageStorage/Image_0.png", p0 } } },
        };
        int oddBad = 0;
        auto compareFile = [&](const std::wstring& path, const char* name) {
            std::vector<SnapFrame> ra, rb;
            XmlNode da("Before"), db("Before");
            bool lA = snapref::LoadSnapFile(path, ra), lB = LoadSnapFile(path, rb);
            bool dA = snapref::LoadSnapDocument(path, da), dB = LoadSnapDocument(path, db);
            bool same = lA == lB && (!lA || sameFrames(ra, rb)) && dA == dB && XmlSerialize(da) == XmlSerialize(db);
            if (!same) printf("       %s: read differently (C++ %s, Rust %s)\n", name, lA ? "ok" : "refused", lB ? "ok" : "refused");
            return same;
        };
        for (auto& [name, streams] : odd) {
            make(streams);
            if (!compareFile(pa, name)) oddBad++;
        }
        SaveSnapFile(pa, docs[1].second);
        std::string good = readAll(pa);
        auto writeBytes = [&](const std::string& b) {
            HANDLE h = CreateFileW(pa.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD wr = 0; WriteFile(h, b.data(), (DWORD)b.size(), &wr, nullptr); CloseHandle(h);
        };
        std::string garbage = good.substr(0, 24);
        for (int i = 0; i < 8192; i++) garbage += (char)((i * 2654435761u) >> 13);
        std::pair<const char*, std::string> bad[] = { { "garbage after the header", garbage }, { "cut in half", good.substr(0, good.size() / 2) },
                                                      { "header only", good.substr(0, 24) }, { "a text file", "not a .snap at all" }, { "empty file", "" } };
        for (auto& [name, bytes] : bad) {
            writeBytes(bytes);
            if (!compareFile(pa, name)) oddBad++;
        }
        DeleteFileW(pa.c_str()); DeleteFileW(pb.c_str());
        if (!compareFile(pa, "missing file")) oddBad++;
        CHECK(!oddBad, "%zu odd, damaged and missing files read alike, %d differ", odd.size() + std::size(bad) + 1, oddBad);

        // a save that cannot open its file leaves nothing behind, in both
        std::wstring noDir = Tmp(L"no_such_dir\\x.snap");
        CHECK(!snapref::SaveSnapFile(noDir, docs[0].second) && !SaveSnapFile(noDir, docs[0].second), "saving into a missing folder fails in both");

        // the user's library: every .snap read alike, frames and documents (only read)
        int files = 0, libBad = 0;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((LibraryDir() + L"\\*.snap").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                files++;
                if (!compareFile(LibraryDir() + L"\\" + fd.cFileName, WToUtf8(fd.cFileName).c_str())) libBad++;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        CHECK(!libBad, "%d .snap files of the library read alike (frames, images, metadata, documents), %d differ", files, libBad);
        SetSnapBackend(def);
    }

    printf("[20] Capture library: Rust and the former C++\n");
    {
        // library.rs must give what Library.cpp gave. The writing parts run in folders of their own (the Rust ones through
        // the C ABI with that folder); the user's library is only read. Not compared: moving to the Recycle Bin
        // (DeleteLibraryItem, the cleanup with recycle) and the daily cleanup, which writes the settings.
        namespace fs = std::filesystem;
        auto take = [](auto call) {
            SnapcoreWStr w{};
            std::wstring r;
            if (call(&w)) { if (w.len) r.assign(w.data, w.len); snapcore_wstr_free(w); }
            return r;
        };
        auto rList = [](const std::wstring& dir, size_t max) {
            std::vector<LibraryItem> v;
            snapcore_library_list(dir.data(), dir.size(), max, +[](void* ctx, const wchar_t* p, size_t n, uint64_t m, uint64_t sz, bool video) {
                LibraryItem it; it.path.assign(p, n);
                it.modified.dwLowDateTime = (DWORD)m; it.modified.dwHighDateTime = (DWORD)(m >> 32);
                it.size = sz; it.video = video;
                static_cast<std::vector<LibraryItem>*>(ctx)->push_back(it);
            }, &v);
            return v;
        };
        auto rName = [&](const std::wstring& dir, const std::wstring& item) {
            return take([&](SnapcoreWStr* o) { return snapcore_library_name(dir.data(), dir.size(), item.data(), item.size(), o); });
        };
        auto rSetName = [](const std::wstring& dir, const std::wstring& item, const std::wstring& name) {
            return snapcore_library_set_name(dir.data(), dir.size(), item.data(), item.size(), name.data(), name.size());
        };
        auto rThumb = [&](const std::wstring& dir, const std::wstring& item) {
            return take([&](SnapcoreWStr* o) { return snapcore_library_thumb_path(dir.data(), dir.size(), item.data(), item.size(), o); });
        };
        auto key = [](const FILETIME& f) { return (uint64_t)f.dwHighDateTime << 32 | f.dwLowDateTime; };
        auto sameItems = [&](const std::vector<LibraryItem>& a, const std::vector<LibraryItem>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); i++)
                if (a[i].path != b[i].path || key(a[i].modified) != key(b[i].modified) || a[i].size != b[i].size || a[i].video != b[i].video) return false;
            return true;
        };
        auto fileBytes = [](const std::wstring& path) {
            std::string b;
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) return std::string("<missing>");
            b.resize(GetFileSize(h, nullptr));
            DWORD rd = 0; ReadFile(h, b.data(), (DWORD)b.size(), &rd, nullptr); CloseHandle(h);
            return b;
        };
        auto putFile = [](const std::wstring& path, const std::string& bytes, uint64_t when) {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD wr = 0; WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wr, nullptr);
            if (when) { FILETIME t{ (DWORD)when, (DWORD)(when >> 32) }; SetFileTime(h, nullptr, nullptr, &t); }
            CloseHandle(h);
        };
        auto setTime = [](const std::wstring& path, uint64_t when) {
            HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            FILETIME t{ (DWORD)when, (DWORD)(when >> 32) }; SetFileTime(h, nullptr, nullptr, &t); CloseHandle(h);
        };
        auto freshDir = [](const wchar_t* name) {
            std::wstring d = Tmp(name);
            std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
            return d;
        };
        // what a folder holds (files and the files of its subfolders), with the contents; for a .snap its size (the
        // compound file records when it was written)
        auto contents = [&](const std::wstring& dir) {
            std::map<std::wstring, std::string> m;
            std::error_code ec;
            for (auto& e : fs::recursive_directory_iterator(dir, ec))
                if (e.is_regular_file())
                    m[fs::relative(e.path(), dir).wstring()] = IsSnapFile(e.path().wstring()) ? std::to_string(e.file_size()) : fileBytes(e.path().wstring());
            return m;
        };
        FILETIME nowFt; GetSystemTimeAsFileTime(&nowFt);
        const uint64_t now = key(nowFt), day = 24ULL * 3600 * 10000000;

        // the program's library folder and the user's library (only read)
        std::wstring lib = LibraryDir();
        int realBad = 0;
        if (lib != libref::LibraryDir()) { realBad++; printf("       LibraryDir differs\n"); }
        {
            auto a = libref::ListLibrary(lib, 1000000), b = ListLibrary(1000000);
            auto order = [&](std::vector<LibraryItem> v) {     // items written in the same 100 ns may come in either order
                std::stable_sort(v.begin(), v.end(), [&](auto& x, auto& y) { return key(x.modified) != key(y.modified) ? key(x.modified) > key(y.modified) : x.path < y.path; });
                return v;
            };
            if (!sameItems(order(a), order(b))) { realBad++; printf("       the library is listed differently\n"); }
            auto a300 = libref::ListLibrary(lib, 300), b300 = ListLibrary(300);
            bool same300 = a300.size() == b300.size();
            for (size_t i = 0; same300 && i < a300.size(); i++) same300 = key(a300[i].modified) == key(b300[i].modified);
            if (!same300) { realBad++; printf("       the 300 most recent differ\n"); }
            int names = 0;
            for (auto& it : b) {
                std::wstring na = libref::CaptureName(lib, it.path), nb = CaptureName(it.path);
                if (!nb.empty()) names++;
                if (na != nb || libref::ThumbPathFor(lib, it.path) != ThumbPathFor(it.path) ||
                    libref::IsInLibrary(lib, it.path) != IsInLibrary(it.path) || !IsInLibrary(it.path)) {
                    realBad++; printf("       %ls: name, thumbnail or place differ\n", it.path.c_str());
                }
            }
            std::wstring va = libref::NewVideoPath(lib), vb = NewVideoPath();
            if (va != vb) { va = libref::NewVideoPath(lib); vb = NewVideoPath(); }      // a second may have turned
            if (va != vb) { realBad++; printf("       NewVideoPath: %ls / %ls\n", va.c_str(), vb.c_str()); }
            // the audio folder only if it is there already (not created in the user's Music folder for the test)
            bool audio = !RegGetString(L"AudioFolder", L"").empty();
            PWSTR music = nullptr;
            if (!audio && SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &music)))
                audio = GetFileAttributesW((std::wstring(music) + L"\\Snapnab").c_str()) != INVALID_FILE_ATTRIBUTES;
            CoTaskMemFree(music);
            if (audio) {
                std::wstring aa = libref::NewAudioPath(true), ab = NewAudioPath(true);
                if (aa != ab) { aa = libref::NewAudioPath(true); ab = NewAudioPath(true); }
                if (libref::AudioDir() != AudioDir() || aa != ab) { realBad++; printf("       the audio folder or file name differ\n"); }
            }
            CHECK(!realBad, "the library folder and its %zu item(s) (%d with a name) read alike: list, names, thumbnails, place, new video%s",
                  b.size(), names, audio ? " and audio" : "");
        }

        // a folder made for the test: what is listed, in which order, how many
        std::wstring t1 = freshDir(L"lib_list");
        const wchar_t* files[] = { L"a.snap", L"B.SNAP", L"c.mp4", L"d.MP4", L"e.txt", L"f g.snap", L"h.sn ap", L"noext",
                                   L".snap", L"x.snap.bak", L"ção 中.snap", L"hidden.snap", L"v.Mp4" };
        for (size_t i = 0; i < std::size(files); i++) putFile(t1 + L"\\" + files[i], std::string(i * 37 + 1, 'x'), now - (i * 7 % 13 + 1) * 3600 * 10000000ULL);
        SetFileAttributesW((t1 + L"\\hidden.snap").c_str(), FILE_ATTRIBUTE_HIDDEN);
        CreateDirectoryW((t1 + L"\\dir.snap").c_str(), nullptr);
        int listBad = 0;
        for (size_t max : { (size_t)1000, (size_t)4, (size_t)0 })
            if (!sameItems(libref::ListLibrary(t1, max), rList(t1, max))) { listBad++; printf("       listed differently (at most %zu)\n", max); }
        auto l = rList(t1, 1000);
        CHECK(!listBad && l.size() == 9, "a folder of odd files listed alike (%zu of %zu: .snap and .mp4, the most recent first)", l.size(), std::size(files) + 1);

        // file names, places, thumbnails and the next automatic name
        int pathBad = 0;
        std::wstring up = t1; for (auto& c : up) c = towupper(c);
        for (const std::wstring& pth : { t1 + L"\\a.snap", up + L"\\a.snap", t1 + L"/a.snap", t1 + L"\\sub\\a.snap", t1,
                                         std::wstring(L"a.snap"), std::wstring(), t1 + L"\\", DirOf(t1) + L"\\x.snap" }) {
            if (libref::IsInLibrary(t1, pth) != snapcore_library_contains(t1.data(), t1.size(), pth.data(), pth.size()) ||
                libref::ThumbPathFor(t1, pth) != rThumb(t1, pth)) { pathBad++; printf("       %ls: place or thumbnail differ\n", pth.c_str()); }
        }
        putFile(t1 + L"\\Snap001.png", "x", 0); putFile(t1 + L"\\Snap002.PNG", "x", 0); putFile(t1 + L"\\Snap1.png", "x", 0);
        CreateDirectoryW((t1 + L"\\Snap003.png").c_str(), nullptr);
        putFile(t1 + L"\\Captura ç_01.jpg", "x", 0);
        for (int digits : { 3, 1, 0, 2, 5, -4 })
            for (const wchar_t* pre : { L"Snap", L"Captura ç_", L"" })
                for (const wchar_t* ext : { L".png", L".jpg", L"" }) {
                    std::wstring a = libref::NextAutoName(t1, pre, ext, digits), b = NextAutoName(t1, pre, ext, digits);
                    if (a != b) { pathBad++; printf("       NextAutoName(%ls, %ls, %d): %ls / %ls\n", pre, ext, digits, a.c_str(), b.c_str()); }
                }
        CHECK(!pathBad, "places, thumbnail paths and automatic names (6 widths, 3 prefixes, 3 extensions) alike");

        // capture names: the same steps in two folders write the same Names.xml and give the same names
        std::wstring na = freshDir(L"lib_names_a"), nb = freshDir(L"lib_names_b");
        std::vector<std::pair<std::wstring, std::wstring>> steps = {
            { L"a.snap", L"  Nome 1 " }, { L"A.SNAP", L"Nome 2" }, { L"ção 中.snap", L"Ç <&> \"x\"" }, { L"z.mp4", L"\"q\"" },
            { L"b.snap", L" " }, { L"Video_1.mp4", L"vídeo\ttab" }, { L"a.snap", L"" }, { L"ÁRVORE.snap", L"árvore" },
        };
        int nameBad = 0;
        auto compareNames = [&](const char* when) {
            bool same = fileBytes(na + L"\\Names.xml") == fileBytes(nb + L"\\Names.xml");
            for (const wchar_t* f : { L"a.snap", L"z.MP4", L"ção 中.snap", L"árvore.snap", L"b.snap", L"Video_1.mp4", L"x\\Video_1.mp4", L"nada" })
                same = same && libref::CaptureName(na, na + L"\\" + f) == rName(nb, nb + L"\\" + f);
            if (!same) { nameBad++; printf("       names differ %s\n", when); }
        };
        for (auto& [f, n] : steps) {
            bool okA = libref::SetCaptureName(na, na + L"\\" + f, n), okB = rSetName(nb, nb + L"\\" + f, n);
            if (okA != okB) nameBad++;
            compareNames("after a change");
        }
        // Names.xml changed by the other program, malformed, missing
        for (const char* xml : { "<Names><Item File=\"Q.snap\" Name=\"Outro\"/><Other File=\"r.snap\" Name=\"x\"/><Item Name=\"sem arquivo\"/><Item File=\"s.snap\"/></Names>",
                                 "<Names><Item File=\"q.snap\" Name=\"meio\"", "" }) {
            Sleep(20);
            putFile(na + L"\\Names.xml", xml, 0); putFile(nb + L"\\Names.xml", xml, 0);
            compareNames("after Names.xml changed");
            bool same = libref::CaptureName(na, L"q.snap") == rName(nb, L"q.snap") && libref::CaptureName(na, L"s.snap") == rName(nb, L"s.snap");
            if (!same) { nameBad++; printf("       %s: read differently\n", xml); }
        }
        DeleteFileW((na + L"\\Names.xml").c_str()); DeleteFileW((nb + L"\\Names.xml").c_str());
        compareNames("after Names.xml was deleted");
        CHECK(!nameBad, "capture names: %zu changes, a Names.xml changed outside, malformed and deleted, alike", steps.size());

        // adding captures: the same document (but for the GUID and the time), the same thumbnail
        std::wstring aa = freshDir(L"lib_add_a"), ab = freshDir(L"lib_add_b");
        auto rAdd = [&](const Image& base, const Image& flat, const CaptureInfo& info, const XmlNode* objects,
                        const std::map<std::string, Image>* embedded) {
            Image thumb = MakeThumbnail(flat, 160, 120);
            std::vector<SnapcoreNamedImage> emb;
            if (embedded) for (auto& [n, im] : *embedded) emb.push_back({ n.data(), n.size(), ImageIn(im) });
            SnapcoreLibraryAdd r{};
            r.dir = ab.data(); r.dirLen = ab.size();
            r.base = ImageIn(base); r.thumb = ImageIn(thumb);
            r.mode = info.mode.c_str(); r.windowTitle = info.windowTitle.c_str(); r.application = info.application.c_str();
            r.objects = objects ? XmlToSnapcore(*objects) : nullptr;
            r.embedded = emb.data(); r.embeddedCount = emb.size();
            r.version = kAppVersion;
            return take([&](SnapcoreWStr* o) { return snapcore_library_add(&r, o); });
        };
        auto sameAdded = [&](const std::wstring& pa, const std::wstring& pb) {
            if (pa.empty() || pb.empty()) return pa.empty() && pb.empty();
            std::wstring fa = FileNameOf(pa), fb = FileNameOf(pb);
            if (DirOf(pa) != aa || DirOf(pb) != ab || fa.size() != 41 || fb.size() != 41 || FileExt(fb) != L".snap" || fa == fb) return false;
            std::vector<SnapFrame> x, y;
            if (!LoadSnapFile(pa, x) || !LoadSnapFile(pb, y) || x.size() != 1 || y.size() != 1) return false;
            std::string ca = x[0].doc.get("Created"), cb = y[0].doc.get("Created");
            if (ca.size() != 19 || cb.size() != 19 || ca[10] != 'T' || cb[10] != 'T') return false;
            x[0].doc.set("Created", ""); y[0].doc.set("Created", "");
            if (XmlSerialize(x[0].doc) != XmlSerialize(y[0].doc) || x[0].base.px != y[0].base.px || x[0].embedded.size() != y[0].embedded.size()) return false;
            for (auto& [k, v] : x[0].embedded) if (!y[0].embedded.count(k) || y[0].embedded.at(k).px != v.px) return false;
            Image ta, tb;
            bool hasA = LoadImageFile(libref::ThumbPathFor(aa, pa), ta), hasB = LoadImageFile(rThumb(ab, pb), tb);
            return hasA == hasB && ta.w == tb.w && ta.h == tb.h && ta.px == tb.px;
        };
        int addBad = 0;
        CaptureInfo info{ L"Região", L"Título — ç 中 <&>", L"app.exe" };
        Image photo = Pattern(500, 300, 31);
        auto meta = std::make_shared<ImageMeta>(); meta->make = L"Câmera"; meta->gps = true; meta->lat = 1.03125; meta->lon = -2.5;
        photo.meta = meta;
        Image tall = Pattern(90, 200, 32), dot(1, 1, 0x80FFFFFF);
        for (const Image* img : { &photo, &tall, &dot }) {
            if (!sameAdded(libref::AddImageToLibrary(aa, *img, info), rAdd(*img, *img, info, nullptr, nullptr))) { addBad++; printf("       an image %dx%d added differently\n", img->w, img->h); }
        }
        XmlNode objs("Objects");
        objs.add("Image").set("StreamName", "Image_1.png").seti("X", 5);
        objs.add("Group").add("Image").set("StreamName", "Image_2.png");
        std::map<std::string, Image> emb = { { "Image_1.png", Pattern(30, 20, 33) }, { "Image_2.png", Pattern(8, 8, 34) } };
        CaptureInfo info2{ L"Janela", L"", L"" };
        if (!sameAdded(libref::AddLayeredToLibrary(aa, Pattern(300, 200, 35), Pattern(300, 200, 36), info2, objs, emb),
                       rAdd(Pattern(300, 200, 35), Pattern(300, 200, 36), info2, &objs, &emb))) { addBad++; printf("       a layered capture added differently\n"); }
        XmlNode noObjs("Objects");
        if (!sameAdded(libref::AddLayeredToLibrary(aa, Pattern(30, 20, 37), Image(), info2, noObjs, {}),
                       rAdd(Pattern(30, 20, 37), Image(), info2, &noObjs, nullptr))) { addBad++; printf("       a capture without a thumbnail added differently\n"); }
        if (!sameAdded(libref::AddImageToLibrary(aa, Image(), info), rAdd(Image(), Image(), info, nullptr, nullptr))) { addBad++; printf("       an empty image is not refused alike\n"); }
        auto added = rList(ab, 100);
        CHECK(!addBad && added.size() == 5, "%zu captures added alike (photo metadata, objects and embedded images, no thumbnail, an empty one refused)", added.size());

        // the cleanup (deleting, not to the Recycle Bin): old, new and future captures, named, tagged, videos, with and
        // without thumbnails, for every choice of what is kept
        auto fill = [&](const std::wstring& dir, bool rust) {
            auto snapWith = [&](const wchar_t* name, const char* tags, uint64_t when) {
                SnapFrame f; f.base = Pattern(12, 9, 40);
                if (tags) f.doc.set("Tags", tags);
                std::wstring pth = dir + L"\\" + name;
                SaveSnapFile(pth, { f });
                setTime(pth, when);
                putFile(rust ? rThumb(dir, pth) : libref::ThumbPathFor(dir, pth), "thumb", 0);
                return pth;
            };
            uint64_t old = now - 10 * day, recent = now - day / 2, future = now + 3 * day;
            std::wstring named = snapWith(L"old_named.snap", nullptr, old);
            snapWith(L"old_tagged.snap", "trabalho", old);
            snapWith(L"old_blank_tags.snap", "  \"\" ", old);
            snapWith(L"old_plain.snap", nullptr, old);
            snapWith(L"recent.snap", nullptr, recent);
            snapWith(L"future.snap", nullptr, future);
            putFile(dir + L"\\Video_old.mp4", "mp4", old);
            putFile(dir + L"\\Video_named.mp4", "mp4", old);
            putFile(dir + L"\\old_damaged.snap", std::string("\xCB" "SNAPNAB") + std::string(100, 'x'), old);
            putFile(dir + L"\\notes.txt", "keep", old);
            if (rust) { rSetName(dir, named, L"Importante"); rSetName(dir, dir + L"\\Video_named.mp4", L"Vídeo"); rSetName(dir, dir + L"\\gone.snap", L"Sem arquivo"); }
            else { libref::SetCaptureName(dir, named, L"Importante"); libref::SetCaptureName(dir, dir + L"\\Video_named.mp4", L"Vídeo"); libref::SetCaptureName(dir, dir + L"\\gone.snap", L"Sem arquivo"); }
        };
        int cleanBad = 0, cases = 0;
        for (int days : { 30, 5, 0, -1 })
            for (bool keepNamed : { true, false })
                for (bool keepTagged : { true, false }) {
                    std::wstring ca = freshDir(L"lib_clean_a"), cb = freshDir(L"lib_clean_b");
                    fill(ca, false); fill(cb, true);
                    int ra = libref::CleanupLibrary(ca, days, keepNamed, keepTagged, false);
                    int rb = snapcore_library_cleanup(cb.data(), cb.size(), days, keepNamed, keepTagged, false);
                    cases++;
                    if (ra != rb || contents(ca) != contents(cb)) {
                        cleanBad++;
                        printf("       %d days, keep named %d, tagged %d: %d / %d removed%s\n", days, keepNamed, keepTagged, ra, rb,
                               ra == rb ? ", different files left" : "");
                    }
                }
        CHECK(!cleanBad, "the cleanup alike in %d cases (days, keeping the named and the tagged ones): what is removed, what stays, Names.xml", cases);

        std::error_code ec;
        for (const wchar_t* d : { L"lib_list", L"lib_names_a", L"lib_names_b", L"lib_add_a", L"lib_add_b", L"lib_clean_a", L"lib_clean_b" })
            fs::remove_all(Tmp(d), ec);
    }

    printf("[21] Animated GIF: Rust and the former C++\n");
    {
        // the palette sort first: the MSVC std::sort, equal elements included, on the inputs the median cut gives it
        int sortCases = 0, sortBad = 0;
        uint32_t seed = 2463534242u;
        auto rnd = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
        for (size_t n : { 0, 1, 2, 3, 31, 32, 33, 40, 41, 42, 63, 100, 257, 1000, 4096, 40000, 120000 })
            for (int kind = 0; kind < 7; kind++)
                for (uint32_t shift : { 0u, 8u, 16u }) {
                    std::vector<uint32_t> v(n);
                    for (size_t i = 0; i < n; i++) {
                        uint32_t r = rnd() & 0xFFFFFF, key;
                        switch (kind) {
                        case 0: key = rnd() & 255; break;                      // random
                        case 1: key = rnd() % 3 * 100; break;                  // few distinct values
                        case 2: key = 42; break;                               // all equal
                        case 3: key = (uint32_t)(i * 255 / std::max<size_t>(1, n)); break;          // sorted
                        case 4: key = 255 - (uint32_t)(i * 255 / std::max<size_t>(1, n)); break;    // reversed
                        case 5: key = (uint32_t)(i < n / 2 ? i : n - i) % 256; break;               // organ pipe
                        default: key = (uint32_t)(i % 7 == 0 ? 255 : i % 2); break;                 // nearly two values
                        }
                        v[i] = (r & ~(255u << shift)) | (key << shift);
                    }
                    auto lt = [shift](uint32_t x, uint32_t y) { return ((x >> shift) & 255) < ((y >> shift) & 255); };
                    std::vector<uint32_t> a = v, b = v, c = v, d = v;
                    std::sort(a.begin(), a.end(), lt);
                    snapcore_sort_by_byte(b.data(), b.size(), shift, false);
                    std::make_heap(c.begin(), c.end(), lt); std::sort_heap(c.begin(), c.end(), lt);   // the introsort's fallback
                    snapcore_sort_by_byte(d.data(), d.size(), shift, true);
                    sortCases++;
                    if (a != b || c != d) { sortBad++; printf("       %zu elements, kind %d, shift %u: %s differently\n", n, kind, shift, a != b ? "sorted" : "heap sorted"); }
                }
        CHECK(!sortBad, "the palette sort orders %d inputs as std::sort does (and its heap sort as make_heap/sort_heap), equal elements included", sortCases);

        // GIFs written by both: sizes, 4 and 8 bpp, dithering, delta frames, frame rates, delays, comments, frames of
        // another size, transparency, identical frames, a file over the 1 MB buffer
        auto readAll = [](const std::wstring& path) {
            std::string b;
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) return std::string("<missing>");
            b.resize(GetFileSize(h, nullptr));
            DWORD rd = 0; ReadFile(h, b.data(), (DWORD)b.size(), &rd, nullptr); CloseHandle(h);
            return b;
        };
        auto writeNew = [](const std::wstring& path, int w, int h, double fps, int bpp, bool dither, bool delta,
                           const std::vector<std::pair<Image, int>>& frames, const std::string& comment) {
            GifWriter g;
            if (!g.Open(path, w, h, fps, bpp, dither, delta, comment)) return false;
            bool ok = true;
            for (auto& [img, delay] : frames) ok = g.AddFrame(img, delay) && ok;
            return g.Close() && ok && !g.failed();
        };
        std::wstring ga = Tmp(L"ref_a.gif"), gb = Tmp(L"ref_b.gif");
        int gifs = 0, gifBad = 0;
        size_t biggest = 0;
        auto compare = [&](const char* what, int w, int h, double fps, int bpp, bool dither, bool delta,
                           const std::vector<std::pair<Image, int>>& frames, const std::string& comment) {
            DeleteFileW(ga.c_str()); DeleteFileW(gb.c_str());
            bool okA = gifref::WriteGif(ga, w, h, fps, bpp, dither, delta, frames, comment);
            bool okB = writeNew(gb, w, h, fps, bpp, dither, delta, frames, comment);
            std::string a = readAll(ga), b = readAll(gb);
            gifs++;
            biggest = std::max(biggest, b.size());
            if (okA != okB || a != b) {
                gifBad++;
                size_t at = 0; while (at < a.size() && at < b.size() && a[at] == b[at]) at++;
                printf("       %s (%dx%d, %d bpp%s%s, %.1f fps): %s at byte %zu of %zu/%zu\n", what, w, h, bpp, dither ? ", dithering" : "",
                       delta ? ", delta" : "", fps, okA != okB ? "written by one only" : "different bytes", at, a.size(), b.size());
            }
        };
        auto moving = [](int w, int h, int n, int seed) {      // a moving block over a pattern, with a still frame
            std::vector<std::pair<Image, int>> f;
            Image bg = Pattern(w, h, seed);
            for (int i = 0; i < n; i++) {
                Image im = bg;
                for (int y = h / 4; y < h / 2; y++)
                    for (int x = (i * 7) % std::max(1, w / 2); x < std::min(w, (i * 7) % std::max(1, w / 2) + w / 3); x++)
                        im.at(x, y) = ARGB(255, (i * 40) & 255, 200, 255 - (i * 30 & 255));
                f.push_back({ im, i == 2 ? 50 : -1 });
                if (i == 1) f.push_back({ im, -1 });            // the same frame again
            }
            return f;
        };
        for (int bpp : { 4, 8 })
            for (bool dither : { false, true })
                for (bool delta : { false, true }) {
                    compare("moving block", 120, 90, 10, bpp, dither, delta, moving(120, 90, 5, 1), "Snapnab");
                    compare("1x1", 1, 1, 30, bpp, dither, delta, { { Image(1, 1, 0xFF00FF00), -1 }, { Image(1, 1, 0xFFFF0000), 3 } }, "");
                    compare("odd size", 37, 21, 7.5, bpp, dither, delta, moving(37, 21, 4, 2), std::string(600, 'c'));
                }
        Image alpha = Pattern(64, 48, 3);
        for (size_t i = 0; i < alpha.px.size(); i++) alpha.px[i] = (alpha.px[i] & 0xFFFFFF) | ((uint32_t)(i * 5 % 256) << 24);
        compare("transparency", 64, 48, 0, 8, true, true, { { alpha, -1 }, { Pattern(64, 48, 4), -1 }, { alpha, 0 } }, "Snapnab");
        compare("frames of another size", 100, 80, 1, 8, true, false, { { Pattern(300, 200, 5), 100 }, { Pattern(50, 30, 6), 100 } }, "Snapnab");
        compare("frame rates", 50, 40, 100, 4, false, true, moving(50, 40, 3, 7), "Snapnab");
        compare("frame rates", 50, 40, 0.3, 4, false, true, moving(50, 40, 3, 8), "Snapnab");
        compare("over 1 MB", 640, 480, 15, 8, true, false, moving(640, 480, 6, 9), "Snapnab");
        compare("noise (LZW dictionary resets)", 400, 300, 10, 8, false, true, { { Pattern(400, 300, 10), -1 }, { Pattern(400, 300, 11), -1 } }, "x");
        // real images: the library's thumbnails as the frames of a GIF (only read)
        {
            std::vector<std::pair<Image, int>> thumbs;
            WIN32_FIND_DATAW fd;
            std::wstring dir = LibraryDir() + L"\\Thumbnails";
            HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do { Image t; if (LoadImageFile(dir + L"\\" + fd.cFileName, t)) thumbs.push_back({ t, 20 }); } while (thumbs.size() < 40 && FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (!thumbs.empty())
                for (int bpp : { 4, 8 })
                    for (bool delta : { false, true }) compare("library thumbnails", 160, 120, 5, bpp, true, delta, thumbs, "Snapnab");
        }
        CHECK(!gifBad, "%d GIFs written byte for byte the same (the largest %zu bytes), %d differ", gifs, biggest, gifBad);

        std::wstring noDir = Tmp(L"no_such_dir\\x.gif");
        GifWriter g;
        CHECK(!gifref::WriteGif(noDir, 10, 10, 10, 8, false, false, {}, "") && !g.Open(noDir, 10, 10, 10, 8, false, false) && g.failed() &&
              !g.AddFrame(Image(10, 10, 0)) && !g.Close(), "a GIF that cannot be created fails in both");
        DeleteFileW(ga.c_str()); DeleteFileW(gb.c_str());
    }

    printf("[22] WebP: libwebp in Rust and the former C++ build\n");
    {
        std::vector<std::string> want;
        std::ifstream golden(ExeDir() + L"\\..\\..\\..\\..\\tests\\data\\webp-libwebp-1.5.0.txt");
        for (std::string l; std::getline(golden, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) want.push_back(l);
        }
        std::vector<std::string> got = WebPFingerprints();
        int differ = 0;
        for (size_t i = 0; i < got.size(); i++)
            if (i >= want.size() || got[i] != want[i]) {
                differ++;
                printf("       %s (was %s)\n", got[i].c_str(), i < want.size() ? want[i].c_str() : "missing");
            }
        CHECK(want.size() == 42 && got.size() == want.size() && !differ,
              "%zu WebP files and their decoded pixels the same as libwebp 1.5.0 built by CMake, %d differ", got.size(), differ);

        const std::wstring invalid = Tr(L"Arquivo WebP inválido.");
        std::vector<BYTE> enc;
        Image cut(3, 3, 0xFF112233);
        bool okEnc = EncodeToMemoryEx(Pattern(37, 21, 1), ImageFormat::WEBP, enc, SaveOptions()) && enc.size() > 40;
        bool okCut = okEnc && DecodeFromMemory(enc.data(), 40, cut);
        CHECK(okEnc && !okCut && ImageIoError() == invalid && cut.empty(), "a cut WebP is refused with its message, and the image cleared");
        const char junk[] = "RIFF\0\0\0\0WEBPVP8 nothing";
        Image keep(2, 2, 0xFF112233);
        bool okJunk = DecodeFromMemory((const BYTE*)junk, sizeof junk - 1, keep);
        CHECK(!okJunk && ImageIoError() == invalid && keep.w == 2, "a file that only looks like WebP is refused with its message");
        std::wstring wp = Tmp(L"not_really.webp");
        { std::ofstream f(wp, std::ios::binary); f.write(junk, sizeof junk - 1); }
        Image fromFile;
        CHECK(!LoadImageFile(wp, fromFile) && ImageIoError() == invalid, "and so is such a file");
        DeleteFileW(wp.c_str());
        std::vector<BYTE> tooWide;
        CHECK(!EncodeToMemoryEx(Image(16384, 1, 0xFF000000), ImageFormat::WEBP, tooWide, SaveOptions()) &&
              ImageIoError() == Tr(L"Falha ao codificar WebP."), "an image wider than WebP allows fails with its message");
    }

    printf("[23] Effects, Beautify, scaling, computer vision and PDF in Rust against the former C++ code\n");
    {
        std::vector<std::string> want;
        std::ifstream golden(ExeDir() + L"\\..\\..\\..\\..\\tests\\data\\pixels-cpp.txt");
        for (std::string l; std::getline(golden, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) want.push_back(l);
        }
        std::vector<std::string> got = PixelFingerprints(TestTempDir());
        int differ = 0;
        for (size_t i = 0; i < got.size(); i++)
            if (i >= want.size() || got[i] != want[i]) {
                differ++;
                printf("       %s (was %s)\n", got[i].c_str(), i < want.size() ? want[i].c_str() : "missing");
            }
        CHECK(want.size() > 800 && got.size() == want.size() && !differ,
              "%zu pixel results the same as the C++ code's, %d differ", got.size(), differ);
    }

    // the lines of a golden file next to what this build gives
    auto compareGolden = [](const wchar_t* file, const std::vector<std::string>& got, int& differ) {
        std::vector<std::string> want;
        std::ifstream golden(ExeDir() + L"\\..\\..\\..\\..\\tests\\data\\" + file);
        for (std::string l; std::getline(golden, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) want.push_back(l);
        }
        differ = 0;
        for (size_t i = 0; i < std::max(got.size(), want.size()); i++)
            if (i >= want.size() || i >= got.size() || got[i] != want[i]) {
                differ++;
                printf("       %s (was %s)\n", i < got.size() ? got[i].c_str() : "missing", i < want.size() ? want[i].c_str() : "missing");
            }
        return want.size();
    };

    printf("[24] QR codes and barcodes against the former ZXing-C++ reader\n");
    {
        // each code: "[format|text|left,top,right,bottom|url]"; the box rxing gives is where it found the code (a 1D
        // code: the row it read, start and stop characters included), so it has to overlap the one ZXing-C++ gave
        struct Code { std::string key; RECT box; };
        auto parse = [](const std::string& line) {
            std::vector<Code> v;
            for (size_t a = line.find(" ["); a != std::string::npos; a = line.find(" [", a + 1)) {
                size_t e = line.find(']', a);
                std::string f = line.substr(a + 2, e - a - 2);
                size_t p1 = f.find('|'), p2 = f.find('|', p1 + 1), p3 = f.rfind('|');
                Code c{ f.substr(0, p2) + f.substr(p3), {} };
                sscanf_s(f.substr(p2 + 1, p3 - p2 - 1).c_str(), "%ld,%ld,%ld,%ld", &c.box.left, &c.box.top, &c.box.right, &c.box.bottom);
                v.push_back(c);
            }
            return v;
        };
        std::vector<std::string> want;
        std::ifstream golden(ExeDir() + L"\\..\\..\\..\\..\\tests\\data\\barcodes.txt");
        for (std::string l; std::getline(golden, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) want.push_back(l);
        }
        auto imgs = BarcodeImages();
        std::vector<std::string> got = BarcodeResults(imgs);
        int differ = 0, boxes = 0, codes = 0;
        for (size_t i = 0; i < want.size(); i++) {
            auto w = parse(want[i]), g = i < got.size() ? parse(got[i]) : std::vector<Code>{};
            bool same = w.size() == g.size();
            for (size_t k = 0; same && k < w.size(); k++) {
                same = w[k].key == g[k].key;
                RECT x;
                if (same && !IntersectRect(&x, &w[k].box, &g[k].box)) boxes++;
            }
            codes += (int)w.size();
            if (!same) { differ++; printf("       %s (was %s)\n", i < got.size() ? got[i].c_str() : "missing", want[i].c_str()); }
        }
        CHECK(want.size() == 23 && imgs.size() == want.size() && got.size() == want.size() && !differ && !boxes && codes == 23,
              "%zu images: the %d codes ZXing-C++ read, read alike (%d differ, %d boxes elsewhere)", want.size(), codes, differ, boxes);
    }

    printf("[25] Color texts, HDR tone mapping and OCR against the former C++ code\n");
    {
        int differ = 0;
        size_t n = compareGolden(L"misc-cpp.txt", MiscFingerprints(), differ);
        CHECK(n > 100 && !differ, "%zu results the same as the C++ code's, %d differ", n, differ);
    }

    printf("[26] Editor objects drawn as the former C++ painters drew them\n");
    {
        int differ = 0;
        size_t n = compareGolden(L"render-cpp.txt", RenderFingerprints(), differ);
        CHECK(n > 600 && !differ, "%zu drawings and measures the same as the C++ code's, %d differ", n, differ);
    }

    printf("[27] Editor objects saved, read and hit-tested as the former C++ code did\n");
    {
        int differ = 0;
        size_t n = compareGolden(L"doc-cpp.txt", DocFingerprints(), differ);
        CHECK(n > 200 && !differ, "%zu objects, files and hit tests the same as the C++ code's, %d differ", n, differ);
    }

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL OK", g_fail);
    CoUninitialize();
    return g_fail ? 1 : 0;
}
