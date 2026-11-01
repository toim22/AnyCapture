#include "QuickOverlay.h"
#include "Clipboard.h"
#include "DragOut.h"
#include "Icons.h"
#include "Library.h"
#include "Ui.h"
#include <dwmapi.h>

namespace snap {

namespace {

enum Act { ActNone = -1, ActCopy, ActSave, ActAnnotate, ActPin, ActDelete, ActClose, ActCount };

struct Card {
    HWND h = nullptr;
    QuickOverlay* owner = nullptr;
    Image img, thumb;
    std::wstring lib, title;
    bool video = false;
    bool hover = false;
    int hot = ActNone;
    int left = 0;                    // seconds until it closes (0 = never)
    POINT down{}; bool pressed = false;
    RECT thumbR{}, btn[ActCount]{};
};

std::vector<Card*> g_cards;

int Dpi(Card* c) { return DpiForWindow(c->h); }

SIZE CardSize(Card* c, int dpi) {
    int pad = ui::Scale(8, dpi), bar = ui::Scale(36, dpi);
    return { c->thumb.w + 2 * pad, c->thumb.h + 2 * pad + bar };
}

// stacks the cards from the corner (newest at the corner)
void Layout() {
    if (g_cards.empty()) return;
    POINT cur; GetCursorPos(&cur);
    RECT wa = MonitorRectFromPoint(cur, true);
    int corner = (int)RegGetDword(L"OverlayCorner", 0);           // 0 bottom right, 1 bottom left, 2 top right, 3 top left
    int dpi = DpiForWindow(g_cards.back()->h), gap = ui::Scale(10, dpi);
    int y = (corner < 2) ? wa.bottom - gap : wa.top + gap;
    for (auto it = g_cards.rbegin(); it != g_cards.rend(); ++it) {
        SIZE s = CardSize(*it, dpi);
        int x = (corner == 0 || corner == 2) ? wa.right - gap - s.cx : wa.left + gap;
        int top = corner < 2 ? y - s.cy : y;
        SetWindowPos((*it)->h, HWND_TOPMOST, x, top, s.cx, s.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        y = corner < 2 ? top - gap : top + s.cy + gap;
    }
}

void CloseCard(Card* c) { if (c && c->h) DestroyWindow(c->h); }

const wchar_t* ActTip(int a) {
    switch (a) {
    case ActCopy: return Tr(L"Copiar");
    case ActSave: return Tr(L"Salvar como…");
    case ActAnnotate: return Tr(L"Anotar no Editor");
    case ActPin: return Tr(L"Fixar na tela");
    case ActDelete: return Tr(L"Excluir da biblioteca");
    case ActClose: return Tr(L"Fechar");
    default: return L"";
    }
}

void Paint(Card* c, HDC hdc) {
    RECT cr; GetClientRect(c->h, &cr);
    int dpi = Dpi(c), pad = ui::Scale(8, dpi);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bb = CreateCompatibleBitmap(hdc, cr.right, cr.bottom);
    HGDIOBJ ob = SelectObject(mem, bb);
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 36)); FillRect(mem, &cr, bg); DeleteObject(bg);
    c->thumbR = { pad, pad, pad + c->thumb.w, pad + c->thumb.h };
    c->thumb.DrawTo(mem, c->thumbR.left, c->thumbR.top, c->thumb.w, c->thumb.h, false);
    Gdiplus::Graphics g(mem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    if (c->video) {                                  // play badge on video thumbnails
        float s = (float)ui::Scale(30, dpi), cx = (c->thumbR.left + c->thumbR.right) / 2.0f, cy = (c->thumbR.top + c->thumbR.bottom) / 2.0f;
        Gdiplus::SolidBrush dark(Gdiplus::Color(170, 0, 0, 0));
        g.FillEllipse(&dark, cx - s / 2, cy - s / 2, s, s);
        Gdiplus::SolidBrush wh(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::PointF tri[3] = { { cx - s / 6, cy - s / 4 }, { cx + s / 4, cy }, { cx - s / 6, cy + s / 4 } };
        g.FillPolygon(&wh, tri, 3);
    }
    // buttons (always visible: the overlay is small and quick)
    int bs = ui::Scale(28, dpi), gap = ui::Scale(4, dpi), y = cr.bottom - bs - ui::Scale(4, dpi);
    int n = ActCount, total = n * bs + (n - 1) * gap, x = (cr.right - total) / 2;
    const icons::Id ico[ActCount] = { icons::Copy, icons::Save, icons::Pen, icons::Pin, icons::Trash, icons::Cancel };
    for (int a = 0; a < n; a++) {
        RECT r{ x, y, x + bs, y + bs };
        c->btn[a] = r;
        bool disabled = c->video && (a == ActPin);
        if (c->hot == a && !disabled) {
            Gdiplus::SolidBrush hb(a == ActDelete ? Gdiplus::Color(255, 196, 43, 28) : Gdiplus::Color(255, 70, 70, 78));
            g.FillEllipse(&hb, (float)r.left, (float)r.top, (float)bs, (float)bs);
        }
        int ip = bs / 4;
        icons::Draw(g, ico[a], Gdiplus::RectF((float)r.left + ip, (float)r.top + ip, (float)bs - 2 * ip, (float)bs - 2 * ip),
                    disabled ? Gdiplus::Color(255, 100, 100, 104) : Gdiplus::Color(255, 235, 235, 240));
        x += bs + gap;
    }
    if (c->hot != ActNone) {                         // the button's name, over the bottom of the picture
        std::wstring t = ActTip(c->hot);
        HFONT f = ui::UiFont(dpi, 9, true);
        HGDIOBJ of = SelectObject(mem, f);
        SIZE ts{}; GetTextExtentPoint32W(mem, t.c_str(), (int)t.size(), &ts);
        SelectObject(mem, of);
        int tw = ts.cx + ui::Scale(18, dpi), th = ui::Scale(22, dpi);
        RECT tr{ (cr.right - tw) / 2, c->thumbR.bottom - th - ui::Scale(4, dpi), 0, 0 };
        tr.right = tr.left + tw; tr.bottom = tr.top + th;
        ui::FillRoundRect(mem, tr, th / 2, RGB(20, 20, 22), RGB(20, 20, 22));
        ui::DrawTextCentered(mem, t, tr, RGB(255, 255, 255), f);
    }
    if (c->left > 0 && !c->hover) {                  // countdown bar along the bottom
        int full = (int)RegGetDword(L"OverlaySeconds", 8);
        float k = full > 0 ? (float)c->left / full : 0;
        Gdiplus::SolidBrush pb(Gdiplus::Color(255, 0, 120, 212));
        g.FillRectangle(&pb, 0.0f, (float)cr.bottom - 3, cr.right * k, 3.0f);
    }
    g.Flush(Gdiplus::FlushIntentionSync);
    BitBlt(hdc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bb); DeleteDC(mem);
}

int HitAct(Card* c, POINT p) {
    for (int a = 0; a < ActCount; a++) if (PtInRect(&c->btn[a], p)) return a;
    return ActNone;
}

std::wstring DragFile(Card* c) {
    if (c->video) return c->lib;
    return TempImageFile(c->img, c->title.empty() ? L"Snapnab_" + TimeStamp() : c->title);
}

void DoAct(Card* c, int a) {
    QuickOverlay* o = c->owner;
    switch (a) {
    case ActCopy:
        if (c->video) clip::CopyFiles(c->h, { c->lib }); else CopyImageToClipboard(c->h, c->img);
        if (o->notify) o->notify(L"Snapnab", Tr(L"Copiado para a área de transferência."));
        break;
    case ActSave: {
        std::wstring path = DefaultSaveDir() + L"\\" + (c->title.empty() ? Tr(L"Captura") : c->title);
        for (size_t i = 3; i < path.size(); i++) if (wcschr(L":*?\"<>|", path[i])) path[i] = L'_';
        int fi = 1;
        if (c->video) {
            std::wstring f = Tr(L"Vídeo MP4 (*.mp4)|*.mp4|");
            std::replace(f.begin(), f.end(), L'|', L'\0');
            if (ui::SaveFileDialog(c->h, f, path, fi, L"mp4", Tr(L"Salvar o vídeo"))) CopyFileW(c->lib.c_str(), path.c_str(), FALSE);
        } else if (ui::SaveFileDialog(c->h, ImageSaveFilter(), path, fi, L"png", Tr(L"Salvar como"))) {
            if (FileExt(path).empty()) path += L".png";
            if (FileExt(path) == L".pdf" || FileExt(path) == L".snap") path = StripExt(path) + L".png";
            if (!SaveImageFile(path, c->img)) MessageBoxW(c->h, (Tr(L"Não foi possível salvar:\n") + path + L"\n\n" + ImageIoError()).c_str(), L"Snapnab", MB_ICONWARNING);
        }
        break;
    }
    case ActAnnotate: if (o->annotate && !c->lib.empty()) o->annotate(c->lib); CloseCard(c); break;
    case ActPin: if (!c->video && o->pin) { o->pin(c->img, c->title); CloseCard(c); } break;
    case ActDelete:
        if (!c->lib.empty() && DeleteLibraryItem(c->lib, c->h)) { if (o->libraryChanged) o->libraryChanged(); CloseCard(c); }
        break;
    case ActClose: CloseCard(c); break;
    }
}

LRESULT CALLBACK CardProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* c = (Card*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!c) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); Paint(c, dc); EndPaint(h, &ps); return 0; }
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, c->hot != ActNone ? IDC_HAND : IDC_ARROW)); return TRUE;
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (!c->hover) { c->hover = true; TRACKMOUSEEVENT t{ sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t); }
        int hot = HitAct(c, p);
        if (hot != c->hot) {
            c->hot = hot;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (c->pressed && PtInRect(&c->thumbR, c->down) &&
            (std::abs(p.x - c->down.x) > GetSystemMetrics(SM_CXDRAG) || std::abs(p.y - c->down.y) > GetSystemMetrics(SM_CYDRAG))) {
            c->pressed = false;
            ReleaseCapture();
            std::wstring f = DragFile(c);
            if (!f.empty()) DragFilesOut(h, { f }, &c->thumb, !c->video);      // the capture goes into another program
        }
        return 0;
    }
    case WM_MOUSELEAVE: c->hover = false; c->hot = ActNone; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: c->down = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; c->pressed = true; SetCapture(h); return 0;
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        bool click = c->pressed;
        c->pressed = false;
        ReleaseCapture();
        if (!click) return 0;
        int a = HitAct(c, p);
        if (a != ActNone) DoAct(c, a);
        else if (PtInRect(&c->thumbR, p)) DoAct(c, ActAnnotate);       // a click on the picture opens it in the Editor
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT s{ GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ClientToScreen(h, &s);
        enum { MCopyAs = 1, MQr, MUpload, MCloseAll };
        HMENU mm = CreatePopupMenu();
        AppendMenuW(mm, MF_STRING, MCopyAs, Tr(L"Copiar como…"));
        if (!c->video) AppendMenuW(mm, MF_STRING, MQr, Tr(L"Ler código QR"));
        if (c->owner->upload) AppendMenuW(mm, MF_STRING, MUpload, Tr(L"Enviar (upload) e copiar o link"));
        AppendMenuW(mm, MF_SEPARATOR, 0, nullptr);
        for (int a = 0; a < ActCount; a++) if (!(c->video && a == ActPin)) AppendMenuW(mm, MF_STRING, 100 + a, ActTip(a));
        AppendMenuW(mm, MF_STRING, MCloseAll, Tr(L"Fechar todas"));
        SetForegroundWindow(h);
        int id = (int)TrackPopupMenu(mm, TPM_RETURNCMD | TPM_RIGHTBUTTON, s.x, s.y, 0, h, nullptr);
        DestroyMenu(mm);
        if (id == MCopyAs) CopyAsMenu(h, s, c->img, c->video ? c->lib : std::wstring(), c->title);
        else if (id == MQr && c->owner->readQr) c->owner->readQr(c->img);
        else if (id == MUpload && c->owner->upload) c->owner->upload(c->lib, c->img);
        else if (id == MCloseAll) c->owner->CloseAll();
        else if (id >= 100 && id < 100 + ActCount) DoAct(c, id - 100);
        return 0;
    }
    case WM_TIMER:
        if (c->hover || c->left <= 0) return 0;     // paused while the mouse is over it
        if (--c->left <= 0) { CloseCard(c); return 0; }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        g_cards.erase(std::remove(g_cards.begin(), g_cards.end(), c), g_cards.end());
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete c;
        Layout();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}

