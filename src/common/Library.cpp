// The capture library is read and written by rust/snapcore/src/library.rs; this keeps the C++ API (Library.h) and
// scales the thumbnails (MakeThumbnail, GDI+), which stay in C++ for now.
#include "Library.h"
#include "SnapcoreBridge.h"

namespace snap {

namespace {

// the string snapcore wrote into *out, or "" if the call failed
template <class F> std::wstring Str(F call) {
    SnapcoreWStr s{};
    if (!call(&s)) return L"";
    std::wstring r = s.len ? std::wstring(s.data, s.len) : std::wstring();
    snapcore_wstr_free(s);
    return r;
}

void AddItem(void* ctx, const wchar_t* path, size_t len, uint64_t modified, uint64_t size, bool video) {
    LibraryItem it;
    it.path.assign(path, len);
    it.modified.dwLowDateTime = (DWORD)modified;
    it.modified.dwHighDateTime = (DWORD)(modified >> 32);
    it.size = size;
    it.video = video;
    static_cast<std::vector<LibraryItem>*>(ctx)->push_back(std::move(it));
}

Image Thumbnail(const Image& img) { return MakeThumbnail(img, 160, 120); }

std::wstring Add(const Image& base, const Image& thumbOf, const CaptureInfo& info, const XmlNode* objects,
                 const std::map<std::string, Image>* embedded) {
    Image thumb = Thumbnail(thumbOf);
    std::vector<SnapcoreNamedImage> emb;
    if (embedded)
        for (auto& [name, img] : *embedded) emb.push_back({ name.data(), name.size(), ImageIn(img) });
    SnapcoreLibraryAdd r{};
    r.base = ImageIn(base);
    r.thumb = ImageIn(thumb);
    r.mode = info.mode.c_str(); r.windowTitle = info.windowTitle.c_str(); r.application = info.application.c_str();
    r.objects = objects ? XmlToSnapcore(*objects) : nullptr;
    r.embedded = emb.data(); r.embeddedCount = emb.size();
    r.version = kAppVersion;
    return Str([&](SnapcoreWStr* out) { return snapcore_library_add(&r, out); });
}

} // namespace

std::vector<LibraryItem> ListLibrary(size_t maxItems) {
    std::vector<LibraryItem> items;
    snapcore_library_list(nullptr, 0, maxItems, AddItem, &items);
    return items;
}

std::wstring ThumbPathFor(const std::wstring& itemPath) {
    return Str([&](SnapcoreWStr* out) { return snapcore_library_thumb_path(nullptr, 0, itemPath.data(), itemPath.size(), out); });
}

bool WriteThumbnail(const std::wstring& itemPath, const Image& img) {
    Image t = Thumbnail(img);
    SnapcoreImageIn in = ImageIn(t);
    return !t.empty() && snapcore_library_write_thumbnail(nullptr, 0, itemPath.data(), itemPath.size(), &in, kAppVersion);
}

Image LoadThumbnail(const std::wstring& itemPath) {
    Image t;
    std::wstring tp = ThumbPathFor(itemPath);
    if (FileExists(tp)) LoadImageFile(tp, t);
    return t;
}

std::wstring AddImageToLibrary(const Image& img, const CaptureInfo& info) {
    return Add(img, img, info, nullptr, nullptr);
}

std::wstring AddLayeredToLibrary(const Image& base, const Image& flat, const CaptureInfo& info, const XmlNode& objects,
                                 const std::map<std::string, Image>& embedded) {
    return Add(base, flat, info, &objects, &embedded);
}

std::wstring NewVideoPath() {
    return Str([](SnapcoreWStr* out) { return snapcore_library_new_video_path(nullptr, 0, out); });
}

std::wstring AudioDir() {
    return Str([](SnapcoreWStr* out) { return snapcore_library_audio_dir(out); });
}

std::wstring NewAudioPath(bool mp3) {
    return Str([&](SnapcoreWStr* out) { return snapcore_library_new_audio_path(mp3, out); });
}

bool DeleteLibraryItem(const std::wstring& itemPath, HWND owner) {
    return snapcore_library_delete(nullptr, 0, itemPath.data(), itemPath.size(), owner);
}

int CleanupLibrary(int days, bool keepNamed, bool keepTagged, bool recycle) {
    return snapcore_library_cleanup(nullptr, 0, days, keepNamed, keepTagged, recycle);
}

int CleanupLibraryNow() { return snapcore_library_cleanup_now(); }

bool IsInLibrary(const std::wstring& path) {
    return snapcore_library_contains(nullptr, 0, path.data(), path.size());
}

std::wstring CaptureName(const std::wstring& itemPath) {
    return Str([&](SnapcoreWStr* out) { return snapcore_library_name(nullptr, 0, itemPath.data(), itemPath.size(), out); });
}

bool SetCaptureName(const std::wstring& itemPath, const std::wstring& name) {
    return snapcore_library_set_name(nullptr, 0, itemPath.data(), itemPath.size(), name.data(), name.size());
}

std::wstring NextAutoName(const std::wstring& folder, const std::wstring& prefix, const std::wstring& ext, int digits) {
    return Str([&](SnapcoreWStr* out) {
        return snapcore_next_auto_name(folder.data(), folder.size(), prefix.data(), prefix.size(), ext.data(), ext.size(), digits, out);
    });
}

} // namespace snap
