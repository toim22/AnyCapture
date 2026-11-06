// Library.cpp as it was before it moved to Rust (rust/snapcore/src/library.rs), kept only to check that the Rust code
// gives the same results (tests.cpp, [20] "Capture library: Rust and the former C++"). Two changes: the library folder
// is a parameter (the tests use folders of their own instead of the user's library), and the names are read again when
// that folder changes. LibraryDir is the former one of Util.cpp.
#include "Library.h"
#include <shlobj.h>

namespace snap::libref {

std::wstring LibraryDir() {
    std::wstring custom = RegGetString(L"LibraryFolder", L"");
    auto d = custom.empty() ? AppDataDir() + L"\\Library" : custom;
    EnsureDir(d); return d;
}

static std::wstring ThumbDir(const std::wstring& lib) { auto d = lib + L"\\Thumbnails"; EnsureDir(d); return d; }
std::wstring CaptureName(const std::wstring& lib, const std::wstring& itemPath);
bool SetCaptureName(const std::wstring& lib, const std::wstring& itemPath, const std::wstring& name);

std::vector<LibraryItem> ListLibrary(const std::wstring& lib, size_t maxItems) {
    std::vector<LibraryItem> items;
    std::wstring dir = lib;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return items;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring ext = FileExt(fd.cFileName);
        if (ext != L".snap" && ext != L".mp4") continue;
        LibraryItem it;
        it.path = dir + L"\\" + fd.cFileName;
        it.modified = fd.ftLastWriteTime;
        it.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        it.video = ext == L".mp4";
        items.push_back(it);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(items.begin(), items.end(), [](const LibraryItem& a, const LibraryItem& b) {
        return CompareFileTime(&a.modified, &b.modified) > 0;
    });
    if (items.size() > maxItems) items.resize(maxItems);
    return items;
}

std::wstring ThumbPathFor(const std::wstring& lib, const std::wstring& itemPath) {
    return ThumbDir(lib) + L"\\" + FileNameOf(itemPath) + L".png";
}

bool WriteThumbnail(const std::wstring& lib, const std::wstring& itemPath, const Image& img) {
    Image t = MakeThumbnail(img, 160, 120);
    return !t.empty() && SaveImageFile(ThumbPathFor(lib, itemPath), t);
}

std::wstring AddImageToLibrary(const std::wstring& lib, const Image& img, const CaptureInfo& info) {
    std::wstring path = lib + L"\\" + NewGuidString() + L".snap";
    SnapFrame f;
    f.base = img;
    f.doc.setW("CaptureMode", info.mode);
    f.doc.setW("WindowTitle", info.windowTitle);
    f.doc.setW("Application", info.application);
    f.doc.setW("Created", TimeStamp(L"%04d-%02d-%02dT%02d:%02d:%02d"));
    if (!SaveSnapFile(path, { f })) return L"";
    WriteThumbnail(lib, path, img);
    Log(L"Library: added %s (%dx%d, %s)", path.c_str(), img.w, img.h, info.mode.c_str());
    return path;
}

std::wstring AddLayeredToLibrary(const std::wstring& lib, const Image& base, const Image& flat, const CaptureInfo& info, const XmlNode& objects,
                                 const std::map<std::string, Image>& embedded) {
    std::wstring path = lib + L"\\" + NewGuidString() + L".snap";
    SnapFrame f;
    f.base = base;
    f.doc.setW("CaptureMode", info.mode);
    f.doc.setW("WindowTitle", info.windowTitle);
    f.doc.setW("Application", info.application);
    f.doc.setW("Created", TimeStamp(L"%04d-%02d-%02dT%02d:%02d:%02d"));
    f.doc.children.push_back(objects);
    f.embedded = embedded;
    if (!SaveSnapFile(path, { f })) return L"";
    WriteThumbnail(lib, path, flat);
    Log(L"Library: added %s (%dx%d, %s, %zu object(s))", path.c_str(), base.w, base.h, info.mode.c_str(), objects.children.size());
    return path;
}

std::wstring NewVideoPath(const std::wstring& lib) {
    return lib + L"\\Video_" + TimeStamp() + L".mp4";
}

std::wstring AudioDir() {
    std::wstring d = RegGetString(L"AudioFolder", L"");
    if (d.empty()) {
        PWSTR music = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &music))) d = std::wstring(music) + L"\\Snapnab";
        CoTaskMemFree(music);
        if (d.empty()) d = AppDataDir() + L"\\Audio";
    }
    EnsureDir(d);
    return d;
}

std::wstring NewAudioPath(bool mp3) {
    return AudioDir() + L"\\" + Tr(L"Audio") + L"_" + TimeStamp() + (mp3 ? L".mp3" : L".m4a");
}

