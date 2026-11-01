// Image comparison window (C17; ShareX dossier 01 · F15 "Image comparison" in Tools — two images, overlay / difference):
// overlay with a transparency slider, swipe (a divider dragged across), difference (changed pixels in red, with a
// tolerance) and side by side. Exports a before/after animated GIF (two frames) or a combined image with captions.
#include "EditorTools.h"
#include "Effects.h"
#include "Gif.h"
#include "Ui.h"
#include <commctrl.h>
#include <windowsx.h>

namespace snap::ed {

namespace {

enum { IDC_MODE = 10, IDC_SLIDER, IDC_SWAP, IDC_GIF, IDC_PNG, IDC_CLOSE };

struct CompareState {
    Image a, b;
    std::wstring nameA, nameB;
    HWND h = nullptr, mode = nullptr, slider = nullptr, sliderLabel = nullptr, info = nullptr;
    int dpi = 96;
    int view = 0;                    // 0 overlay, 1 swipe, 2 difference, 3 side by side
    int amount = 50;                 // overlay: opacity of B (%); swipe: divider (%); difference: tolerance
    bool dragging = false;
    bool done = false;
    RECT area{};                     // image area (client)
    RECT shown{};                    // where the composed image is drawn
};

int S(const CompareState* st, int v) { return ui::Scale(v, st->dpi); }

std::wstring DialogFilter(std::wstring f) { std::replace(f.begin(), f.end(), L'|', L'\0'); return f; }

Image Fit(const Image& img, int W, int H) {
    Image out(W, H, 0xFFFFFFFF);
    out.Blit(img, 0, 0, false);
    return out;
}

// the composed view at the images' own size (both padded to the larger size)
Image Compose(CompareState* st, int* changed) {
    int W = std::max(st->a.w, st->b.w), H = std::max(st->a.h, st->b.h);
    Image A = Fit(st->a, W, H), B = Fit(st->b, W, H);
    switch (st->view) {
    case 0: {
        Image out = A;
        int k = st->amount * 255 / 100;
        for (size_t i = 0; i < out.px.size(); i++) {
            uint32_t p = out.px[i], q = B.px[i];
            out.px[i] = ARGB(255, CR(p) + (CR(q) - CR(p)) * k / 255, CG(p) + (CG(q) - CG(p)) * k / 255, CB(p) + (CB(q) - CB(p)) * k / 255);
        }
        return out;
    }
    case 1: {
        Image out = A;
        int cut = W * st->amount / 100;
        for (int y = 0; y < H; y++)
            for (int x = cut; x < W; x++) out.at(x, y) = B.at(x, y);
        for (int y = 0; y < H; y++) for (int x = std::max(0, cut - 1); x < std::min(W, cut + 2); x++) out.at(x, y) = 0xFFE5322D;
        return out;
    }
    case 2: return DiffImage(st->a, st->b, st->amount * 255 / 100, changed);
    default: {
        CombineOptions o; o.layout = 0; o.gap = 12; o.align = 0; o.background = RGB(240, 240, 244);
        return CombineImages({ st->a, st->b }, o);
    }
    }
}

void UpdateInfo(CompareState* st) {
    const wchar_t* labels[] = { TR_NOOP(L"Transparência de B"), TR_NOOP(L"Divisória"), TR_NOOP(L"Tolerância"), TR_NOOP(L"") };
    SetWindowTextW(st->sliderLabel, Tr(labels[st->view]));
    ShowWindow(st->slider, st->view == 3 ? SW_HIDE : SW_SHOW);
    ShowWindow(st->sliderLabel, st->view == 3 ? SW_HIDE : SW_SHOW);
    std::wstring t = Format(Tr(L"A: %s (%d × %d)   B: %s (%d × %d)"), st->nameA.c_str(), st->a.w, st->a.h, st->nameB.c_str(), st->b.w, st->b.h);
    if (st->view == 2) {
        int changed = 0;
        DiffImage(st->a, st->b, st->amount * 255 / 100, &changed);
        int total = std::max(st->a.w, st->b.w) * std::max(st->a.h, st->b.h);
        t += Format(Tr(L"   •   %d pixels diferentes (%.2f%%)"), changed, total ? 100.0 * changed / total : 0.0);
    }
    SetWindowTextW(st->info, t.c_str());
}

void Layout(CompareState* st) {
    RECT cr; GetClientRect(st->h, &cr);
    int pad = S(st, 10), rowH = S(st, 28), y = pad;
    MoveWindow(st->mode, pad, y, S(st, 200), S(st, 300), TRUE);
    MoveWindow(st->sliderLabel, pad + S(st, 214), y + S(st, 5), S(st, 140), S(st, 20), TRUE);
    MoveWindow(st->slider, pad + S(st, 358), y, std::max(S(st, 100), (int)cr.right - pad - S(st, 358) - S(st, 380)), rowH, TRUE);
    int bx = cr.right - pad;
    auto btn = [&](int id, int w) { bx -= w; MoveWindow(GetDlgItem(st->h, id), bx, y, w, rowH, TRUE); bx -= S(st, 6); };
    btn(IDC_CLOSE, S(st, 80)); btn(IDC_PNG, S(st, 110)); btn(IDC_GIF, S(st, 110)); btn(IDC_SWAP, S(st, 60));
    st->area = { pad, y + rowH + pad, cr.right - pad, cr.bottom - S(st, 30) };
    MoveWindow(st->info, pad, cr.bottom - S(st, 24), cr.right - 2 * pad, S(st, 20), TRUE);
    InvalidateRect(st->h, nullptr, FALSE);
}

bool ExportGif(CompareState* st) {
    std::wstring path = Tr(L"Antes-depois") + std::wstring(L".gif");
    int fi = 1;
    if (!ui::SaveFileDialog(st->h, DialogFilter(Tr(L"GIF animado (*.gif)|*.gif|")), path, fi, L"gif")) return false;
    int W = std::max(st->a.w, st->b.w), H = std::max(st->a.h, st->b.h);
    GifWriter gw;
    if (!gw.Open(path, W, H, 1, 8, true, false)) return false;
    gw.AddFrame(Fit(st->a, W, H), 100);
    gw.AddFrame(Fit(st->b, W, H), 100);
    return gw.Close() && !gw.failed();
}

bool ExportCombined(CompareState* st) {
    std::wstring path = Tr(L"Antes-depois") + std::wstring(L".png");
    int fi = 1;
    if (!ui::SaveFileDialog(st->h, DialogFilter(L"PNG (*.png)|*.png|JPEG (*.jpg)|*.jpg|WebP (*.webp)|*.webp|"), path, fi, L"png")) return false;
    int fontPx = std::clamp(std::max(st->a.h, st->b.h) / 22, 14, 48);
    Image ca = fx::Caption(st->a, Tr(L"Antes"), true, fontPx, 0xFF202226, 0xFFF0F0F4);
    Image cb = fx::Caption(st->b, Tr(L"Depois"), true, fontPx, 0xFF202226, 0xFFF0F0F4);
    CombineOptions o; o.layout = st->a.w >= st->a.h * 2 ? 1 : 0; o.gap = 16; o.margin = 16; o.align = 0; o.background = RGB(240, 240, 244);
    return SaveImageFile(path, CombineImages({ ca, cb }, o));
}

LRESULT CALLBACK CompareProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = (CompareState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        st = (CompareState*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        st->h = h;
        HFONT f = ui::UiFont(st->dpi);
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
            HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
            return c;
        };
        st->mode = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, IDC_MODE);
        for (auto t : { TR_NOOP(L"Sobreposição"), TR_NOOP(L"Deslizar (antes | depois)"), TR_NOOP(L"Diferença"), TR_NOOP(L"Lado a lado") })
            SendMessageW(st->mode, CB_ADDSTRING, 0, (LPARAM)Tr(t));
        SendMessageW(st->mode, CB_SETCURSEL, st->view, 0);
        st->sliderLabel = mk(L"STATIC", L"", SS_RIGHT, 0);
        st->slider = mk(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, IDC_SLIDER);
        SendMessageW(st->slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(st->slider, TBM_SETPOS, TRUE, st->amount);
        mk(L"BUTTON", Tr(L"A ⇄ B"), BS_PUSHBUTTON | WS_TABSTOP, IDC_SWAP);
        mk(L"BUTTON", Tr(L"Exportar GIF…"), BS_PUSHBUTTON | WS_TABSTOP, IDC_GIF);
        mk(L"BUTTON", Tr(L"Exportar imagem…"), BS_PUSHBUTTON | WS_TABSTOP, IDC_PNG);
        mk(L"BUTTON", Tr(L"Fechar"), BS_PUSHBUTTON | WS_TABSTOP, IDC_CLOSE);
        st->info = mk(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0);
        return 0;
    }
    case WM_SIZE: if (st) Layout(st); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize = { 760, 420 }; return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT cr; GetClientRect(h, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bb = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ ob = SelectObject(mem, bb);
        FillRect(mem, &cr, GetSysColorBrush(COLOR_BTNFACE));
        RECT a = st->area;
        HBRUSH dark = CreateSolidBrush(RGB(222, 224, 228)); FillRect(mem, &a, dark); DeleteObject(dark);
        Image img = Compose(st, nullptr);
        if (!img.empty()) {
            int aw = a.right - a.left, ah = a.bottom - a.top;
            double k = std::min({ 1.0, (double)aw / img.w, (double)ah / img.h });
            int dw = std::max(1, (int)(img.w * k)), dh = std::max(1, (int)(img.h * k));
            int x = a.left + (aw - dw) / 2, y = a.top + (ah - dh) / 2;
            img.DrawTo(mem, x, y, dw, dh, true);
            st->shown = { x, y, x + dw, y + dh };
        }
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bb); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    // swipe: drag the divider on the image
    case WM_LBUTTONDOWN: case WM_MOUSEMOVE: {
        if (st->view != 1) return 0;
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (m == WM_LBUTTONDOWN && PtInRect(&st->shown, p)) { st->dragging = true; SetCapture(h); }
        if (st->dragging && st->shown.right > st->shown.left) {
            st->amount = std::clamp((int)((p.x - st->shown.left) * 100 / (st->shown.right - st->shown.left)), 0, 100);
            SendMessageW(st->slider, TBM_SETPOS, TRUE, st->amount);
            InvalidateRect(h, &st->area, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: if (st->dragging) { st->dragging = false; ReleaseCapture(); } return 0;
    case WM_SETCURSOR:
        if (st && st->view == 1 && LOWORD(l) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
            if (PtInRect(&st->shown, p)) { SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_HSCROLL:
        if ((HWND)l == st->slider) {
            st->amount = (int)SendMessageW(st->slider, TBM_GETPOS, 0, 0);
            if (st->view == 2) UpdateInfo(st);
            InvalidateRect(h, &st->area, FALSE);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_MODE:
            if (HIWORD(w) == CBN_SELCHANGE) {
                st->view = (int)SendMessageW(st->mode, CB_GETCURSEL, 0, 0);
                st->amount = st->view == 2 ? 6 : 50;
                SendMessageW(st->slider, TBM_SETPOS, TRUE, st->amount);
                UpdateInfo(st);
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        case IDC_SWAP: std::swap(st->a, st->b); std::swap(st->nameA, st->nameB); UpdateInfo(st); InvalidateRect(h, nullptr, FALSE); break;
        case IDC_GIF: if (ExportGif(st)) MessageBeep(MB_OK); break;
        case IDC_PNG: if (ExportCombined(st)) MessageBeep(MB_OK); break;
        case IDC_CLOSE: case IDCANCEL: st->done = true; break;
        }
        return 0;
    case WM_CLOSE: st->done = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

void ShowCompare(HWND owner, const Image& a, const Image& b, const std::wstring& nameA, const std::wstring& nameB) {
    static bool reg = false;
    if (!reg) {
        INITCOMMONCONTROLSEX ic{ sizeof ic, ICC_BAR_CLASSES };
        InitCommonControlsEx(&ic);
        WNDCLASSW wc{};
        wc.lpfnWndProc = CompareProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SnapnabCompare";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        RegisterClassW(&wc);
        reg = true;
    }
    CompareState st;
    st.a = a; st.b = b; st.nameA = nameA; st.nameB = nameB;
    st.dpi = owner ? (int)GetDpiForWindow(owner) : 96;
    RECT orc; GetWindowRect(owner ? owner : GetDesktopWindow(), &orc);
    int W = std::max(ui::Scale(900, st.dpi), (int)(orc.right - orc.left) * 3 / 4), H = std::max(ui::Scale(600, st.dpi), (int)(orc.bottom - orc.top) * 3 / 4);
    HWND h = CreateWindowExW(0, L"SnapnabCompare", Tr(L"Comparar imagens"), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             (orc.left + orc.right - W) / 2, (orc.top + orc.bottom - H) / 2, W, H, owner, nullptr, GetModuleHandleW(nullptr), &st);
    if (!h) return;
    UpdateInfo(&st);
    Layout(&st);
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { st.done = true; continue; }
        if (IsDialogMessageW(h, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    DestroyWindow(h);
}

} // namespace snap::ed
