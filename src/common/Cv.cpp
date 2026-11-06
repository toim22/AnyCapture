// In-house computer vision: the work is done by snapcore (rust/snapcore/src/cv.rs); these are the C++ entry points.
#include "Cv.h"
#include "SnapcoreBridge.h"

namespace snap::cv {

namespace {
SnapcoreRect R(RECT r) { return { r.left, r.top, r.right, r.bottom }; }
}

Mask WandSelect(const Image& img, POINT seed, float tolerance, bool global) {
    Mask m((size_t)std::max(0, img.w) * std::max(0, img.h), 0);
    SnapcoreImageIn in = ImageIn(img);
    snapcore_cv_wand(&in, seed.x, seed.y, tolerance, global, false, m.data());
    return m;
}

Mask DoWand(const Image& img, POINT pt, float tol, bool global) {
    Mask m((size_t)std::max(0, img.w) * std::max(0, img.h), 0);
    SnapcoreImageIn in = ImageIn(img);
    if (!snapcore_cv_wand(&in, pt.x, pt.y, tol, global, true, m.data())) return {};   // outside the image
    return m;
}

Mask RectMask(int w, int h, RECT r) {
    Mask m((size_t)std::max(0, w) * std::max(0, h), 0);
    snapcore_cv_shape_mask(0, w, h, R(r), nullptr, 0, m.data());
    return m;
}

Mask EllipseMask(int w, int h, RECT r) {
    Mask m((size_t)std::max(0, w) * std::max(0, h), 0);
    snapcore_cv_shape_mask(1, w, h, R(r), nullptr, 0, m.data());
    return m;
}

Mask PolygonMask(int w, int h, const std::vector<POINT>& pts) {
    Mask m((size_t)std::max(0, w) * std::max(0, h), 0);
    std::vector<int32_t> xy;
    for (auto& p : pts) { xy.push_back(p.x); xy.push_back(p.y); }
    snapcore_cv_shape_mask(2, w, h, SnapcoreRect{}, xy.data(), pts.size(), m.data());
    return m;
}

RECT MaskBounds(const Mask& m, int w, int h) {
    if (m.size() < (size_t)std::max(0, w) * std::max(0, h)) return {};
    SnapcoreRect r = snapcore_cv_mask_bounds(m.data(), w, h);
    return { r.left, r.top, r.right, r.bottom };
}

bool MaskEmpty(const Mask& m) { return snapcore_cv_mask_empty(m.data(), m.size()); }

Mask Dilate(const Mask& m, int w, int h, int k) {
    Mask d((size_t)std::max(0, w) * std::max(0, h), 0);
    if (m.size() >= d.size()) snapcore_cv_dilate(m.data(), w, h, k, d.data());
    return d;
}

bool Dingo7Inpaint(Image& img, const Mask& mask, int kernel, uint32_t* dominantOut, double* shareOut) {
    if (img.empty() || mask.size() != img.px.size()) return false;
    return snapcore_cv_inpaint(0, img.px.data(), img.w, img.h, mask.data(), kernel, dominantOut, shareOut);
}

void DiffusionInpaint(Image& img, const Mask& mask, int iterations) {
    if (img.empty() || mask.size() != img.px.size()) return;
    snapcore_cv_inpaint(1, img.px.data(), img.w, img.h, mask.data(), iterations, nullptr, nullptr);
}

void SmartFill(Image& img, const Mask& mask) {
    if (img.empty() || mask.size() != img.px.size()) return;
    snapcore_cv_inpaint(2, img.px.data(), img.w, img.h, mask.data(), 0, nullptr, nullptr);
}

float SmartMoveAutoTolerance(const Image& img) {
    SnapcoreImageIn in = ImageIn(img);
    return snapcore_cv_auto_tolerance(&in);
}

float SmartMoveToleranceScaling(float t, float inMin, float inMax, float outMin, float outMax) {
    return snapcore_cv_tolerance_scaling(t, inMin, inMax, outMin, outMax);
}

POINT DetectTranslation(const Image& a, const Image& b, double* confidence) {
    SnapcoreImageIn ia = ImageIn(a), ib = ImageIn(b);
    int32_t off[2] = { 0, 0 };
    snapcore_cv_detect_translation(&ia, &ib, off, confidence);
    return { off[0], off[1] };
}

} // namespace snap::cv
