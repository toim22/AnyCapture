// XmlNode stays here for the C++ code; reading and writing XML text lives in rust/snapcore/src/xml.rs.
#include "Xml.h"
#include "Common.h"
#include "snapcore.h"
#include <cstdlib>
#include <cstdio>

namespace snap {

const std::string* XmlNode::attr(const std::string& k) const {
    for (auto& a : attrs) if (a.first == k) return &a.second;
    return nullptr;
}
std::string XmlNode::get(const std::string& k, const std::string& def) const { auto* a = attr(k); return a ? *a : def; }
std::wstring XmlNode::getW(const std::string& k, const std::wstring& def) const { auto* a = attr(k); return a ? Utf8ToW(*a) : def; }
int XmlNode::geti(const std::string& k, int def) const { auto* a = attr(k); return a ? atoi(a->c_str()) : def; }
double XmlNode::getd(const std::string& k, double def) const { auto* a = attr(k); return a ? atof(a->c_str()) : def; }

XmlNode& XmlNode::set(const std::string& k, const std::string& v) {
    for (auto& a : attrs) if (a.first == k) { a.second = v; return *this; }
    attrs.emplace_back(k, v);
    return *this;
}
XmlNode& XmlNode::setW(const std::string& k, const std::wstring& v) { return set(k, WToUtf8(v)); }
XmlNode& XmlNode::seti(const std::string& k, long long v) { return set(k, std::to_string(v)); }
XmlNode& XmlNode::setd(const std::string& k, double v) { char b[64]; snprintf(b, sizeof b, "%.4f", v); return set(k, b); }
XmlNode& XmlNode::add(const std::string& n) { children.emplace_back(n); return children.back(); }
const XmlNode* XmlNode::child(const std::string& n) const { for (auto& c : children) if (c.name == n) return &c; return nullptr; }
XmlNode* XmlNode::child(const std::string& n) { for (auto& c : children) if (c.name == n) return &c; return nullptr; }

namespace {

// rebuilds the tree snapcore reads: each open adds the element below the one still open
struct Building {
    XmlNode* root;
    std::vector<XmlNode*> open;
};

void SinkOpen(void* ctx, const char* name, size_t len) {
    auto* b = static_cast<Building*>(ctx);
    XmlNode* n = b->root;
    if (!b->open.empty()) n = &b->open.back()->children.emplace_back();
    n->name.assign(name, len);
    b->open.push_back(n);
}
void SinkAttr(void* ctx, const char* k, size_t kl, const char* v, size_t vl) {
    static_cast<Building*>(ctx)->open.back()->attrs.emplace_back(std::string(k, kl), std::string(v, vl));
}
void SinkText(void* ctx, const char* t, size_t len) { static_cast<Building*>(ctx)->open.back()->text.assign(t, len); }
void SinkClose(void* ctx) { static_cast<Building*>(ctx)->open.pop_back(); }

const SnapcoreXmlSink kSink{ SinkOpen, SinkAttr, SinkText, SinkClose };

SnapcoreXmlBuilder* Build(const XmlNode& root) {
    SnapcoreXmlBuilder* b = snapcore_xml_builder_new();
    auto feed = [b](auto& self, const XmlNode& n) -> void {
        snapcore_xml_open(b, n.name.data(), n.name.size());
        for (auto& [k, v] : n.attrs) snapcore_xml_attr(b, k.data(), k.size(), v.data(), v.size());
        if (!n.text.empty()) snapcore_xml_text(b, n.text.data(), n.text.size());
        for (auto& c : n.children) self(self, c);
        snapcore_xml_close(b);
    };
    feed(feed, root);
    return b;
}

} // namespace

std::string XmlSerialize(const XmlNode& root, bool decl) {
    std::string out;
    SnapcoreBuf buf{};
    if (snapcore_xml_serialize(Build(root), decl, &buf)) {
        out.assign(reinterpret_cast<const char*>(buf.data), buf.len);
        snapcore_buf_free(buf);
    }
    return out;
}

bool XmlParse(const std::string& text, XmlNode& root) {
    root = XmlNode();
    Building b{ &root, {} };
    return snapcore_xml_parse(text.data(), text.size(), &kSink, &b);
}

bool XmlLoadFile(const std::wstring& path, XmlNode& root) {
    XmlNode n;
    Building b{ &n, {} };
    int r = snapcore_xml_load(path.data(), path.size(), &kSink, &b);
    if (r >= 0) root = std::move(n);         // as before: a file that does not open leaves the tree as it was
    return r == 1;
}

bool XmlSaveFile(const std::wstring& path, const XmlNode& root) {
    return snapcore_xml_save(Build(root), path.data(), path.size());
}

} // namespace snap
