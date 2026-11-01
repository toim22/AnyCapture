// Clean desktop while capturing and recording (C16; ShareX dossier 01 · A3 DesktopIconManager: Progman →
// SHELLDLL_DefView → SysListView32, ShowWindow). Options (registry): HideDesktopIcons (captures and recordings),
// DndWhileRecording (turns Windows notification banners off while recording), KeepAwake (no screen dimming or sleep
// while recording or during the capture countdown). The state changed is written first to CleanDesktopState so
// that, after a crash, the next start of Snapnab puts everything back (RestoreAfterCrash).
#pragma once
#include "Common.h"

namespace snap {

class CleanDesktop {
public:
    void Begin(bool recording);      // applies the options that are on (idempotent)
    void End();                      // restores what Begin changed
    bool active() const { return active_; }
    static void RestoreAfterCrash(); // at startup
private:
    bool active_ = false, iconsHidden_ = false, toastsChanged_ = false, awake_ = false;
};

HWND DesktopIconsView();             // the desktop's SysListView32 (nullptr if not found)

} // namespace snap
