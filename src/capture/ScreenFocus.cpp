#include "ScreenFocus.h"

namespace snap {

void ScreenFocus::Toggle() { if (wnd_) Stop(); else {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SnapnabScreenFocus";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc); reg = true;
    }
    vs_ = VirtualScreenRect();
    wnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"SnapnabScreenFocus",
                           L"", WS_POPUP, vs_.left, vs_.top, vs_.right - vs_.left, vs_.bottom - vs_.top, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(wnd_, GWLP_USERDATA, (LONG_PTR)this);
    int dark = (int)std::clamp<DWORD>(RegGetDword(L"FocusDarkness", 60), 10, 95);
    SetLayeredWindowAttributes(wnd_, 0, (BYTE)(dark * 255 / 100), LWA_ALPHA);
    last_ = { -1, -1 };
    Update();
    ShowWindow(wnd_, SW_SHOWNOACTIVATE);
    SetTimer(wnd_, 1, 15, nullptr);               // follows the pointer
    Log(L"Screen focus: on");
} }

void ScreenFocus::Stop() {
    if (!wnd_) return;
    DestroyWindow(wnd_);
    wnd_ = nullptr;
    Log(L"Screen focus: off");
}

void ScreenFocus::Update() {
    POINT p; GetCursorPos(&p);
    if (p.x == last_.x && p.y == last_.y) return;
    last_ = p;
    int r = (int)std::clamp<DWORD>(RegGetDword(L"FocusSize", 160), 40, 1200);
    int x = p.x - vs_.left, y = p.y - vs_.top;
    HRGN all = CreateRectRgn(0, 0, vs_.right - vs_.left, vs_.bottom - vs_.top);
    HRGN hole = RegGetDword(L"FocusShape", 0) == 1 ? CreateRoundRectRgn(x - r * 3 / 2, y - r, x + r * 3 / 2, y + r, 24, 24)
                                                    : CreateEllipticRgn(x - r, y - r, x + r, y + r);
    CombineRgn(all, all, hole, RGN_DIFF);
    DeleteObject(hole);
    SetWindowRgn(wnd_, all, TRUE);               // the window owns the region from here on
}

LRESULT CALLBACK ScreenFocus::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = (ScreenFocus*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_TIMER && self) { self->Update(); return 0; }
    if (m == WM_DISPLAYCHANGE && self) { self->Stop(); return 0; }
    return DefWindowProcW(h, m, w, l);
}

} // namespace snap
