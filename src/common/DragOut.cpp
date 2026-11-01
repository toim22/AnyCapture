#include "DragOut.h"
#include <shlobj.h>

namespace snap {

namespace {
// IDataObject with the files in two formats: CF_HDROP (Explorer, browsers, messengers, Office) and the Shell item
// list (CFSTR_SHELLIDLIST, used by packaged apps such as Paint and Photos). It also stores the formats that
// IDragSourceHelper writes (the image that follows the cursor).
class FileDropData : public IDataObject {
    LONG ref_ = 1;
    HGLOBAL hdrop_ = nullptr, idlist_ = nullptr;
    CLIPFORMAT cfIdList_ = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
    std::vector<std::pair<FORMATETC, STGMEDIUM>> extra_;
    static HGLOBAL Dup(HGLOBAL h) {
        SIZE_T n = GlobalSize(h);
        HGLOBAL c = GlobalAlloc(GMEM_MOVEABLE, n);
        if (!c) return nullptr;
        void* d = GlobalLock(c); void* s = GlobalLock(h);
        memcpy(d, s, n);
        GlobalUnlock(h); GlobalUnlock(c);
        return c;
    }
public:
    explicit FileDropData(const std::vector<std::wstring>& files) {
        size_t chars = 1;
        for (auto& f : files) chars += f.size() + 1;
        hdrop_ = GlobalAlloc(GHND, sizeof(DROPFILES) + chars * sizeof(wchar_t));
        auto* df = (DROPFILES*)GlobalLock(hdrop_);
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        wchar_t* p = (wchar_t*)((BYTE*)df + sizeof(DROPFILES));
        for (auto& f : files) { memcpy(p, f.c_str(), f.size() * sizeof(wchar_t)); p += f.size(); *p++ = 0; }
        *p = 0;
        GlobalUnlock(hdrop_);
        // CIDA: parent folder = desktop (empty PIDL) and each item with its full PIDL
        std::vector<PIDLIST_ABSOLUTE> pidls;
        for (auto& f : files) { PIDLIST_ABSOLUTE id = nullptr; if (SUCCEEDED(SHParseDisplayName(f.c_str(), nullptr, &id, 0, nullptr)) && id) pidls.push_back(id); }
        if (!pidls.empty()) {
            UINT n = (UINT)pidls.size();
            size_t header = sizeof(UINT) * (n + 2), total = header + 2;
            for (auto id : pidls) total += ILGetSize(id);
            idlist_ = GlobalAlloc(GHND, total);
            BYTE* b = (BYTE*)GlobalLock(idlist_);
            UINT* u = (UINT*)b;
            u[0] = n;
            size_t off = header;
            u[1] = (UINT)off; off += 2;                       // empty PIDL = desktop
            for (UINT i = 0; i < n; i++) {
                UINT sz = ILGetSize(pidls[i]);
                u[2 + i] = (UINT)off;
                memcpy(b + off, pidls[i], sz);
                off += sz;
            }
            GlobalUnlock(idlist_);
        }
        for (auto id : pidls) CoTaskMemFree(id);
    }
    ~FileDropData() {
        if (hdrop_) GlobalFree(hdrop_);
        if (idlist_) GlobalFree(idlist_);
        for (auto& e : extra_) ReleaseStgMedium(&e.second);
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** pv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) { *pv = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
        *pv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&ref_); if (!r) delete this; return r; }
    STDMETHODIMP GetData(FORMATETC* fe, STGMEDIUM* m) override {
        if (!fe || !m) return E_INVALIDARG;
        if ((fe->cfFormat == CF_HDROP || (fe->cfFormat == cfIdList_ && idlist_)) && (fe->tymed & TYMED_HGLOBAL)) {
            m->tymed = TYMED_HGLOBAL; m->hGlobal = Dup(fe->cfFormat == CF_HDROP ? hdrop_ : idlist_); m->pUnkForRelease = nullptr;
            return m->hGlobal ? S_OK : E_OUTOFMEMORY;
        }
        for (auto& e : extra_)
            if (e.first.cfFormat == fe->cfFormat && (e.first.tymed & fe->tymed) && e.second.tymed == TYMED_HGLOBAL) {
                m->tymed = TYMED_HGLOBAL; m->hGlobal = Dup(e.second.hGlobal); m->pUnkForRelease = nullptr;
                return m->hGlobal ? S_OK : E_OUTOFMEMORY;
            }
        return DV_E_FORMATETC;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryGetData(FORMATETC* fe) override {
        if (!fe) return E_INVALIDARG;
        if ((fe->cfFormat == CF_HDROP || (fe->cfFormat == cfIdList_ && idlist_)) && (fe->tymed & TYMED_HGLOBAL)) return S_OK;
        for (auto& e : extra_) if (e.first.cfFormat == fe->cfFormat && (e.first.tymed & fe->tymed)) return S_OK;
        return DV_E_FORMATETC;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override { if (out) out->ptd = nullptr; return DATA_S_SAMEFORMATETC; }
    STDMETHODIMP SetData(FORMATETC* fe, STGMEDIUM* m, BOOL release) override {
        if (!fe || !m || m->tymed != TYMED_HGLOBAL) return E_NOTIMPL;
        STGMEDIUM copy{};
        copy.tymed = TYMED_HGLOBAL;
        copy.hGlobal = release ? m->hGlobal : Dup(m->hGlobal);
        for (auto it = extra_.begin(); it != extra_.end(); ++it)
            if (it->first.cfFormat == fe->cfFormat) { ReleaseStgMedium(&it->second); extra_.erase(it); break; }
        FORMATETC f = *fe; f.ptd = nullptr;
        extra_.push_back({ f, copy });
        return S_OK;
    }
    STDMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** e) override {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        std::vector<FORMATETC> list = { { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL } };
        if (idlist_) list.push_back({ cfIdList_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL });
        for (auto& x : extra_) list.push_back(x.first);
        return SHCreateStdEnumFmtEtc((UINT)list.size(), list.data(), e);
    }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

} // namespace

DWORD DragFilesOut(HWND source, const std::vector<std::wstring>& files, const Image* thumb, bool allowMove) {
    if (files.empty()) return 0;
    auto* obj = new FileDropData(files);
    IDragSourceHelper* helper = nullptr;
    if (thumb && !thumb->empty() && SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper)))) {
        int maxS = MulDiv(96, DpiForWindow(source), 96);
        double k = std::min({ 1.0, (double)maxS / thumb->w, (double)maxS / thumb->h });
        Image img = ScaleImage(*thumb, std::max(1, (int)(thumb->w * k)), std::max(1, (int)(thumb->h * k)));
        for (auto& px : img.px) { int a = CA(px) * 220 / 255; px = ARGB(a, CR(px) * a / 255, CG(px) * a / 255, CB(px) * a / 255); }
        SHDRAGIMAGE di{};
        di.sizeDragImage = { img.w, img.h };
        di.ptOffset = { img.w / 2, img.h / 2 };
        di.hbmpDragImage = img.ToHBITMAP();
        di.crColorKey = CLR_NONE;
        if (FAILED(helper->InitializeFromBitmap(&di, obj))) DeleteObject(di.hbmpDragImage);
        helper->Release();
    }
    // Explorer follows the preferred effect (copy); some apps, like Paint, only accept it when "move" is also allowed.
    // Move only applies to temporary copies: an original file (a video of the library) cannot leave its folder
    FORMATETC pref{ (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM prefData{};
    prefData.tymed = TYMED_HGLOBAL;
    if ((prefData.hGlobal = GlobalAlloc(GHND, sizeof(DWORD))) != nullptr) {
        *(DWORD*)GlobalLock(prefData.hGlobal) = DROPEFFECT_COPY;
        GlobalUnlock(prefData.hGlobal);
        obj->SetData(&pref, &prefData, TRUE);
    }
    DWORD allowed = DROPEFFECT_COPY | DROPEFFECT_LINK | (allowMove ? DROPEFFECT_MOVE : 0);
    DWORD effect = 0;
    HRESULT hr = SHDoDragDrop(source, obj, nullptr, allowed, &effect);
    obj->Release();
    return SUCCEEDED(hr) && hr != DRAGDROP_S_CANCEL ? effect : 0;
}

std::wstring TempImageFile(const Image& img, const std::wstring& nameIn, bool strip) {
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"Snapnab";
    EnsureDir(dir);
    std::wstring name = nameIn.empty() ? L"Snapnab_" + TimeStamp() : nameIn;
    for (auto& c : name) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    std::wstring path = dir + L"\\" + name + L".png";
    SaveOptions o = SaveOptions::FromSettings();
    if (strip) o.stripMetadata = true;
    return SaveImageFileEx(path, img, o) ? path : L"";
}

} // namespace snap