void QuickOverlay::Show(const Image& img, const std::wstring& libPath, bool video, const std::wstring& title) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = CardProc; wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SnapnabQuickOverlay"; wc.style = CS_DROPSHADOW;
        RegisterClassW(&wc); reg = true;
    }
    if (img.empty()) return;
    while (g_cards.size() >= 5) CloseCard(g_cards.front());      // up to five stacked
    auto* c = new Card();
    c->owner = this; c->img = img; c->lib = libPath; c->video = video; c->title = title;
    c->h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"SnapnabQuickOverlay", L"Snapnab", WS_POPUP,
                           0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(c->h, GWLP_USERDATA, (LONG_PTR)c);
    int dpi = DpiForWindow(c->h);
    int maxW = ui::Scale(240, dpi), maxH = ui::Scale(150, dpi);
    c->thumb = MakeThumbnail(img, maxW, maxH);
    if (c->thumb.w < ui::Scale(200, dpi)) {          // narrow captures: the card keeps room for the buttons
        Image wide(ui::Scale(200, dpi), c->thumb.h, 0xFF202024);
        wide.Blit(c->thumb, (wide.w - c->thumb.w) / 2, 0, true);
        c->thumb = wide;
    } else {
        Image flat(c->thumb.w, c->thumb.h, 0xFF202024);
        flat.Blit(c->thumb, 0, 0, true);
        c->thumb = flat;
    }
    DWORD corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(c->h, 33, &corner, sizeof corner);
    c->left = (int)RegGetDword(L"OverlaySeconds", 8);
    if (c->left > 0) SetTimer(c->h, 1, 1000, nullptr);
    g_cards.push_back(c);
    Layout();
}

void QuickOverlay::CloseAll() {
    auto copy = g_cards;
    for (auto* c : copy) CloseCard(c);
}

} // namespace snap
