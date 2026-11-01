#include "ColorPicker.h"
#include "Color.h"
#include "Image.h"
#include "Ui.h"

namespace snap {

bool ColorFormatsMenu(HWND owner, COLORREF c, POINT pt) {
    HMENU m = CreatePopupMenu();
    color::Fmt def = color::DefaultFmt();
    for (int f = 0; f < color::FmtCount; f++) {
        std::wstring t = color::ToText(c, (color::Fmt)f) + L"\t" + color::FmtName((color::Fmt)f);
        AppendMenuW(m, MF_STRING | (f == def ? MF_DEFAULT : 0), 100 + f, t.c_str());
    }
    auto recent = color::Recent();
    if (!recent.empty()) {
        HMENU rm = CreatePopupMenu();
        for (size_t i = 0; i < recent.size(); i++) AppendMenuW(rm, MF_STRING, 200 + i, color::ToText(recent[i], def).c_str());
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)rm, Tr(L"Cores recentes"));
    }
    HMENU dm = CreatePopupMenu();
    for (int f = 0; f < color::FmtCount; f++)
        AppendMenuW(dm, MF_STRING | (f == def ? MF_CHECKED : 0), 300 + f, color::FmtName((color::Fmt)f));
    AppendMenuW(m, MF_POPUP, (UINT_PTR)dm, Tr(L"Formato padrão"));
    SetForegroundWindow(owner);
    int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(m);
    if (id >= 100 && id < 100 + color::FmtCount) { color::CopyText(owner, color::ToText(c, (color::Fmt)(id - 100))); return true; }
    if (id >= 200 && id < 200 + (int)recent.size()) { color::CopyText(owner, color::ToText(recent[id - 200], def)); color::AddRecent(recent[id - 200]); return true; }
    if (id >= 300 && id < 300 + color::FmtCount) { color::SetDefaultFmt((color::Fmt)(id - 300)); color::CopyText(owner, color::ToText(c, (color::Fmt)(id - 300))); return true; }
    return false;
}

