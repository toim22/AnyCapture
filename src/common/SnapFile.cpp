#include "SnapFile.h"
#include <wrl/client.h>
#ifdef SNAPNAB_RUST
#include "snapcore.h"
#endif

using Microsoft::WRL::ComPtr;

namespace snap {

// 0xCB + "SNAPNAB" + 16-byte format identifier  (≙ 0xCB + "TSCSNAG" + 16 B in the original)
static const BYTE kHeader[24] = { 0xCB, 'S', 'N', 'A', 'P', 'N', 'A', 'B',
                                  0x5A, 0x3C, 0x91, 0x0E, 0x6B, 0x2D, 0x4F, 0x18,
                                  0xA7, 0x44, 0xC2, 0x9B, 0x10, 0x7E, 0xD3, 0x55 };

// The container: named streams in nested storages, addressed by paths such as "Frame0/ImageStorage/Image_0.png".
// Two implementations: snapcore (Rust, rust/snapcore — the default when built with it) and the Windows structured
// storage API (the original one, kept so the tests can check that each reads what the other writes).
namespace {

struct ContainerReader {
    virtual ~ContainerReader() = default;
    virtual bool Read(const std::string& stream, std::string& out) = 0;
};

struct ContainerWriter {
    virtual ~ContainerWriter() = default;
    virtual bool Write(const std::string& stream, const void* data, size_t len) = 0;
    virtual bool Finish() = 0;              // writes everything out and closes the file
};

std::vector<std::wstring> SplitPath(const std::string& stream) {
    std::vector<std::wstring> parts;
    size_t i = 0;
    while (i <= stream.size()) {
        size_t j = stream.find('/', i);
        if (j == std::string::npos) j = stream.size();
        if (j > i) parts.push_back(Utf8ToW(stream.substr(i, j - i)));
        i = j + 1;
    }
    return parts;
}

// ---------------------------------------------------------------- Windows structured storage

// ILockBytes over the file, adding the header offset (≙ CHeaderedILockBytes)
class HeaderedLockBytes : public ILockBytes {
public:
    HeaderedLockBytes(HANDLE f, ULONGLONG off) : file_(f), off_(off) {}
    ~HeaderedLockBytes() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ILockBytes) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --ref_; if (!r) delete this; return r; }
    STDMETHODIMP ReadAt(ULARGE_INTEGER pos, void* pv, ULONG cb, ULONG* read) override {
        LARGE_INTEGER p; p.QuadPart = pos.QuadPart + off_;
        SetFilePointerEx(file_, p, nullptr, FILE_BEGIN);
        DWORD rd = 0;
        BOOL ok = ReadFile(file_, pv, cb, &rd, nullptr);
        if (read) *read = rd;
        return ok ? S_OK : STG_E_READFAULT;
    }
    STDMETHODIMP WriteAt(ULARGE_INTEGER pos, const void* pv, ULONG cb, ULONG* written) override {
        LARGE_INTEGER p; p.QuadPart = pos.QuadPart + off_;
        SetFilePointerEx(file_, p, nullptr, FILE_BEGIN);
        DWORD wr = 0;
        BOOL ok = WriteFile(file_, pv, cb, &wr, nullptr);
        if (written) *written = wr;
        return ok ? S_OK : STG_E_WRITEFAULT;
    }
    STDMETHODIMP Flush() override { return FlushFileBuffers(file_) ? S_OK : STG_E_WRITEFAULT; }
    STDMETHODIMP SetSize(ULARGE_INTEGER cb) override {
        LARGE_INTEGER p; p.QuadPart = cb.QuadPart + off_;
        SetFilePointerEx(file_, p, nullptr, FILE_BEGIN);
        return SetEndOfFile(file_) ? S_OK : STG_E_MEDIUMFULL;
    }
    STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    STDMETHODIMP Stat(STATSTG* st, DWORD) override {
        memset(st, 0, sizeof(*st));
        st->type = STGTY_LOCKBYTES;
        LARGE_INTEGER sz; GetFileSizeEx(file_, &sz);
        st->cbSize.QuadPart = sz.QuadPart > (LONGLONG)off_ ? sz.QuadPart - off_ : 0;
        return S_OK;
    }
private:
    HANDLE file_;
    ULONGLONG off_;
    ULONG ref_ = 1;
};

