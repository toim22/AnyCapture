#include "Office.h"
#include <oleauto.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace snap::office {

namespace {
const wchar_t* ProgId(Target t) {
    switch (t) {
    case Word: return L"Word.Application";
    case PowerPoint: return L"PowerPoint.Application";
    case Excel: return L"Excel.Application";
    default: return L"OneNote.Application";
    }
}

struct Var {                         // VARIANT that clears itself
    VARIANT v;
    Var() { VariantInit(&v); }
    explicit Var(const std::wstring& s) { VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(s.c_str()); }
    explicit Var(int i) { VariantInit(&v); v.vt = VT_I4; v.lVal = i; }
    explicit Var(float f) { VariantInit(&v); v.vt = VT_R4; v.fltVal = f; }
    explicit Var(bool b) { VariantInit(&v); v.vt = VT_BOOL; v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; }
    ~Var() { VariantClear(&v); }
    Var(const Var&) = delete;
    Var& operator=(const Var&) = delete;
    IDispatch* disp() const { return v.vt == VT_DISPATCH ? v.pdispVal : nullptr; }
};

// late-bound call: method or property get/put with the arguments in order (IDispatch wants them reversed)
HRESULT Call(IDispatch* d, WORD flags, const wchar_t* name, Var* result, std::initializer_list<const Var*> args = {}) {
    if (!d) return E_POINTER;
    DISPID id;
    LPOLESTR n = (LPOLESTR)name;
    HRESULT hr = d->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;
    std::vector<VARIANT> a;
    for (auto it = std::rbegin(args); it != std::rend(args); ++it) a.push_back((*it)->v);
    DISPPARAMS dp{ a.empty() ? nullptr : a.data(), nullptr, (UINT)a.size(), 0 };
    DISPID put = DISPID_PROPERTYPUT;
    if (flags & DISPATCH_PROPERTYPUT) { dp.cNamedArgs = 1; dp.rgdispidNamedArgs = &put; }
    EXCEPINFO ei{};
    return d->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result ? &result->v : nullptr, &ei, nullptr);
}

HRESULT Get(IDispatch* d, const wchar_t* name, Var& out, std::initializer_list<const Var*> args = {}) {
    return Call(d, DISPATCH_PROPERTYGET | DISPATCH_METHOD, name, &out, args);
}

// the running instance of the program, or a new one made visible
bool Connect(Target t, ComPtr<IDispatch>& app, bool& started, std::wstring& err) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(ProgId(t), &clsid))) {
        err = Format(Tr(L"O %s não está instalado neste computador (ou é a versão da Microsoft Store, sem automação)."), TargetName(t));
        return false;
    }
    started = false;
    ComPtr<IUnknown> unk;
    if (SUCCEEDED(GetActiveObject(clsid, nullptr, &unk)) && unk && SUCCEEDED(unk.As(&app))) return true;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&app)))) {
        err = Format(Tr(L"Não foi possível abrir o %s."), TargetName(t));
        return false;
    }
    started = true;
    if (t != OneNote) { Var vis(true); Call(app.Get(), DISPATCH_PROPERTYPUT, L"Visible", nullptr, { &vis }); }
    return true;
}
}

const wchar_t* TargetName(Target t) {
    switch (t) {
    case Word: return L"Word";
    case PowerPoint: return L"PowerPoint";
    case Excel: return L"Excel";
    default: return L"OneNote";
    }
}

bool Available(Target t) { CLSID c; return SUCCEEDED(CLSIDFromProgID(ProgId(t), &c)); }

