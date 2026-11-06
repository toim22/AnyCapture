// .snap format — ed_comp_2 §3: 24-byte header + OLE compound file (STGFMT_DOCFILE)
//   Index                    XML stream <Index><FrameList><Frame Name='Frame0'/></FrameList></Index>
//   Frame%d/Document         XML stream of the frame (vector objects, properties)
//   Frame%d/ImageStorage/... PNG streams (base image and embedded images, by StreamName)
#pragma once
#include "Image.h"
#include "Xml.h"

namespace snap {

struct SnapFrame {
    Image base;                                     // background image (Image_0.png)
    XmlNode doc{ "Document" };                      // objects and metadata
    std::map<std::string, Image> embedded;          // images referenced by the objects (StreamName → image)
};

bool SaveSnapFile(const std::wstring& path, const std::vector<SnapFrame>& frames);
bool LoadSnapFile(const std::wstring& path, std::vector<SnapFrame>& frames);
bool LoadSnapDocument(const std::wstring& path, XmlNode& doc);      // only the first frame's XML (no images): tags, mode…
bool IsSnapFile(const std::wstring& path);

// Who reads and writes the compound file: snapcore (Rust, rust/snapcore; what the programs use) or the Windows
// structured storage API. Both produce the same format; the tests switch to check one against the other.
enum class SnapBackend { Rust, Windows };
void SetSnapBackend(SnapBackend b);
SnapBackend GetSnapBackend();

} // namespace snap
