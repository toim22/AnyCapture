// Screen focus for presentations (C39; ShareX has only a spotlight annotation — dossier 01 · H/SpotlightTool, 02 · E.1):
// dims the whole screen except a circle or rectangle around the pointer until the hotkey (or the tray item) turns it
// off. The window lets every click through. Settings: FocusShape (0 circle, 1 rectangle), FocusSize (radius in pixels),
// FocusDarkness (%), FocusHotkey.
#pragma once
#include "Common.h"

namespace snap {

class ScreenFocus {
public:
    void Toggle();
    void Stop();
    bool active() const { return wnd_ != nullptr; }
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Update();
    HWND wnd_ = nullptr;
    POINT last_{ -1, -1 };
    RECT vs_{};
};

} // namespace snap