bool Send(Target t, const std::wstring& path, int w, int h, std::wstring& err) {
    HRESULT ci = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Uninit { HRESULT hr; ~Uninit() { if (SUCCEEDED(hr)) CoUninitialize(); } } un{ ci };
    ComPtr<IDispatch> app;
    bool started = false;
    if (!Connect(t, app, started, err)) return false;
    Var file(path), no(false), yes(true);
    HRESULT hr = E_FAIL;
    switch (t) {
    case Word: {
        Var docs, count;
        Get(app.Get(), L"Documents", docs);
        Get(docs.disp(), L"Count", count);
        if (count.v.vt == VT_I4 && count.v.lVal == 0) { Var nd; Call(docs.disp(), DISPATCH_METHOD, L"Add", &nd); }
        Var sel, shapes, pic;
        Get(app.Get(), L"Selection", sel);
        Get(sel.disp(), L"InlineShapes", shapes);
        hr = Call(shapes.disp(), DISPATCH_METHOD, L"AddPicture", &pic, { &file, &no, &yes });      // FileName, LinkToFile, SaveWithDocument
        break;
    }
    case PowerPoint: {
        Var pres, count, p;
        Get(app.Get(), L"Presentations", pres);
        Get(pres.disp(), L"Count", count);
        if (count.v.vt == VT_I4 && count.v.lVal == 0) Call(pres.disp(), DISPATCH_METHOD, L"Add", &p);
        else Get(app.Get(), L"ActivePresentation", p);
        Var slides, slide, setup, sw, sh;
        Get(p.disp(), L"Slides", slides);
        Get(p.disp(), L"PageSetup", setup);
        Get(setup.disp(), L"SlideWidth", sw);
        Get(setup.disp(), L"SlideHeight", sh);
        bool newSlide = RegGetDword(L"OfficeNewSlide", 1) != 0;
        Var scount; Get(slides.disp(), L"Count", scount);
        int n = scount.v.vt == VT_I4 ? scount.v.lVal : 0;
        int current = 0;
        {   // the slide shown in the window (0 when there is no window or slide yet)
            Var win, view, cur, idx;
            if (SUCCEEDED(Get(app.Get(), L"ActiveWindow", win)) && SUCCEEDED(Get(win.disp(), L"View", view)) &&
                SUCCEEDED(Get(view.disp(), L"Slide", cur)) && SUCCEEDED(Get(cur.disp(), L"SlideIndex", idx)) && idx.v.vt == VT_I4)
                current = idx.v.lVal;
        }
        if (newSlide || n == 0 || current == 0) {
            Var index(current > 0 ? current + 1 : n + 1), blank(12);         // ppLayoutBlank
            hr = Call(slides.disp(), DISPATCH_METHOD, L"Add", &slide, { &index, &blank });
            if (SUCCEEDED(hr)) {
                Var win, view; if (SUCCEEDED(Get(app.Get(), L"ActiveWindow", win)) && SUCCEEDED(Get(win.disp(), L"View", view))) {
                    Var idx(current > 0 ? current + 1 : n + 1); Var r; Call(view.disp(), DISPATCH_METHOD, L"GotoSlide", &r, { &idx });
                }
            }
        } else { Var index(current); hr = Call(slides.disp(), DISPATCH_METHOD, L"Item", &slide, { &index }); }
        if (FAILED(hr)) break;
        // the picture fitted inside the slide (with a margin), centered; pixels at 96 DPI = 0.75 pt
        double W = sw.v.vt == VT_R4 ? sw.v.fltVal : 960, H = sh.v.vt == VT_R4 ? sh.v.fltVal : 540;
        double pw = w * 0.75, ph = h * 0.75, k = std::min({ 1.0, W * 0.9 / pw, H * 0.9 / ph });
        pw *= k; ph *= k;
        Var shapes, pic;
        Get(slide.disp(), L"Shapes", shapes);
        Var left((float)((W - pw) / 2)), top((float)((H - ph) / 2)), width((float)pw), height((float)ph), msoFalse(0), msoTrue(-1);
        hr = Call(shapes.disp(), DISPATCH_METHOD, L"AddPicture", &pic, { &file, &msoFalse, &msoTrue, &left, &top, &width, &height });
        break;
    }
    case Excel: {
        Var books, count, sheet, cell, l, tp;
        Get(app.Get(), L"Workbooks", books);
        Get(books.disp(), L"Count", count);
        if (count.v.vt == VT_I4 && count.v.lVal == 0) { Var nb; Call(books.disp(), DISPATCH_METHOD, L"Add", &nb); }
        Get(app.Get(), L"ActiveSheet", sheet);
        Get(app.Get(), L"ActiveCell", cell);
        Get(cell.disp(), L"Left", l);
        Get(cell.disp(), L"Top", tp);
        VariantChangeType(&l.v, &l.v, 0, VT_R4);
        VariantChangeType(&tp.v, &tp.v, 0, VT_R4);
        Var shapes, pic, msoFalse(0), msoTrue(-1), width((float)(w * 0.75)), height((float)(h * 0.75));
        Get(sheet.disp(), L"Shapes", shapes);
        hr = Call(shapes.disp(), DISPATCH_METHOD, L"AddPicture", &pic, { &file, &msoFalse, &msoTrue, &l, &tp, &width, &height });
        break;
    }
    case OneNote: {
        Var windows, cur, pageId;
        Get(app.Get(), L"Windows", windows);
        Get(windows.disp(), L"CurrentWindow", cur);
        Get(cur.disp(), L"CurrentPageId", pageId);
        std::wstring id = pageId.v.vt == VT_BSTR && pageId.v.bstrVal ? pageId.v.bstrVal : L"";
        if (id.empty()) { err = Tr(L"Abra uma página no OneNote antes de enviar a captura."); return false; }
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        std::vector<BYTE> data;
        if (f != INVALID_HANDLE_VALUE) {
            DWORD n = GetFileSize(f, nullptr), rd = 0;
            data.resize(n);
            ReadFile(f, data.data(), n, &rd, nullptr);
            CloseHandle(f);
        }
        std::wstring xml = L"<?xml version=\"1.0\"?><one:Page xmlns:one=\"http://schemas.microsoft.com/office/onenote/2013/onenote\" ID=\"" + id +
                           L"\"><one:Outline><one:OEChildren><one:OE><one:Image><one:Size width=\"" + std::to_wstring(w * 0.75) + L"\" height=\"" +
                           std::to_wstring(h * 0.75) + L"\" isSetByUser=\"true\"/><one:Data>" + Utf8ToW(Base64Encode(data)) +
                           L"</one:Data></one:Image></one:OE></one:OEChildren></one:Outline></one:Page>";
        Var x(xml), r;
        hr = Call(app.Get(), DISPATCH_METHOD, L"UpdatePageContent", &r, { &x });
        break;
    }
    default: break;
    }
    if (FAILED(hr)) {
        if (err.empty()) err = Format(Tr(L"O %s recusou a imagem (0x%08X)."), TargetName(t), hr);
        Log(L"Office: %s failed 0x%08X", TargetName(t), hr);
        return false;
    }
    Log(L"Office: picture sent to %s", TargetName(t));
    return true;
}

} // namespace snap::office
