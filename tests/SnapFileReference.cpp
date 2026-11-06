// The .snap reading and writing of SnapFile.cpp as it was before it moved to Rust (rust/snapcore/src/snap.rs), over the
// snapcore container, kept only to check that the Rust code writes the same bytes and reads the same documents
// (tests.cpp, [19] ".snap documents: Rust and the former C++").
#include "Common.h"
#include "SnapFile.h"
#include "snapcore.h"
#include <functional>

namespace snap::snapref {

namespace {

class Reader {
public:
    ~Reader() { snapcore_close(r_); }
    bool Open(const std::wstring& path) { r_ = snapcore_open(path.c_str(), path.size()); return r_ != nullptr; }
    bool Read(const std::string& stream, std::string& out) {
        SnapcoreBuf b{};
        if (!snapcore_read(r_, stream.data(), stream.size(), &b)) return false;
        out.assign((const char*)b.data, b.len);
        snapcore_buf_free(b);
        return true;
    }
private:
    SnapcoreReader* r_ = nullptr;
};

class Writer {
public:
    ~Writer() { snapcore_abort(w_); }
    bool Create(const std::wstring& path) { w_ = snapcore_create(path.c_str(), path.size()); return w_ != nullptr; }
    bool Write(const std::string& stream, const void* data, size_t len) {
        return snapcore_write(w_, stream.data(), stream.size(), (const uint8_t*)data, len);
    }
    bool Finish() { bool ok = snapcore_finish(w_); w_ = nullptr; return ok; }
private:
    SnapcoreWriter* w_ = nullptr;
};

bool WritePng(Writer& w, const std::string& stream, const Image& img) {
    std::vector<BYTE> png;
    if (!EncodeToMemory(img, ImageFormat::PNG, png)) return false;
    return w.Write(stream, png.data(), png.size());
}

bool ReadPng(Reader& r, const std::string& stream, Image& img) {
    std::string data;
    if (!r.Read(stream, data)) return false;
    return DecodeFromMemory((const BYTE*)data.data(), data.size(), img);
}

} // namespace

bool SaveSnapFile(const std::wstring& path, const std::vector<SnapFrame>& frames) {
    std::wstring tmp = path + L".saving";
    bool ok = false;
    {
        auto w = std::make_unique<Writer>();
        if (!w->Create(tmp)) { Log(L"SaveSnapFile: could not open %s", tmp.c_str()); return false; }
        XmlNode index("Index");
        XmlNode& list = index.add("FrameList");
        index.set("SoftwareVersion", WToUtf8(kAppVersion));
        ok = true;
        for (size_t i = 0; i < frames.size() && ok; i++) {
            std::string fname = FormatA("Frame%zu", i);
            list.add("Frame").set("Name", fname);
            XmlNode doc = frames[i].doc;
            doc.set("BaseImage", "Image_0.png");
            // metadata of an imported photo (C29), written only when there is some
            if (const ImageMeta* m = frames[i].base.meta.get(); m && !doc.child("Metadata")) {
                XmlNode& md = doc.add("Metadata");
                md.setW("DateTaken", m->dateTaken).setW("Make", m->make).setW("Model", m->model).setW("Software", m->software);
                if (m->gps) md.seti("Gps", 1).setd("Latitude", m->lat).setd("Longitude", m->lon);
            }
            doc.seti("Width", frames[i].base.w).seti("Height", frames[i].base.h);
            std::string xml = XmlSerialize(doc);
            ok = w->Write(fname + "/Document", xml.data(), xml.size()) &&
                 WritePng(*w, fname + "/ImageStorage/Image_0.png", frames[i].base);
            for (auto& e : frames[i].embedded) if (ok) ok = WritePng(*w, fname + "/ImageStorage/" + e.first, e.second);
        }
        if (ok) {
            std::string xml = XmlSerialize(index);
            ok = w->Write("Index", xml.data(), xml.size());
        }
        ok = ok && w->Finish();
    }                           // the file is closed here (an unfinished one too)
    if (!ok) { DeleteFileW(tmp.c_str()); Log(L"SaveSnapFile failed: %s", path.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool LoadSnapDocument(const std::wstring& path, XmlNode& doc) {
    if (!IsSnapFile(path)) return false;
    Reader r;
    std::string xml;
    return r.Open(path) && r.Read("Frame0/Document", xml) && XmlParse(xml, doc);
}

bool LoadSnapFile(const std::wstring& path, std::vector<SnapFrame>& frames) {
    if (!IsSnapFile(path)) return false;
    bool ok = false;
    if (Reader r; r.Open(path)) {
        std::string idx;
        XmlNode index;
        if (r.Read("Index", idx) && XmlParse(idx, index)) {
            frames.clear();
            ok = true;
            if (const XmlNode* list = index.child("FrameList")) {
                for (auto& fr : list->children) {
                    std::string name = fr.get("Name");
                    SnapFrame frame;
                    std::string xml;
                    if (!r.Read(name + "/Document", xml)) { ok = false; break; }      // a frame without its document
                    XmlParse(xml, frame.doc);
                    std::string images = name + "/ImageStorage/";
                    ReadPng(r, images + frame.doc.get("BaseImage", "Image_0.png"), frame.base);
                    if (const XmlNode* md = frame.doc.child("Metadata")) {
                        auto m = std::make_shared<ImageMeta>();
                        m->dateTaken = md->getW("DateTaken"); m->make = md->getW("Make"); m->model = md->getW("Model");
                        m->software = md->getW("Software");
                        m->gps = md->getb("Gps"); m->lat = md->getd("Latitude"); m->lon = md->getd("Longitude");
                        if (!m->empty()) frame.base.meta = m;
                    }
                    // embedded images referenced by StreamName at any level
                    std::function<void(const XmlNode&)> walk = [&](const XmlNode& n) {
                        std::string sn = n.get("StreamName");
                        if (!sn.empty() && !frame.embedded.count(sn)) {
                            Image im; if (ReadPng(r, images + sn, im)) frame.embedded[sn] = std::move(im);
                        }
                        for (auto& c : n.children) walk(c);
                    };
                    walk(frame.doc);
                    frames.push_back(std::move(frame));
                }
            }
            ok = ok && !frames.empty() && !frames[0].base.empty();
        }
    }
    if (!ok) Log(L"LoadSnapFile failed: %s", path.c_str());
    return ok;
}

} // namespace snap::snapref
