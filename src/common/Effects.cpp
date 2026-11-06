// Effects and filters: the pixel work is done by snapcore (rust/snapcore/src/fx.rs); these are the C++ entry points.
#include "Effects.h"
#include "SnapcoreBridge.h"

namespace snap::fx {

namespace {

SnapcoreFx Op(int op, int i0 = 0, int i1 = 0, int i2 = 0, int i3 = 0) {
    SnapcoreFx a{};
    a.op = op;
    a.i[0] = i0; a.i[1] = i1; a.i[2] = i2; a.i[3] = i3;
    return a;
}

SnapcoreRect R(RECT r) { return { r.left, r.top, r.right, r.bottom }; }

// an effect that makes a new image
Image Run(const Image& s, const SnapcoreFx& a) {
    SnapcoreImageIn in = ImageIn(s);
    return TakeImage(snapcore_fx(&in, &a));
}

// an effect that changes the pixels in place
void RunInPlace(Image& img, const SnapcoreFx& a) {
    if (!img.empty()) snapcore_fx_in_place(img.px.data(), img.w, img.h, &a);
}

} // namespace

Image Rotate90(const Image& s, bool cw) { return Run(s, Op(0, cw)); }
Image Rotate180(const Image& s) { return Run(s, Op(1)); }
Image FlipH(const Image& s) { return Run(s, Op(2)); }
Image FlipV(const Image& s) { return Run(s, Op(3)); }

Image RotateAngle(const Image& s, double deg, uint32_t bg) {
    SnapcoreFx a = Op(4);
    a.d = deg; a.c[0] = bg;
    return Run(s, a);
}

Image CanvasResize(const Image& s, int nw, int nh, int anchor, uint32_t bg) {
    SnapcoreFx a = Op(5, nw, nh, anchor);
    a.c[0] = bg;
    return Run(s, a);
}

Image AutoTrim(const Image& s, int tol) { return Run(s, Op(6, tol)); }
Image Scale(const Image& s, int percent) { return Run(s, Op(7, percent)); }

void Grayscale(Image& img) { RunInPlace(img, Op(0)); }
void Invert(Image& img) { RunInPlace(img, Op(1)); }
void Sepia(Image& img) { RunInPlace(img, Op(2)); }
void Monochrome(Image& img, int threshold) { RunInPlace(img, Op(3, threshold)); }
void BrightnessContrast(Image& img, int b, int c) { RunInPlace(img, Op(4, b, c)); }
void HueSaturation(Image& img, int hue, int sat) { RunInPlace(img, Op(5, hue, sat)); }

void Gamma(Image& img, double g) {
    SnapcoreFx a = Op(6);
    a.d = g;
    RunInPlace(img, a);
}

void ReplaceColor(Image& img, uint32_t from, uint32_t to, int tol) {
    SnapcoreFx a = Op(7, tol);
    a.c[0] = from; a.c[1] = to;
    RunInPlace(img, a);
}

void BoxBlur(Image& img, RECT r, int radius) {
    SnapcoreFx a = Op(8, radius);
    a.rect = R(r);
    RunInPlace(img, a);
}

void Pixelate(Image& img, RECT r, int block) {
    SnapcoreFx a = Op(9, block);
    a.rect = R(r);
    RunInPlace(img, a);
}

Image Border(const Image& s, int width, uint32_t color) {
    SnapcoreFx a = Op(8, width);
    a.c[0] = color;
    return Run(s, a);
}

Image Shadow(const Image& s, int offset, int blur, int opacity) { return Run(s, Op(9, offset, blur, opacity)); }

Image EdgeEffect(const Image& s, Edge style, int size, int sides, bool shadow) {
    return Run(s, Op(10, (int)style, size, sides, shadow));
}

Image Perspective(const Image& s, int h, int v, int shear) { return Run(s, Op(11, h, v, shear)); }
Image PageCurl(const Image& s, int size, int corner) { return Run(s, Op(12, size, corner)); }

Image Spotlight(const Image& s, RECT f, int dim, int blur, int zoom, bool shadow) {
    SnapcoreFx a = Op(13, dim, blur, zoom, shadow);
    a.rect = R(f);
    return Run(s, a);
}

void Emboss(Image& img, int depth) { RunInPlace(img, Op(10, depth)); }
void Sharpen(Image& img, int amount) { RunInPlace(img, Op(11, amount)); }

void Watermark(Image& img, const std::wstring& text, int opacity, bool tiled, bool emboss) {
    SnapcoreFx a = Op(12, opacity, tiled, emboss);
    a.text = text.data(); a.textLen = text.size();
    RunInPlace(img, a);
}

std::wstring ExpandCaptionTokens(const std::wstring& text) {
    SnapcoreWStr s{};
    if (!snapcore_fx_expand_tokens(text.data(), text.size(), &s)) return text;
    std::wstring r = s.len ? std::wstring(s.data, s.len) : std::wstring();
    snapcore_wstr_free(s);
    return r;
}

Image Caption(const Image& s, const std::wstring& text, bool top, int fontPx, uint32_t fg, uint32_t bg) {
    SnapcoreFx a = Op(14, top, fontPx);
    a.c[0] = fg; a.c[1] = bg;
    a.text = text.data(); a.textLen = text.size();
    return Run(s, a);
}

Image ApplyCaptureEffects(const Image& s, const CaptureEffects& e) {
    SnapcoreCaptureEffects c{};
    c.border = e.border; c.borderWidth = e.borderWidth; c.borderColor = e.borderColor;
    c.filter = e.filter; c.filterKind = e.filterKind;
    c.shadow = e.shadow; c.shadowOffset = e.shadowOffset; c.shadowBlur = e.shadowBlur; c.shadowOpacity = e.shadowOpacity;
    c.caption = e.caption; c.captionText = e.captionText.data(); c.captionTextLen = e.captionText.size();
    c.captionTop = e.captionTop;
    c.replaceColor = e.replaceColor; c.replaceFrom = e.replaceFrom; c.replaceTo = e.replaceTo;
    c.replaceTolerance = e.replaceTolerance;
    c.scale = e.scale; c.scalePercent = e.scalePercent;
    c.edge = e.edge; c.edgeStyle = e.edgeStyle; c.edgeSize = e.edgeSize; c.edgeSides = e.edgeSides;
    c.trim = e.trim; c.trimTolerance = e.trimTolerance;
    c.adjust = e.adjust; c.brightness = e.brightness; c.contrast = e.contrast; c.hue = e.hue; c.saturation = e.saturation;
    c.rotate = e.rotate; c.rotateKind = e.rotateKind;
    c.watermark = e.watermark; c.watermarkText = e.watermarkText.data(); c.watermarkTextLen = e.watermarkText.size();
    c.watermarkOpacity = e.watermarkOpacity; c.watermarkTiled = e.watermarkTiled;
    c.resolution = e.resolution; c.dpi = e.dpi;
    c.beautify = e.beautify; c.beautifyOpt = ToSnapcore(e.beautifyOpt);
    SnapcoreImageIn in = ImageIn(s);
    return TakeImage(snapcore_capture_effects(&in, &c));
}

} // namespace snap::fx