bool DeleteLibraryItem(const std::wstring& lib, const std::wstring& itemPath, HWND owner) {
    std::wstring from = itemPath; from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.hwnd = owner; op.wFunc = FO_DELETE; op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;      // goes to the Recycle Bin
    bool ok = SHFileOperationW(&op) == 0;
    DeleteFileW(ThumbPathFor(lib, itemPath).c_str());
    if (ok && !CaptureName(lib, itemPath).empty()) SetCaptureName(lib, itemPath, L"");      // the name goes with it
    return ok;
}

int CleanupLibrary(const std::wstring& lib, int days, bool keepNamed, bool keepTagged, bool recycle) {
    if (days <= 0) return 0;
    FILETIME nowFt; GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now{ nowFt.dwLowDateTime, nowFt.dwHighDateTime };
    const ULONGLONG limit = (ULONGLONG)days * 24 * 3600 * 10000000ULL;
    int removed = 0;
    for (auto& it : ListLibrary(lib, 1000000)) {
        ULARGE_INTEGER t{ it.modified.dwLowDateTime, it.modified.dwHighDateTime };
        if (now.QuadPart < t.QuadPart || now.QuadPart - t.QuadPart < limit) continue;
        if (keepNamed && !CaptureName(lib, it.path).empty()) continue;
        if (keepTagged && !it.video) {
            XmlNode doc;
            if (LoadSnapDocument(it.path, doc) && !Trim(doc.getW("Tags")).empty()) continue;
        }
        bool ok;
        if (recycle) ok = DeleteLibraryItem(lib, it.path, nullptr);
        else {
            ok = DeleteFileW(it.path.c_str()) != 0;
            DeleteFileW(ThumbPathFor(lib, it.path).c_str());
            if (ok && !CaptureName(lib, it.path).empty()) SetCaptureName(lib, it.path, L"");
        }
        if (ok) { removed++; Log(L"Cleanup: removed %s", it.path.c_str()); }
    }
    return removed;
}

bool IsInLibrary(const std::wstring& libDir, const std::wstring& path) {
    std::wstring lib = ToLower(libDir);
    return ToLower(DirOf(path)) == lib;
}

// ---------------------------------------------------------------- capture names
namespace {
struct NameIndex { std::map<std::wstring, std::wstring> names; FILETIME stamp{}; bool loaded = false; std::wstring file; };
NameIndex& Names() { static NameIndex n; return n; }
std::wstring NamesFile(const std::wstring& lib) { return lib + L"\\Names.xml"; }

void LoadNames(const std::wstring& lib) {
    NameIndex& n = Names();
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    bool exists = GetFileAttributesExW(NamesFile(lib).c_str(), GetFileExInfoStandard, &fa) != 0;
    if (n.loaded && n.file == NamesFile(lib) && (!exists || CompareFileTime(&fa.ftLastWriteTime, &n.stamp) == 0)) return;   // reloaded when the other program changed it
    n.loaded = true;
    n.file = NamesFile(lib);
    n.names.clear();
    n.stamp = exists ? fa.ftLastWriteTime : FILETIME{};
    XmlNode root;
    if (!exists || !XmlLoadFile(NamesFile(lib), root)) return;
    for (auto& c : root.children) if (c.name == "Item") n.names[ToLower(c.getW("File"))] = c.getW("Name");
}
}

std::wstring CaptureName(const std::wstring& lib, const std::wstring& itemPath) {
    LoadNames(lib);
    auto it = Names().names.find(ToLower(FileNameOf(itemPath)));
    return it == Names().names.end() ? L"" : it->second;
}

bool SetCaptureName(const std::wstring& lib, const std::wstring& itemPath, const std::wstring& name) {
    LoadNames(lib);
    NameIndex& n = Names();
    std::wstring key = ToLower(FileNameOf(itemPath)), v = Trim(name);
    if (v.empty()) n.names.erase(key); else n.names[key] = v;
    XmlNode root("Names");
    for (auto& [file, nm] : n.names) root.add("Item").setW("File", file).setW("Name", nm);
    bool ok = XmlSaveFile(NamesFile(lib), root);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(NamesFile(lib).c_str(), GetFileExInfoStandard, &fa)) n.stamp = fa.ftLastWriteTime;
    return ok;
}

std::wstring NextAutoName(const std::wstring& folder, const std::wstring& prefix, const std::wstring& ext, int digits) {
    for (int n = 1; n < 1000000; n++) {
        std::wstring p = folder + L"\\" + prefix + Format(L"%0*d", digits, n) + ext;
        if (!FileExists(p)) return p;
    }
    return folder + L"\\" + prefix + NewGuidString() + ext;
}

} // namespace snap::libref