class ComReader : public ContainerReader {
public:
    bool Open(const std::wstring& path) {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        auto* lb = new HeaderedLockBytes(f, sizeof kHeader);
        bool ok = SUCCEEDED(StgOpenStorageOnILockBytes(lb, nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &root_));
        lb->Release();          // the storage keeps its own reference; the file closes with it
        return ok;
    }
    bool Read(const std::string& stream, std::string& out) override {
        auto parts = SplitPath(stream);
        if (parts.empty()) return false;
        // the parents stay open while the stream is read: releasing one invalidates what was opened in it
        std::vector<ComPtr<IStorage>> chain{ root_ };
        for (size_t i = 0; i + 1 < parts.size(); i++) {
            ComPtr<IStorage> sub;
            if (FAILED(chain.back()->OpenStorage(parts[i].c_str(), nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, &sub))) return false;
            chain.push_back(sub);
        }
        ComPtr<IStream> st;
        if (FAILED(chain.back()->OpenStream(parts.back().c_str(), nullptr, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &st))) return false;
        STATSTG ss{}; st->Stat(&ss, STATFLAG_NONAME);
        out.resize((size_t)ss.cbSize.QuadPart);
        ULONG rd = 0;
        return SUCCEEDED(st->Read(out.data(), (ULONG)out.size(), &rd)) && rd == out.size();
    }
private:
    ComPtr<IStorage> root_;
};

class ComWriter : public ContainerWriter {
public:
    bool Create(const std::wstring& path) {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        DWORD wr; WriteFile(f, kHeader, sizeof kHeader, &wr, nullptr);
        auto* lb = new HeaderedLockBytes(f, sizeof kHeader);
        bool ok = SUCCEEDED(StgCreateDocfileOnILockBytes(lb, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &root_));
        lb->Release();
        return ok;
    }
    bool Write(const std::string& stream, const void* data, size_t len) override {
        auto parts = SplitPath(stream);
        if (parts.empty()) return false;
        IStorage* stg = root_.Get();
        std::string key;
        for (size_t i = 0; i + 1 < parts.size(); i++) {     // each storage is created once and kept open until Finish
            key += WToUtf8(parts[i]) + "/";
            auto it = storages_.find(key);
            if (it == storages_.end()) {
                ComPtr<IStorage> sub;
                if (FAILED(stg->CreateStorage(parts[i].c_str(), STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &sub))) return false;
                it = storages_.emplace(key, sub).first;
            }
            stg = it->second.Get();
        }
        ComPtr<IStream> st;
        if (FAILED(stg->CreateStream(parts.back().c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &st))) return false;
        ULONG wr = 0;
        return SUCCEEDED(st->Write(data, (ULONG)len, &wr)) && wr == len;
    }
    bool Finish() override {
        // the innermost storages are committed first ("Frame0/ImageStorage/" before "Frame0/"), the root last
        std::vector<std::pair<std::string, ComPtr<IStorage>>> v(storages_.begin(), storages_.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first.size() > b.first.size(); });
        bool ok = true;
        for (auto& s : v) ok = ok && SUCCEEDED(s.second->Commit(STGC_DEFAULT));
        v.clear(); storages_.clear();
        ok = ok && SUCCEEDED(root_->Commit(STGC_DEFAULT));
        root_.Reset();
        return ok;
    }
private:
    ComPtr<IStorage> root_;
    std::map<std::string, ComPtr<IStorage>> storages_;
};

// ---------------------------------------------------------------- snapcore (Rust)
#ifdef SNAPNAB_RUST
class RustReader : public ContainerReader {
public:
    ~RustReader() override { snapcore_close(r_); }
    bool Open(const std::wstring& path) { r_ = snapcore_open(path.c_str(), path.size()); return r_ != nullptr; }
    bool Read(const std::string& stream, std::string& out) override {
        SnapcoreBuf b{};
        if (!snapcore_read(r_, stream.data(), stream.size(), &b)) return false;
        out.assign((const char*)b.data, b.len);
        snapcore_buf_free(b);
        return true;
    }
private:
    SnapcoreReader* r_ = nullptr;
};

