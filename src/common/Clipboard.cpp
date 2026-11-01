#include "Clipboard.h"
#include "DragOut.h"
#include <shlobj.h>

namespace snap::clip {

static bool Open(HWND owner) {
    for (int i = 0; i < 10; i++) { if (OpenClipboard(owner)) return true; Sleep(30); }
    return false;
}

bool CopyText(HWND owner, const std::wstring& text) {
    if (!Open(owner)) return false;
    EmptyClipboard();
    bool ok = false;
    size_t cb = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, cb)) {
        memcpy(GlobalLock(hg), text.c_str(), cb);
        GlobalUnlock(hg);
        ok = SetClipboardData(CF_UNICODETEXT, hg) != nullptr;
        if (!ok) GlobalFree(hg);
    }
    CloseClipboard();
    return ok;
}

bool CopyFiles(HWND owner, const std::vector<std::wstring>& paths) {
    if (paths.empty()) return false;
    size_t chars = 1;
    for (auto& p : paths) chars += p.size() + 1;
    HGLOBAL hd = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
    if (!hd) return false;
    auto* df = (DROPFILES*)GlobalLock(hd);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    auto* dst = (wchar_t*)(df + 1);
    for (auto& p : paths) { memcpy(dst, p.c_str(), p.size() * sizeof(wchar_t)); dst += p.size() + 1; }
    GlobalUnlock(hd);
    HGLOBAL he = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (he) { *(DWORD*)GlobalLock(he) = DROPEFFECT_COPY; GlobalUnlock(he); }
    if (!Open(owner)) { GlobalFree(hd); if (he) GlobalFree(he); return false; }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_HDROP, hd) != nullptr;
    if (!ok) GlobalFree(hd);
    static UINT effect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (he && !SetClipboardData(effect, he)) GlobalFree(he);
    CloseClipboard();
    return ok;
}

std::wstring Base64(const Image& img, bool webp) {
    std::vector<BYTE> d;
    if (!EncodeToMemory(img, webp ? ImageFormat::WEBP : ImageFormat::PNG, d)) return L"";
    return Utf8ToW(Base64Encode(d));
}

std::wstring DataUrl(const Image& img, bool webp) {
    std::wstring b = Base64(img, webp);
    return b.empty() ? L"" : std::wstring(webp ? L"data:image/webp;base64," : L"data:image/png;base64,") + b;
}

std::wstring MarkdownImage(const std::wstring& name, const std::wstring& url) {
    std::wstring alt = name;
    for (auto& c : alt) if (c == L'[' || c == L']') c = L' ';
    return L"![" + alt + L"](" + url + L")";
}

} // namespace snap::clip

namespace snap {

bool CopyAsMenu(HWND owner, POINT pt, const Image& img, const std::wstring& file, const std::wstring& title, const std::wstring& url) {
    enum { CImg = 1, CFile, CPath, CB64, CData, CDataWebp, CMd, CUrl };
    bool video = FileExt(file) == L".mp4";
    HMENU m = CreatePopupMenu();
    if (!video) AppendMenuW(m, MF_STRING, CImg, Tr(L"Imagem"));
    AppendMenuW(m, MF_STRING, CFile, Tr(L"Arquivo (para colar no Explorer, e-mail, chat)"));
    AppendMenuW(m, MF_STRING, CPath, Tr(L"Caminho do arquivo"));
    if (!video) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, CB64, Tr(L"Base64 (PNG)"));
        AppendMenuW(m, MF_STRING, CData, Tr(L"Data URL (PNG)"));
        AppendMenuW(m, MF_STRING, CDataWebp, Tr(L"Data URL (WebP)"));
    }
    if (!url.empty()) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, CUrl, Tr(L"Link do upload"));
        if (!video) AppendMenuW(m, MF_STRING, CMd, Tr(L"Imagem em Markdown (link do upload)"));
    }
    SetForegroundWindow(owner);
    int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(m);
    // the file for File / File path: the video itself, or a PNG copy of the image (the library keeps .snap)
    auto realFile = [&]() -> std::wstring {
        if (video || (!file.empty() && FileExt(file) != L".snap")) return file;
        std::wstring name = title.empty() ? L"Snapnab_" + TimeStamp() : title;
        if (FormatFromExt(FileExt(name)) != ImageFormat::Unknown) name = name.substr(0, name.size() - FileExt(name).size());   // "shot.png" → "shot"
        return TempImageFile(img, name);
    };
    switch (id) {
    case CImg: return CopyImageToClipboard(owner, img);
    case CFile: { auto f = realFile(); return !f.empty() && clip::CopyFiles(owner, { f }); }
    case CPath: { auto f = realFile(); return !f.empty() && clip::CopyText(owner, f); }
    case CB64: return clip::CopyText(owner, clip::Base64(img));
    case CData: return clip::CopyText(owner, clip::DataUrl(img));
    case CDataWebp: return clip::CopyText(owner, clip::DataUrl(img, true));
    case CUrl: return clip::CopyText(owner, url);
    case CMd: return clip::CopyText(owner, clip::MarkdownImage(title.empty() ? L"Snapnab" : title, url));
    }
    return false;
}

} // namespace snap
