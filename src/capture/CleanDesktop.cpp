#include "CleanDesktop.h"

namespace snap {

namespace {
constexpr wchar_t kNotifKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings";
constexpr wchar_t kToasts[] = L"NOC_GLOBAL_SETTING_TOASTS_ENABLED";

// -1 = the value does not exist (Windows default: banners on)
int ReadToasts() {
    DWORD v = 0, cb = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, kNotifKey, kToasts, RRF_RT_REG_DWORD, nullptr, &v, &cb) != ERROR_SUCCESS) return -1;
    return (int)v;
}

void WriteToasts(int v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kNotifKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (v < 0) RegDeleteValueW(k, kToasts);
    else { DWORD d = (DWORD)v; RegSetValueExW(k, kToasts, 0, REG_DWORD, (const BYTE*)&d, sizeof d); }
    RegCloseKey(k);
    DWORD_PTR r;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Notifications", SMTO_ABORTIFHUNG, 200, &r);
}

void SaveState(bool icons, int toastsBefore, bool toastsChanged) {
    if (!icons && !toastsChanged) { RegSetString(L"CleanDesktopState", L""); return; }
    RegSetString(L"CleanDesktopState", Format(L"icons=%d;toasts=%d;changed=%d", icons ? 1 : 0, toastsBefore, toastsChanged ? 1 : 0));
}
}

HWND DesktopIconsView() {
    // the icons live under Progman, or under one of the WorkerW windows (wallpaper slideshow / Win+Tab)
    HWND view = nullptr;
    if (HWND prog = FindWindowW(L"Progman", nullptr)) {
        if (HWND def = FindWindowExW(prog, nullptr, L"SHELLDLL_DefView", nullptr)) view = FindWindowExW(def, nullptr, L"SysListView32", nullptr);
    }
    if (!view) {
        HWND w = nullptr;
        while (!view && (w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr)
            if (HWND def = FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr)) view = FindWindowExW(def, nullptr, L"SysListView32", nullptr);
    }
    return view;
}

void CleanDesktop::Begin(bool recording) {
    if (active_) return;
    active_ = true;
    int toastsBefore = ReadToasts();
    if (RegGetDword(L"HideDesktopIcons", 0)) {
        HWND v = DesktopIconsView();
        if (v && IsWindowVisible(v)) { iconsHidden_ = true; }
    }
    toastsChanged_ = recording && RegGetDword(L"DndWhileRecording", 0) && toastsBefore != 0;
    SaveState(iconsHidden_, toastsBefore, toastsChanged_);          // first the record, then the change
    if (iconsHidden_) { ShowWindow(DesktopIconsView(), SW_HIDE); Log(L"Clean desktop: icons hidden"); }
    if (toastsChanged_) { WriteToasts(0); Log(L"Clean desktop: notification banners off"); }
    if (RegGetDword(L"KeepAwake", 1)) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
        awake_ = true;
    }
}

void CleanDesktop::End() {
    if (!active_) return;
    active_ = false;
    std::wstring st = RegGetString(L"CleanDesktopState", L"");
    int toastsBefore = -1;
    if (auto p = st.find(L"toasts="); p != std::wstring::npos) toastsBefore = _wtoi(st.c_str() + p + 7);
    if (iconsHidden_) {
        if (HWND v = DesktopIconsView()) ShowWindow(v, SW_SHOWNA);
        iconsHidden_ = false;
    }
    if (toastsChanged_) { WriteToasts(toastsBefore); toastsChanged_ = false; }
    if (awake_) { SetThreadExecutionState(ES_CONTINUOUS); awake_ = false; }
    RegSetString(L"CleanDesktopState", L"");
}

void CleanDesktop::RestoreAfterCrash() {
    std::wstring st = RegGetString(L"CleanDesktopState", L"");
    if (st.empty()) return;
    Log(L"Clean desktop: restoring after an unexpected exit (%s)", st.c_str());
    if (st.find(L"icons=1") != std::wstring::npos) if (HWND v = DesktopIconsView()) ShowWindow(v, SW_SHOWNA);
    if (st.find(L"changed=1") != std::wstring::npos) {
        int before = -1;
        if (auto p = st.find(L"toasts="); p != std::wstring::npos) before = _wtoi(st.c_str() + p + 7);
        WriteToasts(before);
    }
    RegSetString(L"CleanDesktopState", L"");
}

} // namespace snap