class RustWriter : public ContainerWriter {
public:
    ~RustWriter() override { snapcore_abort(w_); }
    bool Create(const std::wstring& path) { w_ = snapcore_create(path.c_str(), path.size()); return w_ != nullptr; }
    bool Write(const std::string& stream, const void* data, size_t len) override {
        return snapcore_write(w_, stream.data(), stream.size(), (const uint8_t*)data, len);
    }
    bool Finish() override { bool ok = snapcore_finish(w_); w_ = nullptr; return ok; }
private:
    SnapcoreWriter* w_ = nullptr;
};
#endif

#ifdef SNAPNAB_RUST
SnapBackend g_backend = SnapBackend::Rust;
#else
SnapBackend g_backend = SnapBackend::Windows;
#endif

std::unique_ptr<ContainerReader> OpenContainer(const std::wstring& path) {
#ifdef SNAPNAB_RUST
    if (g_backend == SnapBackend::Rust) {
        auto r = std::make_unique<RustReader>();
        return r->Open(path) ? std::move(r) : nullptr;
    }
#endif
    auto r = std::make_unique<ComReader>();
    return r->Open(path) ? std::move(r) : nullptr;
}

std::unique_ptr<ContainerWriter> CreateContainer(const std::wstring& path) {
#ifdef SNAPNAB_RUST
    if (g_backend == SnapBackend::Rust) {
        auto w = std::make_unique<RustWriter>();
        return w->Create(path) ? std::move(w) : nullptr;
    }
#endif
    auto w = std::make_unique<ComWriter>();
    return w->Create(path) ? std::move(w) : nullptr;
}

bool WritePng(ContainerWriter& w, const std::string& stream, const Image& img) {
    std::vector<BYTE> png;
    if (!EncodeToMemory(img, ImageFormat::PNG, png)) return false;
    return w.Write(stream, png.data(), png.size());
}

bool ReadPng(ContainerReader& r, const std::string& stream, Image& img) {
    std::string data;
    if (!r.Read(stream, data)) return false;
    return DecodeFromMemory((const BYTE*)data.data(), data.size(), img);
}

} // namespace

void SetSnapBackend(SnapBackend b) {
#ifdef SNAPNAB_RUST
    g_backend = b;
#else
    (void)b;
#endif
}

SnapBackend GetSnapBackend() { return g_backend; }

bool SaveSnapFile(const std::wstring& path, const std::vector<SnapFrame>& frames) {
    std::wstring tmp = path + L".saving";
    bool ok = false;
    {
        auto w = CreateContainer(tmp);
        if (!w) { Log(L"SaveSnapFile: could not open %s", tmp.c_str()); return false; }
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

bool IsSnapFile(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BYTE h[24]{}; DWORD rd = 0;
    ReadFile(f, h, 24, &rd, nullptr);
    CloseHandle(f);
    return rd == 24 && memcmp(h, kHeader, 24) == 0;
}

bool LoadSnapDocument(const std::wstring& path, XmlNode& doc) {
    if (!IsSnapFile(path)) return false;
    auto r = OpenContainer(path);
    std::string xml;
    return r && r->Read("Frame0/Document", xml) && XmlParse(xml, doc);
}

bool LoadSnapFile(const std::wstring& path, std::vector<SnapFrame>& frames) {
    if (!IsSnapFile(path)) return false;
    bool ok = false;
    if (auto r = OpenContainer(path)) {
        std::string idx;
        XmlNode index;
        if (r->Read("Index", idx) && XmlParse(idx, index)) {
            frames.clear();
            ok = true;
            if (const XmlNode* list = index.child("FrameList")) {
                for (auto& fr : list->children) {
                    std::string name = fr.get("Name");
                    SnapFrame frame;
                    std::string xml;
                    if (!r->Read(name + "/Document", xml)) { ok = false; break; }      // a frame without its document
                    XmlParse(xml, frame.doc);
                    std::string images = name + "/ImageStorage/";
                    ReadPng(*r, images + frame.doc.get("BaseImage", "Image_0.png"), frame.base);
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
                            Image im; if (ReadPng(*r, images + sn, im)) frame.embedded[sn] = std::move(im);
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

} // namespace snap
