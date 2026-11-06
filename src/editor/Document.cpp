#include "ObjBridge.h"
#include "Library.h"
#include <cmath>

namespace snap::ed {

using Gdiplus::PointF;
using Gdiplus::RectF;

bool Gradient::operator==(const Gradient& o) const {
    if (type != o.type || angle != o.angle || stops.size() != o.stops.size()) return false;
    for (size_t i = 0; i < stops.size(); i++) if (stops[i].pos != o.stops[i].pos || stops[i].color != o.stops[i].color) return false;
    return true;
}

RectF DrawObj::Rect() const {
    float x0 = std::min(p1.X, p2.X), y0 = std::min(p1.Y, p2.Y);
    return RectF(x0, y0, std::fabs(p2.X - p1.X), std::fabs(p2.Y - p1.Y));
}

int DrawObj::Kind() const {
    switch (type) {
    case ObjType::Ellipse: return ShapeEllipse;
    case ObjType::RoundRect: return ShapeRoundRect;
    case ObjType::Rect: return ShapeRect;
    default: return std::clamp(style.shapeKind, 0, ShapeCount - 1);
    }
}

// callout shapes with a tip: speech (wedge), thought (bubbles), box with arrow (line) and arrows (the tip gives the direction)
bool DrawObj::CalloutHasTail() const {
    switch (style.calloutShape) {
    case 16: case 17: case 18: case 19: case 20: case 21: return false;
    default: return true;
    }
}

// the area drawn (stroke, ends, callout tip, label, shadow), measured by snapcore (rust/snapcore/src/doc.rs)
RectF DrawObj::Bounds() const { return ObjRect(*this, 0); }

// on the object, 'tol' pixels around it counting: rust/snapcore/src/doc.rs (DrawObj::hit_test)
bool DrawObj::HitTest(PointF p, float tol) const {
    ObjC c(*this);
    return snapcore_hit_test(&c.c, p.X, p.Y, tol);
}

void DrawObj::Move(float dx, float dy) {
    p1.X += dx; p1.Y += dy; p2.X += dx; p2.Y += dy;
    tail.X += dx; tail.Y += dy;
    ctrl.X += dx; ctrl.Y += dy;
    for (auto& p : pts) { p.X += dx; p.Y += dy; }
    for (auto& r : textBoxes) { r.X += dx; r.Y += dy; }
    for (auto& c : children) c.Move(dx, dy);
}

void DrawObj::Scale(RectF from, RectF to) {
    auto map = [&](PointF& p) {
        float u = from.Width > 0.01f ? (p.X - from.X) / from.Width : 0;
        float v = from.Height > 0.01f ? (p.Y - from.Y) / from.Height : 0;
        p.X = to.X + u * to.Width; p.Y = to.Y + v * to.Height;
    };
    map(p1); map(p2); map(tail); map(ctrl);
    for (auto& p : pts) map(p);
    for (auto& r : textBoxes) {
        PointF a(r.X, r.Y), b(r.GetRight(), r.GetBottom());
        map(a); map(b);
        r = RectF(std::min(a.X, b.X), std::min(a.Y, b.Y), std::fabs(b.X - a.X), std::fabs(b.Y - a.Y));
    }
    for (auto& c : children) c.Scale(from, to);
}

void DrawObj::MapPoints(const std::function<PointF(PointF)>& f) {
    p1 = f(p1); p2 = f(p2); tail = f(tail); ctrl = f(ctrl);
    for (auto& p : pts) p = f(p);
    for (auto& r : textBoxes) {
        PointF a = f(PointF(r.X, r.Y)), b = f(PointF(r.GetRight(), r.GetBottom()));
        r = RectF(std::min(a.X, b.X), std::min(a.Y, b.Y), std::fabs(b.X - a.X), std::fabs(b.Y - a.Y));
    }
    for (auto& c : children) c.MapPoints(f);
}

void DrawObj::UpdateGroupRect() {
    if (type != ObjType::Group || children.empty()) return;
    RectF u = children[0].Bounds();
    for (auto& c : children) RectF::Union(u, u, c.Bounds());
    p1 = PointF(u.X, u.Y); p2 = PointF(u.GetRight(), u.GetBottom());
}

// ---------------------------------------------------------------- history
void Document::Checkpoint() {
    undo_.push_back(st);
    if (undo_.size() > 80) undo_.erase(undo_.begin());
    redo_.clear();
    modified = true;
}

void Document::Undo() {
    if (undo_.empty()) return;
    redo_.push_back(st);
    st = undo_.back(); undo_.pop_back();
    modified = true;
}

void Document::Redo() {
    if (redo_.empty()) return;
    undo_.push_back(st);
    st = redo_.back(); redo_.pop_back();
    modified = true;
}

void Document::UndoAll() {
    if (undo_.empty()) return;
    redo_.push_back(st);
    for (size_t i = undo_.size() - 1; i > 0; i--) redo_.push_back(undo_[i]);
    st = undo_.front();
    undo_.clear();
    modified = true;
}

DrawObj* Document::Mutable(size_t idx) {
    if (idx >= st.objs.size()) return nullptr;
    st.objs[idx] = std::make_shared<DrawObj>(*st.objs[idx]);
    return st.objs[idx].get();
}

Image& Document::MutableBase() {
    auto copy = std::make_shared<Image>(st.base ? *st.base : Image());
    st.base = copy;
    return *copy;
}

void Document::SetBase(Image img) {
    if (!img.meta && st.base) img.meta = st.base->meta;       // crop, resize, effects: the photo keeps its metadata
    st.base = std::make_shared<Image>(std::move(img));
}

Image Document::Render(const DrawObj* hideText) const {
    if (!st.base) return {};
    Image out = *st.base;
    auto bmp = out.WrapGdip();
    Gdiplus::Graphics g(bmp.get());
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    for (auto& o : st.objs) if (!o->hidden && o->type != ObjType::Link) RenderObject(g, out, *o, o.get() == hideText);
    g.Flush(Gdiplus::FlushIntentionSync);
    return out;
}

// ---------------------------------------------------------------- file
// the .snap: rust/snapcore/src/serial.rs (DocFile) writes the base, the objects and the capture's details
bool Document::Save() {
    if (isVideo || path.empty() || !st.base) return false;
    ObjsC objs;
    for (auto& o : st.objs) objs.Add(*o);
    SnapcoreDocInfo info{ captureMode.c_str(), windowTitle.c_str(), application.c_str(), created.c_str(), tags.c_str(), st.nextStep,
                          guidesX.data(), guidesX.size(), guidesY.data(), guidesY.size() };
    SnapcoreImageIn base = ImageIn(*st.base);
    if (!snapcore_doc_save(path.c_str(), path.size(), &info, &base, objs.c.data(), objs.c.size(), kAppVersion)) return false;
    modified = false;
    return true;
}

bool Document::SaveAs(const std::wstring& file, int q) {
    std::wstring ext = FileExt(file);
    if (ext == L".snap") {
        std::wstring old = path;
        path = file;
        if (Save()) return true;
        path = old;
        return false;
    }
    Image img = Render();
    img.meta = st.base->meta;                               // what the imported photo had (removed if StripMetadata)
    if (ext == L".pdf") {                                   // the captured links stay clickable in the PDF
        std::vector<PdfLink> links;
        for (auto& o : st.objs) if (o->type == ObjType::Link && !o->hidden) links.push_back({ o->Rect(), o->text });
        std::wstring err;
        return SavePdf(file, img, err, links);
    }
    return SaveImageFile(file, img, q);
}

std::unique_ptr<Document> Document::FromImage(Image img, const std::wstring& title) {
    auto d = std::make_unique<Document>();
    d->SetBase(std::move(img));
    d->title = title;
    d->created = TimeStamp(L"%04d-%02d-%02dT%02d:%02d:%02d");
    return d;
}

std::unique_ptr<Document> Document::Load(const std::wstring& file) {
    std::wstring ext = FileExt(file);
    if (ext == L".mp4") {
        auto d = std::make_unique<Document>();
        d->path = file; d->isVideo = true; d->title = FileNameOf(file);
        return d;
    }
    if (ext == L".snap") {
        SnapcoreObjList* l = snapcore_doc_load(file.c_str(), file.size());
        if (!l) return nullptr;
        auto d = std::make_unique<Document>();
        SnapcoreDocInfo info{};
        snapcore_doc_info(l, &info);
        d->path = file;
        d->captureMode = info.captureMode;
        d->windowTitle = info.windowTitle;
        d->application = info.application;
        d->created = info.created;
        d->st.nextStep = info.nextStep;
        d->tags = info.tags;
        d->title = d->windowTitle.empty() ? (d->captureMode.empty() ? FileNameOf(file) : d->captureMode) : d->windowTitle;
        std::wstring name = CaptureName(file);          // name given in the library
        if (!name.empty()) d->title = name;
        d->guidesX.assign(info.guidesX, info.guidesX + info.guidesXCount);
        d->guidesY.assign(info.guidesY, info.guidesY + info.guidesYCount);
        d->SetBase(TakeImage(snapcore_doc_take_base(l)));
        for (size_t i = 0, n = snapcore_objs_count(l); i < n; i++)
            d->st.objs.push_back(std::make_shared<DrawObj>(ObjFromC(*snapcore_objs_get(l, i))));
        snapcore_objs_free(l);
        return d;
    }
    Image img;
    if (!LoadImageFile(file, img)) return nullptr;
    return FromImage(std::move(img), FileNameOf(file));
}

} // namespace snap::ed
