// Object painters: the drawing lives in snapcore (rust/snapcore/src/render.rs: each object on its own layer, with its
// shadow and opacity). These wrappers hand it the objects (SnapcoreObj, pointers into the DrawObj) and the GDI+ objects
// by their native pointers, so the callers keep the same API.
#include "ObjBridge.h"

namespace snap::ed {

using namespace Gdiplus;

Color Gc(uint32_t argb, int opacity) {
    int a = (int)((argb >> 24) & 255) * std::clamp(opacity, 0, 100) / 100;
    return Color((BYTE)a, (BYTE)((argb >> 16) & 255), (BYTE)((argb >> 8) & 255), (BYTE)(argb & 255));
}

namespace {

// the native object behind a Gdiplus class: its protected member, read through a member pointer formed in a derived class
struct GraphicsNative : Graphics {
    static GpGraphics* Of(const Graphics& g) { return g.*(&GraphicsNative::nativeGraphics); }
};
struct PathNative : GraphicsPath {
    static GpPath* Of(const GraphicsPath& p) { return p.*(&PathNative::nativePath); }
};
struct PenNative : Pen {
    static GpPen* Of(const Pen& p) { return p.*(&PenNative::nativePen); }
};
// a brush snapcore made, deleted by the Brush destructor
class NativeBrush : public Brush {
public:
    explicit NativeBrush(GpBrush* b) : Brush(b, Ok) {}
};

static_assert(sizeof(PointF) == sizeof(SnapcorePointF) && sizeof(RectF) == sizeof(SnapcoreRectF) &&
              sizeof(GradStop) == sizeof(SnapcoreGradStop) && sizeof(RECT) == sizeof(SnapcoreRect));

SnapcoreRectF R(RectF r) { return { r.X, r.Y, r.Width, r.Height }; }
RectF FromC(SnapcoreRectF r) { return RectF(r.x, r.y, r.w, r.h); }

SnapcoreGradient GradC(const Gradient& g) {
    return { g.type, g.angle, reinterpret_cast<const SnapcoreGradStop*>(g.stops.data()), g.stops.size() };
}

} // namespace

ObjC::ObjC(const DrawObj& o) {
    c.type = (int32_t)o.type;
    c.p1 = { o.p1.X, o.p1.Y };
    c.p2 = { o.p2.X, o.p2.Y };
    c.tail = { o.tail.X, o.tail.Y };
    c.ctrl = { o.ctrl.X, o.ctrl.Y };
    c.pts = reinterpret_cast<const SnapcorePointF*>(o.pts.data());
    c.ptsCount = o.pts.size();
    c.text = o.text.c_str();
    c.stepValue = o.stepValue;
    const Style& s = o.style;
    SnapcoreStyle& t = c.style;
    t.stroke = s.stroke; t.fill = s.fill; t.textColor = s.textColor; t.width = s.width; t.opacity = s.opacity; t.shadow = s.shadow;
    t.shadowDir = s.shadowDir; t.shadowColor = s.shadowColor; t.shadowOpacity = s.shadowOpacity; t.shadowBlur = s.shadowBlur;
    t.shadowDist = s.shadowDist; t.font = s.font.c_str(); t.fontSize = s.fontSize; t.bold = s.bold; t.italic = s.italic;
    t.underline = s.underline; t.alignH = s.alignH; t.alignV = s.alignV; t.padding = s.padding; t.arrowStart = s.arrowStart;
    t.arrowEnd = s.arrowEnd; t.startSize = s.startSize; t.endSize = s.endSize; t.dash = s.dash; t.bezier = s.bezier;
    t.calloutShape = s.calloutShape; t.shaded = s.shaded; t.tailColor = s.tailColor; t.tailWidth = s.tailWidth;
    t.shapeKind = s.shapeKind; t.stepShape = s.stepShape; t.stepType = s.stepType; t.magnifyShape = s.magnifyShape;
    t.magnifyScale = s.magnifyScale; t.magnifyLink = s.magnifyLink; t.hideKind = s.hideKind; t.antialias = s.antialias;
    t.amount = s.amount; t.lineSpacing = s.lineSpacing; t.stepOutline = s.stepOutline; t.stepOutlineWidth = s.stepOutlineWidth;
    t.areaShape = s.areaShape; t.grad = GradC(s.grad); t.bgBlur = s.bgBlur; t.bgBlurAmount = s.bgBlurAmount; t.loupe = s.loupe;
    t.loupeHandle = s.loupeHandle; t.textOnly = s.textOnly; t.snapText = s.snapText; t.sketch = s.sketch;
    t.sketchSeed = s.sketchSeed; t.vertical = s.vertical; t.textOnPath = s.textOnPath;
    if (o.picture && !o.picture->empty()) {
        c.picture = o.picture->px.data();
        c.picW = o.picture->w;
        c.picH = o.picture->h;
        c.picDpi = o.picture->dpi;
    }
    for (auto& k : o.children) kids.push_back(std::make_unique<ObjC>(k));
    for (auto& k : kids) kidsC.push_back(k->c);
    c.children = kidsC.data();
    c.childCount = kidsC.size();
    c.name = o.name.c_str();
    c.hidden = o.hidden;
    c.locked = o.locked;
    c.floating = o.floating;
    c.textBoxes = reinterpret_cast<const SnapcoreRectF*>(o.textBoxes.data());
    c.textBoxCount = o.textBoxes.size();
    c.pressure = o.pressure.data();
    c.pressureCount = o.pressure.size();
}

DrawObj ObjFromC(const SnapcoreObj& c) {
    DrawObj o;
    o.type = (ObjType)c.type;
    o.p1 = PointF(c.p1.x, c.p1.y);
    o.p2 = PointF(c.p2.x, c.p2.y);
    o.tail = PointF(c.tail.x, c.tail.y);
    o.ctrl = PointF(c.ctrl.x, c.ctrl.y);
    if (c.ptsCount) o.pts.assign(reinterpret_cast<const PointF*>(c.pts), reinterpret_cast<const PointF*>(c.pts) + c.ptsCount);
    o.text = c.text ? c.text : L"";
    o.stepValue = c.stepValue;
    const SnapcoreStyle& t = c.style;
    Style& s = o.style;
    s.stroke = t.stroke; s.fill = t.fill; s.textColor = t.textColor; s.width = t.width; s.opacity = t.opacity; s.shadow = t.shadow != 0;
    s.shadowDir = t.shadowDir; s.shadowColor = t.shadowColor; s.shadowOpacity = t.shadowOpacity; s.shadowBlur = t.shadowBlur;
    s.shadowDist = t.shadowDist; s.font = t.font ? t.font : L""; s.fontSize = t.fontSize; s.bold = t.bold != 0; s.italic = t.italic != 0;
    s.underline = t.underline != 0; s.alignH = t.alignH; s.alignV = t.alignV; s.padding = t.padding; s.arrowStart = t.arrowStart;
    s.arrowEnd = t.arrowEnd; s.startSize = t.startSize; s.endSize = t.endSize; s.dash = t.dash; s.bezier = t.bezier != 0;
    s.calloutShape = t.calloutShape; s.shaded = t.shaded != 0; s.tailColor = t.tailColor; s.tailWidth = t.tailWidth;
    s.shapeKind = t.shapeKind; s.stepShape = t.stepShape; s.stepType = t.stepType; s.magnifyShape = t.magnifyShape;
    s.magnifyScale = t.magnifyScale; s.magnifyLink = t.magnifyLink != 0; s.hideKind = t.hideKind; s.antialias = t.antialias != 0;
    s.amount = t.amount; s.lineSpacing = t.lineSpacing; s.stepOutline = t.stepOutline; s.stepOutlineWidth = t.stepOutlineWidth;
    s.areaShape = t.areaShape; s.bgBlur = t.bgBlur != 0; s.bgBlurAmount = t.bgBlurAmount; s.loupe = t.loupe != 0;
    s.loupeHandle = t.loupeHandle != 0; s.textOnly = t.textOnly != 0; s.snapText = t.snapText != 0; s.sketch = t.sketch != 0;
    s.sketchSeed = t.sketchSeed; s.vertical = t.vertical != 0; s.textOnPath = t.textOnPath != 0;
    s.grad.type = t.grad.type;
    s.grad.angle = t.grad.angle;
    if (t.grad.count)
        s.grad.stops.assign(reinterpret_cast<const GradStop*>(t.grad.stops), reinterpret_cast<const GradStop*>(t.grad.stops) + t.grad.count);
    if (c.picture && c.picW > 0 && c.picH > 0) {
        auto pic = std::make_shared<Image>(c.picW, c.picH);
        memcpy(pic->px.data(), c.picture, pic->px.size() * 4);
        pic->dpi = c.picDpi;
        o.picture = pic;
    }
    for (size_t i = 0; i < c.childCount; i++) o.children.push_back(ObjFromC(c.children[i]));
    o.name = c.name ? c.name : L"";
    o.hidden = c.hidden != 0;
    o.locked = c.locked != 0;
    o.floating = c.floating != 0;
    if (c.textBoxCount)
        o.textBoxes.assign(reinterpret_cast<const RectF*>(c.textBoxes), reinterpret_cast<const RectF*>(c.textBoxes) + c.textBoxCount);
    if (c.pressureCount) o.pressure.assign(c.pressure, c.pressure + c.pressureCount);
    return o;
}

void RenderObject(Graphics& g, Image& canvas, const DrawObj& o, bool hideText) {
    ObjC c(o);
    snapcore_render_object(GraphicsNative::Of(g), canvas.px.data(), canvas.w, canvas.h, &c.c, hideText);
}

void ShapePath(GraphicsPath& gp, int kind, RectF r) { snapcore_shape_path(PathNative::Of(gp), kind, R(r)); }

void CalloutBodyPath(GraphicsPath& gp, int shape, RectF r) { snapcore_callout_body_path(PathNative::Of(gp), shape, R(r)); }

void AreaPath(GraphicsPath& gp, const DrawObj& o) {
    ObjC c(o);
    snapcore_area_path(PathNative::Of(gp), &c.c);
}

RectF ObjRect(const DrawObj& o, int which) {
    ObjC c(o);
    return FromC(snapcore_obj_rect(&c.c, which));
}

RectF CalloutTextRect(const DrawObj& o) { return ObjRect(o, 1); }
RectF ArrowLabelRect(const DrawObj& o) { return ObjRect(o, 2); }
RectF MagnifierExtra(const DrawObj& o) { return ObjRect(o, 3); }

float TextBlockHeight(const DrawObj& o, float width) {
    ObjC c(o);
    return snapcore_text_block_height(&c.c, width);
}

float StampAspect(const std::wstring& id) { return snapcore_stamp_aspect(id.c_str()); }

Image StampImage(const std::wstring& id, int px) { return TakeImage(snapcore_stamp_image(id.c_str(), px, 0)); }

std::unique_ptr<Brush> FillBrush(uint32_t color, const Gradient& g, RectF r, int opacity) {
    SnapcoreGradient sg = GradC(g);
    return std::make_unique<NativeBrush>((GpBrush*)snapcore_fill_brush(color, &sg, R(r), opacity));
}

void FillGradientMask(Image& img, const std::vector<uint8_t>& mask, RECT b, const Gradient& g, int opacity) {
    SnapcoreGradient sg = GradC(g);
    snapcore_fill_gradient_mask(img.px.data(), img.w, img.h, mask.data(), mask.size(), { b.left, b.top, b.right, b.bottom }, &sg,
                                opacity);
}

void DrawCapGlyph(Graphics& g, int cap, RectF r, Color c, bool atStart) {
    snapcore_draw_cap_glyph(GraphicsNative::Of(g), cap, R(r), c.GetValue(), atStart);
}

void ApplyDash(Pen& p, int dash) { snapcore_apply_dash(PenNative::Of(p), dash); }

std::wstring StepLabel(int value, int type) {
    SnapcoreWStr out{};
    snapcore_step_label(value, type, &out);
    return TakeWStr(out);
}

float CapLength(float w, float size) { return snapcore_cap_length(w, size); }

} // namespace snap::ed
