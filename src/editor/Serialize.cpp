// XML of the objects: rust/snapcore/src/serial.rs (comp_5 §6: one object = one element; binary data in streams by
// StreamName). These wrappers keep the C++ API of the gradients (quick styles) and of the Editor's clipboard format.
#include "ObjBridge.h"

namespace snap::ed {

namespace {

std::string TakeBuf(SnapcoreBuf& b) {
    std::string out(reinterpret_cast<const char*>(b.data), b.len);
    snapcore_buf_free(b);
    return out;
}

} // namespace

std::string GradStopsToString(const Gradient& g) {
    SnapcoreGradient sg{ g.type, g.angle, reinterpret_cast<const SnapcoreGradStop*>(g.stops.data()), g.stops.size() };
    SnapcoreBuf b{};
    return snapcore_grad_stops_string(&sg, &b) ? TakeBuf(b) : std::string();
}

void GradToXml(XmlNode& n, const Gradient& g) {
    if (!g.Active()) return;
    n.seti("FillGradient", g.type).setd("GradientAngle", g.angle).set("GradientStops", GradStopsToString(g));
}

Gradient GradFromXml(const XmlNode& n) {
    auto value = [&](const char* k) { const std::string* v = n.attr(k); return v ? v->c_str() : nullptr; };
    Gradient g;
    snapcore_grad_from_attrs(value("FillGradient"), value("GradientAngle"), value("GradientStops"), &g.type, &g.angle,
                             [](void* ctx, float pos, uint32_t color) { static_cast<Gradient*>(ctx)->stops.push_back({ pos, color }); },
                             &g);
    return g;
}

// ---------------------------------------------------------------- objects on the clipboard (copy / paste in place)
// the objects as XML (as in the .snap), with the embedded images in base64 PNG; 'source' identifies the document
std::string ObjectsToClip(const std::vector<DrawObj>& objs, const std::wstring& source) {
    ObjsC c;
    for (auto& o : objs) c.Add(o);
    SnapcoreBuf b{};
    return snapcore_objects_to_clip(c.c.data(), c.c.size(), source.c_str(), &b) ? TakeBuf(b) : std::string();
}

std::vector<DrawObj> ObjectsFromClip(const std::string& xml, std::wstring* source) {
    std::vector<DrawObj> out;
    SnapcoreObjList* l = snapcore_objects_from_clip(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
    if (!l) return out;
    if (const wchar_t* s = snapcore_objs_source(l); s && source) *source = s;
    for (size_t i = 0, n = snapcore_objs_count(l); i < n; i++) out.push_back(ObjFromC(*snapcore_objs_get(l, i)));
    snapcore_objs_free(l);
    return out;
}

} // namespace snap::ed
