// Conversions between the C++ types and the snapcore C ABI (rust/snapcore/include/snapcore.h), shared by the C++ files
// that hand work to Rust while both languages live side by side.
#pragma once
#include <functional>
#include "Image.h"
#include "Xml.h"
#include "snapcore.h"

namespace snap {

// Image.cpp: an Image as snapcore takes it (pointers into img, valid while img lives) and a decoded image copied out
SnapcoreImageIn ImageIn(const Image& img);
void CopyDecoded(const SnapcoreImage* d, Image& out);
Image TakeImage(SnapcoreImage* d);                  // copied out and freed; empty for null

// Xml.cpp: a tree handed to a new builder (snapcore takes it over), and a tree rebuilt from what snapcore sends to the
// sink that `send` passes on (root is replaced)
SnapcoreXmlBuilder* XmlToSnapcore(const XmlNode& root);
void XmlFromSnapcore(XmlNode& root, const std::function<void(const SnapcoreXmlSink* sink, void* ctx)>& send);

// a string snapcore allocated, copied out and freed
inline std::wstring TakeWStr(SnapcoreWStr s) {
    std::wstring w = s.len ? std::wstring(s.data, s.len) : std::wstring();
    if (s.data) snapcore_wstr_free(s);
    return w;
}

namespace fx {
struct BeautifyOptions;
SnapcoreBeautify ToSnapcore(const BeautifyOptions& o);   // Beautify.cpp: pointers into o
}

} // namespace snap