namespace {

struct Picker {
    Image frozen;
    RECT vs{};
    HBITMAP bmp = nullptr;
    HDC dc = nullptr;
    HWND hwnd = nullptr;
    POINT cur{};
    std::function<void(bool, COLORREF)> done;
    bool finished = false;
};

COLORREF At(Picker* p, POINT c) {
    int x = std::clamp((int)c.x, 0, p->frozen.w - 1), y = std::clamp((int)c.y, 0, p->frozen.h - 1);
    return ToColorref(p->frozen.at(x, y));
}

void Finish(Picker* p, bool ok) {
    if (p->finished) return;
    p->finished = true;
    COLORREF c = At(p, p->cur);
    auto done = p->done;
    DestroyWindow(p->hwnd);
    if (done) done(ok, c);
}

void Paint(Picker* p, HDC hdc) {
    RECT cr; GetClientRect(p->hwnd, &cr);
    int W = cr.right, H = cr.bottom, dpi = DpiForWindow(p->hwnd);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bb = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ ob = SelectObject(mem, bb);
    BitBlt(mem, 0, 0, W, H, p->dc, 0, 0, SRCCOPY);
    // magnifier: 15 × 15 pixels at 10× with the center pixel outlined
    const int N = 15, Z = ui::Scale(10, dpi), box = N * Z, infoH = ui::Scale(74, dpi);
    int bx = p->cur.x + ui::Scale(26, dpi), by = p->cur.y + ui::Scale(26, dpi);
    if (bx + box > W) bx = p->cur.x - ui::Scale(26, dpi) - box;
    if (by + box + infoH > H) by = p->cur.y - ui::Scale(26, dpi) - box - infoH;
    SetStretchBltMode(mem, COLORONCOLOR);
    StretchBlt(mem, bx, by, box, box, p->dc, p->cur.x - N / 2, p->cur.y - N / 2, N, N, SRCCOPY);
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
    HGDIOBJ op = SelectObject(mem, grid);
    for (int i = 1; i < N; i++) {
        MoveToEx(mem, bx + i * Z, by, nullptr); LineTo(mem, bx + i * Z, by + box);
        MoveToEx(mem, bx, by + i * Z, nullptr); LineTo(mem, bx + box, by + i * Z);
    }
    HPEN white = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(mem, white);
    SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, bx - 1, by - 1, bx + box + 1, by + box + 1);
    HPEN center = CreatePen(PS_SOLID, 2, RGB(255, 40, 40));
    SelectObject(mem, center);
    Rectangle(mem, bx + (N / 2) * Z, by + (N / 2) * Z, bx + (N / 2 + 1) * Z + 1, by + (N / 2 + 1) * Z + 1);
    SelectObject(mem, op);
    DeleteObject(grid); DeleteObject(white); DeleteObject(center);
    // values
    COLORREF c = At(p, p->cur);
    RECT ir{ bx - 1, by + box + 1, bx + box + 1, by + box + infoH };
    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 32)); FillRect(mem, &ir, bg); DeleteObject(bg);
    RECT sw{ ir.left + ui::Scale(8, dpi), ir.top + ui::Scale(8, dpi), ir.left + ui::Scale(34, dpi), ir.top + ui::Scale(34, dpi) };
    ui::FillRoundRect(mem, sw, 6, c, RGB(255, 255, 255));
    HFONT f = ui::UiFont(dpi, 9, true), fs = ui::UiFont(dpi, 8);
    HGDIOBJ of = SelectObject(mem, f);
    SetBkMode(mem, TRANSPARENT); SetTextColor(mem, RGB(255, 255, 255));
    RECT t1{ sw.right + ui::Scale(8, dpi), ir.top + ui::Scale(4, dpi), ir.right - ui::Scale(4, dpi), ir.top + ui::Scale(22, dpi) };
    DrawTextW(mem, color::ToText(c, color::Hex).c_str(), -1, &t1, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(mem, fs);
    RECT t2 = t1; OffsetRect(&t2, 0, ui::Scale(18, dpi));
    DrawTextW(mem, (L"RGB " + color::ToText(c, color::Rgb)).c_str(), -1, &t2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT t3 = t2; OffsetRect(&t3, 0, ui::Scale(18, dpi)); t3.left = ir.left + ui::Scale(8, dpi);
    DrawTextW(mem, (color::ToText(c, color::Hsl) + Format(L"   %d, %d", p->cur.x + p->vs.left, p->cur.y + p->vs.top)).c_str(), -1, &t3,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(mem, of);
    // hint at the top of the monitor under the cursor
    RECT mon = MonitorRectFromPoint({ p->cur.x + p->vs.left, p->cur.y + p->vs.top });
    OffsetRect(&mon, -p->vs.left, -p->vs.top);
    int hw = ui::Scale(640, dpi), hh = ui::Scale(32, dpi);
    RECT hr{ (mon.left + mon.right - hw) / 2, mon.top + ui::Scale(16, dpi), 0, 0 };
    hr.right = hr.left + hw; hr.bottom = hr.top + hh;
    ui::FillRoundRect(mem, hr, 14, RGB(28, 28, 28), RGB(80, 80, 80));
    ui::DrawTextCentered(mem, Tr(L"Conta-gotas: clique para copiar a cor • setas movem 1 px (Shift = 10) • Enter = copiar • Esc = cancelar"),
                         hr, RGB(235, 235, 235), ui::UiFont(dpi, 9));
    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bb); DeleteDC(mem);
}

LRESULT CALLBACK PickerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* p = (Picker*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
        return DefWindowProcW(h, m, w, l);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); Paint(p, dc); EndPaint(h, &ps); return 0; }
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_CROSS)); return TRUE;
    case WM_MOUSEMOVE: p->cur = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: p->cur = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; Finish(p, true); return 0;
    case WM_RBUTTONUP: Finish(p, false); return 0;
    case WM_KEYDOWN:
        switch (w) {
        case VK_ESCAPE: Finish(p, false); return 0;
        case VK_RETURN: case VK_SPACE: Finish(p, true); return 0;
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
            POINT c; GetCursorPos(&c);
            int s = GetKeyState(VK_SHIFT) < 0 ? 10 : 1;
            c.x += w == VK_LEFT ? -s : w == VK_RIGHT ? s : 0;
            c.y += w == VK_UP ? -s : w == VK_DOWN ? s : 0;
            SetCursorPos(c.x, c.y);
            return 0;
        }
        }
        return 0;
    case WM_HOTKEY: if (w == 3) Finish(p, false); return 0;
    case WM_CLOSE: Finish(p, false); return 0;
    case WM_DESTROY:
        UnregisterHotKey(h, 3);
        if (!p->finished) { p->finished = true; if (p->done) p->done(false, 0); }
        DeleteDC(p->dc); DeleteObject(p->bmp);
        delete p;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}

void RunScreenColorPicker(std::function<void(bool, COLORREF)> done) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = PickerProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS); wc.lpszClassName = L"SnapnabColorPicker";
        RegisterClassW(&wc); reg = true;
    }
    auto* p = new Picker();
    p->vs = VirtualScreenRect();
    HDC screen = GetDC(nullptr);
    p->frozen = Image::FromDC(screen, p->vs);
    ReleaseDC(nullptr, screen);
    p->done = std::move(done);
    p->bmp = p->frozen.ToHBITMAP();
    HDC sdc = GetDC(nullptr);
    p->dc = CreateCompatibleDC(sdc); SelectObject(p->dc, p->bmp);
    ReleaseDC(nullptr, sdc);
    POINT c; GetCursorPos(&c);
    p->cur = { c.x - p->vs.left, c.y - p->vs.top };
    p->hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"SnapnabColorPicker", Tr(L"Snapnab — conta-gotas"), WS_POPUP,
                              p->vs.left, p->vs.top, p->vs.right - p->vs.left, p->vs.bottom - p->vs.top, nullptr, nullptr,
                              GetModuleHandleW(nullptr), p);
    RegisterHotKey(p->hwnd, 3, MOD_NOREPEAT, VK_ESCAPE);
    ShowWindow(p->hwnd, SW_SHOW);
    SetForegroundWindow(p->hwnd);
    SetFocus(p->hwnd);
}

} // namespace snap
