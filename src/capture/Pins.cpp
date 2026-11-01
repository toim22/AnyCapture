#include "Pins.h"
#include "Clipboard.h"
#include "Color.h"
#include "DragOut.h"
#include "Icons.h"
#include "Library.h"
#include "Ui.h"
#include "../editor/Document.h"
#include <cmath>

namespace snap {

namespace {

enum Tool { ToolNone, ToolPen, ToolHighlighter, ToolArrow, ToolRect, ToolText };
enum Btn { BtnNone = -1, BtnClose = 100, BtnAnnotate, BtnPen = 200, BtnHighlighter, BtnArrow, BtnRect, BtnText, BtnColor, BtnUndo, BtnDone };

std::wstring PinsDir() { std::wstring d = AppDataDir() + L"\\Pins"; EnsureDir(d); return d; }

struct PinWnd {
    HWND h = nullptr;
    PinManager* mgr = nullptr;
    Image base;
    std::vector<ed::ObjPtr> objs;
    std::wstring title;
    int scale = 100, opacity = 100;
    bool locked = false, clickThrough = false, topmost = true, shadow = true;
    Tool tool = ToolNone;
    uint32_t color = 0xFFE5322D;
    bool hover = false;
    int drag = 0;                    // 0 none, 1 move, 2 drawing, 3 Ctrl pressed (drag-out when it moves)
    POINT down{}, winAtDown{};
    ed::ObjPtr creating;
    Image composed, scaled;          // base + objects, and at the current scale (cached)
    int scaledFor = -1;
    std::vector<std::pair<RECT, int>> buttons;     // hit areas of the last paint (client coordinates)
    POINT imgOrigin{};               // where the image starts in the window (client)
};

std::vector<PinWnd*> g_pins;
int Dpi(PinWnd* p) { return DpiForWindow(p->h); }
int Margin(PinWnd* p) { return p->shadow ? ui::Scale(14, Dpi(p)) : 1; }
int BarH(PinWnd* p) { return p->tool != ToolNone ? ui::Scale(34, Dpi(p)) : 0; }

void Compose(PinWnd* p) {
    p->composed = p->base;
    if (!p->objs.empty() || p->creating) {
        auto bmp = p->composed.WrapGdip();
        Gdiplus::Graphics g(bmp.get());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        for (auto& o : p->objs) if (!o->hidden && o->type != ed::ObjType::Link) ed::RenderObject(g, p->composed, *o);
        if (p->creating) ed::RenderObject(g, p->composed, *p->creating);
        g.Flush(Gdiplus::FlushIntentionSync);
    }
    p->scaledFor = -1;
}

SIZE ImgSize(PinWnd* p) { return { std::max(1, p->base.w * p->scale / 100), std::max(1, p->base.h * p->scale / 100) }; }

// the window as a layered bitmap: shadow, image, border and the buttons (premultiplied alpha)
void Render(PinWnd* p) {
    if (p->scaledFor != p->scale) {
        SIZE s = ImgSize(p);
        p->scaled = p->scale == 100 ? p->composed : ScaleImage(p->composed, s.cx, s.cy, true);
        p->scaledFor = p->scale;
    }
    const int dpi = Dpi(p), m = Margin(p), bar = BarH(p);
    const int iw = p->scaled.w, ih = p->scaled.h;
    const int W = std::max(iw + 2 * m, bar ? ui::Scale(300, dpi) + 2 * m : 0), H = ih + 2 * m + bar;
    Image s(W, H, 0);
    p->imgOrigin = { m, m + bar };
    if (p->shadow) {                                  // soft shadow: alpha falls off with the distance to the image
        RECT ir{ m, m + bar, m + iw, m + bar + ih };
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int dx = std::max({ (int)ir.left - x, 0, x - (int)ir.right + 1 }), dy = std::max({ (int)ir.top - y, 0, y - (int)ir.bottom + 1 - ui::Scale(3, dpi) });
                if (!dx && !dy) continue;
                double d = std::sqrt((double)dx * dx + (double)dy * dy) / m;
                if (d < 1) s.at(x, y) = ARGB((int)(70 * (1 - d) * (1 - d)), 0, 0, 0);
            }
    }
    s.Blit(p->scaled, m, m + bar, false);
    for (int x = m - 1; x <= m + iw; x++) {           // thin border so the pin stands out from the window below
        if (x < 0 || x >= W) continue;
        if (m + bar - 1 >= 0) s.at(x, m + bar - 1) = 0xC8808080;
        if (m + bar + ih < H) s.at(x, m + bar + ih) = 0xC8808080;
    }
    for (int y = m + bar - 1; y <= m + bar + ih; y++) {
        if (y < 0 || y >= H) continue;
        if (m - 1 >= 0) s.at(m - 1, y) = 0xC8808080;
        if (m + iw < W) s.at(m + iw, y) = 0xC8808080;
    }
    p->buttons.clear();
    {
        auto bmp = s.WrapGdip();
        Gdiplus::Graphics g(bmp.get());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        auto button = [&](RECT r, int id, icons::Id icon, bool on, uint32_t bg) {
            Gdiplus::SolidBrush b(Gdiplus::Color((BYTE)(bg >> 24), (BYTE)(bg >> 16), (BYTE)(bg >> 8), (BYTE)bg));
            g.FillEllipse(&b, (float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top));
            if (on) { Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255), 2); g.DrawEllipse(&pen, (float)r.left + 1, (float)r.top + 1, (float)(r.right - r.left - 2), (float)(r.bottom - r.top - 2)); }
            int pad = (r.right - r.left) / 4;
            icons::Draw(g, icon, Gdiplus::RectF((float)r.left + pad, (float)r.top + pad, (float)(r.right - r.left - 2 * pad), (float)(r.bottom - r.top - 2 * pad)),
                        Gdiplus::Color(255, 255, 255, 255));
            p->buttons.push_back({ r, id });
        };
        int bs = ui::Scale(26, dpi), gap = ui::Scale(4, dpi);
        if (p->tool != ToolNone) {                    // annotation toolbar above the image
            struct B { int id; icons::Id icon; Tool t; };
            const B bb[] = { { BtnPen, icons::Pen, ToolPen }, { BtnHighlighter, icons::Highlighter, ToolHighlighter }, { BtnArrow, icons::Arrow, ToolArrow },
                             { BtnRect, icons::Rect, ToolRect }, { BtnText, icons::Text, ToolText } };
            int x = m, y = m + (bar - bs) / 2;
            for (auto& b : bb) { button({ x, y, x + bs, y + bs }, b.id, b.icon, p->tool == b.t, 0xE0303036); x += bs + gap; }
            RECT cr{ x, y, x + bs, y + bs };
            Gdiplus::SolidBrush cb(ed::Gc(p->color));
            g.FillEllipse(&cb, (float)cr.left + 3, (float)cr.top + 3, (float)bs - 6, (float)bs - 6);
            p->buttons.push_back({ cr, BtnColor });
            x += bs + gap;
            button({ x, y, x + bs, y + bs }, BtnUndo, icons::Undo, false, 0xE0303036); x += bs + gap;
            button({ x, y, x + bs, y + bs }, BtnDone, icons::Check, false, 0xE0207A3C);
        } else if (p->hover && !p->clickThrough) {    // close and annotate at the top right of the image
            int x = m + iw - bs - gap, y = m + bar + gap;
            if (iw > 3 * bs) {
                button({ x, y, x + bs, y + bs }, BtnClose, icons::Cancel, false, 0xD0202024);
                button({ x - bs - gap, y, x - gap, y + bs }, BtnAnnotate, icons::Pen, false, 0xD0202024);
            }
        }
        g.Flush(Gdiplus::FlushIntentionSync);
    }
    for (auto& px : s.px) { int a = CA(px); px = ARGB(a, CR(px) * a / 255, CG(px) * a / 255, CB(px) * a / 255); }
    // position: the image stays where it was when the bar/shadow change the window size
    RECT wr; GetWindowRect(p->h, &wr);
    POINT dst{ wr.left, wr.top };
    SIZE sz{ W, H };
    HDC sdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(sdc);
    HBITMAP hb = s.ToHBITMAP();
    HGDIOBJ o = SelectObject(mem, hb);
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, (BYTE)(p->opacity * 255 / 100), AC_SRC_ALPHA };
    UpdateLayeredWindow(p->h, sdc, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, o); DeleteObject(hb); DeleteDC(mem);
    ReleaseDC(nullptr, sdc);
}

void Refresh(PinWnd* p, bool recompose = false) { if (recompose) Compose(p); Render(p); }

POINT ToImage(PinWnd* p, POINT client) {
    return { (client.x - p->imgOrigin.x) * 100 / p->scale, (client.y - p->imgOrigin.y) * 100 / p->scale };
}

int HitButton(PinWnd* p, POINT c) {
    for (auto& [r, id] : p->buttons) if (PtInRect(&r, c)) return id;
    return BtnNone;
}

// moves the window so that the image's top-left corner stays put when the margins change
void KeepImage(PinWnd* p, int oldMargin, int oldBar) {
    RECT wr; GetWindowRect(p->h, &wr);
    int dx = Margin(p) - oldMargin, dy = (Margin(p) + BarH(p)) - (oldMargin + oldBar);
    SetWindowPos(p->h, nullptr, wr.left - dx, wr.top - dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SetTool(PinWnd* p, Tool t) {
    int m = Margin(p), b = BarH(p);
    p->tool = t;
    KeepImage(p, m, b);
    Refresh(p);
}

void SaveHistory(PinWnd* p) {
    std::wstring dir = PinsDir();
    SaveImageFile(dir + L"\\pin_" + TimeStamp() + Format(L"_%u.png", GetTickCount() % 1000), p->composed);
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\pin_*.png").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) { do files.push_back(fd.cFileName); while (FindNextFileW(f, &fd)); FindClose(f); }
    std::sort(files.begin(), files.end());
    for (size_t i = 0; i + 10 < files.size(); i++) DeleteFileW((dir + L"\\" + files[i]).c_str());
}

void ClosePin(PinWnd* p) {
    if (!p->h) return;
    SaveHistory(p);
    DestroyWindow(p->h);
}

void CopyPin(PinWnd* p) { CopyImageToClipboard(p->h, p->composed); }

void SavePin(PinWnd* p) {
    std::wstring path = DefaultSaveDir() + L"\\" + (p->title.empty() ? Tr(L"Fixada") : p->title);
    for (auto& c : path) if (c == L':' && &c != &path[1]) c = L'_';
    int fi = 1;
    if (!ui::SaveFileDialog(p->h, ImageSaveFilter(), path, fi, L"png", Tr(L"Salvar a imagem fixada"))) return;
    if (FileExt(path).empty()) path += L".png";
    if (!SaveImageFile(path, p->composed)) MessageBoxW(p->h, (Tr(L"Não foi possível salvar:\n") + path).c_str(), L"Snapnab", MB_ICONWARNING);
}

void OpenInEditor(PinWnd* p) {
    auto d = ed::Document::FromImage(p->base, p->title);
    d->st.objs = p->objs;
    d->captureMode = Tr(L"Imagem fixada");
    d->path = LibraryDir() + L"\\" + NewGuidString() + L".snap";
    if (!d->Save()) return;
    WriteThumbnail(d->path, p->composed);
    if (p->mgr->openInEditor) p->mgr->openInEditor(d->path);
}

void SetScale(PinWnd* p, int s, POINT anchorScreen) {
    s = std::clamp(s, 20, 500);
    if (s == p->scale) return;
    RECT wr; GetWindowRect(p->h, &wr);
    // the point under the cursor stays where it is
    double ix = (anchorScreen.x - wr.left - p->imgOrigin.x) * 100.0 / p->scale, iy = (anchorScreen.y - wr.top - p->imgOrigin.y) * 100.0 / p->scale;
    p->scale = s;
    int nx = (int)std::lround(anchorScreen.x - p->imgOrigin.x - ix * s / 100.0), ny = (int)std::lround(anchorScreen.y - p->imgOrigin.y - iy * s / 100.0);
    SetWindowPos(p->h, nullptr, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    Render(p);
}

void ApplyStyles(PinWnd* p) {
    LONG ex = GetWindowLongW(p->h, GWL_EXSTYLE);
    ex = p->clickThrough ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    SetWindowLongW(p->h, GWL_EXSTYLE, ex);
    SetWindowPos(p->h, p->topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void PickColorAt(PinWnd* p, POINT client) {
    POINT ip = ToImage(p, client);
    if (ip.x < 0 || ip.y < 0 || ip.x >= p->composed.w || ip.y >= p->composed.h) return;
    COLORREF c = ToColorref(p->composed.at(ip.x, ip.y));
    std::wstring t = color::ToText(c, color::DefaultFmt());
    color::CopyText(p->h, t);
    color::AddRecent(c);
    if (p->mgr->notify) p->mgr->notify(L"Snapnab", Tr(L"Cor copiada: ") + t);
}

void ColorMenu(PinWnd* p, POINT screen) {
    const uint32_t cols[] = { 0xFFE5322D, 0xFFFFC107, 0xFF2EAD4B, 0xFF1E88E5, 0xFF8E24AA, 0xFF000000, 0xFFFFFFFF };
    const wchar_t* names[] = { TR_NOOP(L"Vermelho"), TR_NOOP(L"Amarelo"), TR_NOOP(L"Verde"), TR_NOOP(L"Azul"), TR_NOOP(L"Roxo"), TR_NOOP(L"Preto"), TR_NOOP(L"Branco") };
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < 7; i++) AppendMenuW(m, MF_STRING | (p->color == cols[i] ? MF_CHECKED : 0), 1 + i, Tr(names[i]));
    int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, p->h, nullptr);
    DestroyMenu(m);
    if (id >= 1 && id <= 7) { p->color = cols[id - 1]; Render(p); }
}

void ContextMenu(PinWnd* p, POINT screen) {
    enum { CmCopy = 1, CmSave, CmEditor, CmAnnotate, CmUndo, CmZoom50, CmZoom100, CmZoom200, CmOp100, CmOp75, CmOp50, CmOp25,
           CmLock, CmClickThrough, CmTop, CmShadow, CmQr, CmColor, CmClose, CmCloseAll, CmDragOut };
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CmCopy, Tr(L"Copiar\tCtrl+C"));
    AppendMenuW(m, MF_STRING, CmSave, Tr(L"Salvar como…\tCtrl+S"));
    AppendMenuW(m, MF_STRING, CmEditor, Tr(L"Abrir no Editor"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (p->tool != ToolNone ? MF_CHECKED : 0), CmAnnotate, Tr(L"Anotar (caneta, marcador, seta, retângulo, texto)"));
    AppendMenuW(m, MF_STRING | (p->objs.empty() ? MF_GRAYED : 0), CmUndo, Tr(L"Desfazer a anotação\tCtrl+Z"));
    HMENU z = CreatePopupMenu();
    AppendMenuW(z, MF_STRING | (p->scale == 50 ? MF_CHECKED : 0), CmZoom50, L"50%");
    AppendMenuW(z, MF_STRING | (p->scale == 100 ? MF_CHECKED : 0), CmZoom100, L"100%");
    AppendMenuW(z, MF_STRING | (p->scale == 200 ? MF_CHECKED : 0), CmZoom200, L"200%");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)z, (Tr(L"Zoom") + Format(L"  (%d%%)", p->scale)).c_str());
    HMENU op = CreatePopupMenu();
    int ops[] = { 100, 75, 50, 25 };
    for (int i = 0; i < 4; i++) AppendMenuW(op, MF_STRING | (p->opacity == ops[i] ? MF_CHECKED : 0), CmOp100 + i, Format(L"%d%%", ops[i]).c_str());
    AppendMenuW(m, MF_POPUP, (UINT_PTR)op, (Tr(L"Opacidade") + Format(L"  (%d%%)", p->opacity)).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (p->locked ? MF_CHECKED : 0), CmLock, Tr(L"Travar a posição"));
    AppendMenuW(m, MF_STRING | (p->clickThrough ? MF_CHECKED : 0), CmClickThrough, Tr(L"Atravessar cliques (desligue pelo menu da bandeja)"));
    AppendMenuW(m, MF_STRING | (p->topmost ? MF_CHECKED : 0), CmTop, Tr(L"Sempre visível"));
    AppendMenuW(m, MF_STRING | (p->shadow ? MF_CHECKED : 0), CmShadow, Tr(L"Sombra"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CmQr, Tr(L"Ler código QR"));
    AppendMenuW(m, MF_STRING, CmColor, Tr(L"Copiar a cor neste ponto\tAlt+clique"));
    AppendMenuW(m, MF_STRING, CmDragOut, Tr(L"Arrastar para outro programa: Ctrl+arrastar"));
    EnableMenuItem(m, CmDragOut, MF_GRAYED);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CmClose, Tr(L"Fechar\tEsc"));
    AppendMenuW(m, MF_STRING, CmCloseAll, Tr(L"Fechar todas as imagens fixadas"));
    SetForegroundWindow(p->h);
    int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, p->h, nullptr);
    DestroyMenu(m);
    POINT cl = screen; ScreenToClient(p->h, &cl);
    switch (id) {
    case CmCopy: CopyPin(p); break;
    case CmSave: SavePin(p); break;
    case CmEditor: OpenInEditor(p); break;
    case CmAnnotate: SetTool(p, p->tool != ToolNone ? ToolNone : ToolPen); break;
    case CmUndo: if (!p->objs.empty()) { p->objs.pop_back(); Refresh(p, true); } break;
    case CmZoom50: SetScale(p, 50, screen); break;
    case CmZoom100: SetScale(p, 100, screen); break;
    case CmZoom200: SetScale(p, 200, screen); break;
    case CmOp100: case CmOp75: case CmOp50: case CmOp25: p->opacity = ops[id - CmOp100]; Render(p); break;
    case CmLock: p->locked = !p->locked; break;
    case CmClickThrough: p->clickThrough = !p->clickThrough; p->hover = false; ApplyStyles(p); Render(p); break;
    case CmTop: p->topmost = !p->topmost; ApplyStyles(p); break;
    case CmShadow: { int m0 = Margin(p), b0 = BarH(p); p->shadow = !p->shadow; KeepImage(p, m0, b0); Render(p); break; }
    case CmQr: if (p->mgr->readQr) p->mgr->readQr(p->composed); break;
    case CmColor: PickColorAt(p, cl); break;
    case CmClose: ClosePin(p); break;
    case CmCloseAll: p->mgr->CloseAll(); break;
    }
}

void AddText(PinWnd* p, POINT ip) {
    std::vector<ui::Field> f = { ui::Field::MakeText(Tr(L"Texto"), L"") };
    if (!ui::RunForm(p->h, Tr(L"Texto na imagem fixada"), f) || Trim(f[0].text).empty()) return;
    auto o = std::make_shared<ed::DrawObj>();
    o->type = ed::ObjType::Text;
    o->text = f[0].text;
    o->style.textColor = p->color;
    o->style.stroke = (p->color & 0xFFFFFF) == 0xFFFFFF ? 0xFF000000 : 0xFFFFFFFF;    // outline in the opposite tone
    o->style.width = 3; o->style.fontSize = 24; o->style.bold = true;
    o->style.alignH = 0; o->style.alignV = 0; o->style.shadow = false; o->style.padding = 2;
    float w = std::max(80.0f, (float)p->base.w - ip.x);
    o->p1 = Gdiplus::PointF((float)ip.x, (float)ip.y);
    o->p2 = Gdiplus::PointF(ip.x + w, ip.y + ed::TextBlockHeight(*o, w) + 4);
    p->objs.push_back(o);
    Refresh(p, true);
}

ed::ObjPtr NewObj(PinWnd* p, POINT ip) {
    auto o = std::make_shared<ed::DrawObj>();
    Gdiplus::PointF pt((float)ip.x, (float)ip.y);
    o->p1 = o->p2 = pt;
    o->style.stroke = p->color;
    o->style.shadow = false;
    switch (p->tool) {
    case ToolPen: o->type = ed::ObjType::Pen; o->style.width = 4; o->pts = { pt }; break;
    case ToolHighlighter: o->type = ed::ObjType::Highlighter; o->style.stroke = p->color == 0xFFE5322D ? 0xFFFFEB3B : p->color; o->style.opacity = 50; break;
    case ToolArrow: o->type = ed::ObjType::Arrow; o->style.width = 5; o->style.arrowEnd = ed::CapArrow; o->style.shadow = true; break;
    case ToolRect: o->type = ed::ObjType::Shape; o->style.shapeKind = ed::ShapeRect; o->style.width = 3; break;
    default: return nullptr;
    }
    o->ctrl = pt;
    return o;
}

LRESULT CALLBACK PinProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* p = (PinWnd*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!p) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    case WM_SETCURSOR: {
        POINT c; GetCursorPos(&c); ScreenToClient(h, &c);
        LPCWSTR cur = IDC_SIZEALL;
        if (HitButton(p, c) != BtnNone) cur = IDC_HAND;
        else if (GetKeyState(VK_MENU) < 0) cur = IDC_CROSS;
        else if (p->tool != ToolNone) cur = p->tool == ToolText ? IDC_IBEAM : IDC_CROSS;
        else if (p->locked) cur = IDC_ARROW;
        SetCursor(LoadCursor(nullptr, cur));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        POINT c{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (!p->hover) {
            p->hover = true;
            TRACKMOUSEEVENT t{ sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
            Render(p);
        }
        if (p->drag == 1) {
            POINT sc; GetCursorPos(&sc);
            SetWindowPos(h, nullptr, p->winAtDown.x + sc.x - p->down.x, p->winAtDown.y + sc.y - p->down.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (p->drag == 2 && p->creating) {
            POINT ip = ToImage(p, c);
            Gdiplus::PointF pt((float)ip.x, (float)ip.y);
            if (p->creating->type == ed::ObjType::Pen) p->creating->pts.push_back(pt);
            else {
                p->creating->p2 = pt;
                p->creating->ctrl = Gdiplus::PointF((p->creating->p1.X + pt.X) / 2, (p->creating->p1.Y + pt.Y) / 2);
            }
            Refresh(p, true);
        } else if (p->drag == 3) {
            POINT sc; GetCursorPos(&sc);
            if (std::abs(sc.x - p->down.x) + std::abs(sc.y - p->down.y) > GetSystemMetrics(SM_CXDRAG)) {
                p->drag = 0;
                ReleaseCapture();
                std::wstring f = TempImageFile(p->composed, p->title.empty() ? L"Snapnab" : p->title);
                if (!f.empty()) DragFilesOut(h, { f }, &p->composed, true);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE: if (p->hover) { p->hover = false; Render(p); } return 0;
    case WM_LBUTTONDOWN: {
        POINT c{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        SetFocus(h);
        int b = HitButton(p, c);
        if (b != BtnNone) {
            switch (b) {
            case BtnClose: ClosePin(p); return 0;
            case BtnAnnotate: SetTool(p, ToolPen); return 0;
            case BtnPen: SetTool(p, ToolPen); return 0;
            case BtnHighlighter: SetTool(p, ToolHighlighter); return 0;
            case BtnArrow: SetTool(p, ToolArrow); return 0;
            case BtnRect: SetTool(p, ToolRect); return 0;
            case BtnText: SetTool(p, ToolText); return 0;
            case BtnColor: { POINT s = c; ClientToScreen(h, &s); ColorMenu(p, s); return 0; }
            case BtnUndo: if (!p->objs.empty()) { p->objs.pop_back(); Refresh(p, true); } return 0;
            case BtnDone: SetTool(p, ToolNone); return 0;
            }
        }
        if (GetKeyState(VK_MENU) < 0) { PickColorAt(p, c); return 0; }       // Alt+click: color (C10)
        POINT ip = ToImage(p, c);
        bool onImage = ip.x >= 0 && ip.y >= 0 && ip.x < p->base.w && ip.y < p->base.h;
        if (p->tool == ToolText && onImage) { AddText(p, ip); return 0; }
        if (p->tool != ToolNone && onImage) {
            p->creating = NewObj(p, ip);
            if (p->creating) { p->drag = 2; SetCapture(h); }
            return 0;
        }
        GetCursorPos(&p->down);
        RECT wr; GetWindowRect(h, &wr);
        p->winAtDown = { wr.left, wr.top };
        if (w & MK_CONTROL) { p->drag = 3; SetCapture(h); return 0; }       // Ctrl+drag: the image goes out as a file
        if (!p->locked) { p->drag = 1; SetCapture(h); }
        return 0;
    }
    case WM_LBUTTONUP:
        if (p->drag == 2 && p->creating) {
            auto o = p->creating;
            p->creating.reset();
            Gdiplus::RectF r = o->Rect();
            bool tiny = o->type == ed::ObjType::Pen ? o->pts.size() < 2 : (o->IsLine() ? std::hypot(o->p2.X - o->p1.X, o->p2.Y - o->p1.Y) < 3 : r.Width < 3 || r.Height < 3);
            if (!tiny) p->objs.push_back(o);
            Refresh(p, true);
        }
        if (p->drag) { p->drag = 0; ReleaseCapture(); }
        return 0;
    case WM_RBUTTONUP: { POINT s{ GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ClientToScreen(h, &s); ContextMenu(p, s); return 0; }
    case WM_MBUTTONUP: { RECT wr; GetWindowRect(h, &wr); POINT a{ wr.left + p->imgOrigin.x, wr.top + p->imgOrigin.y }; p->opacity = 100; SetScale(p, 100, a); Render(p); return 0; }
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1;
        POINT s{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) { p->opacity = std::clamp(p->opacity + d * 10, 10, 100); Render(p); }
        else if (!(GET_KEYSTATE_WPARAM(w) & (MK_SHIFT))) SetScale(p, p->scale + d * (p->scale >= 200 ? 25 : 10), s);
        return 0;
    }
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        switch (w) {
        case VK_ESCAPE: if (p->tool != ToolNone) SetTool(p, ToolNone); else ClosePin(p); return 0;
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
            if (p->locked) return 0;
            int s = GetKeyState(VK_SHIFT) < 0 ? 10 : 1;
            RECT wr; GetWindowRect(h, &wr);
            SetWindowPos(h, nullptr, wr.left + (w == VK_LEFT ? -s : w == VK_RIGHT ? s : 0), wr.top + (w == VK_UP ? -s : w == VK_DOWN ? s : 0), 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case VK_ADD: case VK_OEM_PLUS: case VK_SUBTRACT: case VK_OEM_MINUS: {
            RECT wr; GetWindowRect(h, &wr);
            POINT a{ wr.left + p->imgOrigin.x, wr.top + p->imgOrigin.y };
            SetScale(p, p->scale + ((w == VK_ADD || w == VK_OEM_PLUS) ? 10 : -10), a);
            return 0;
        }
        }
        if (ctrl && w == 'C') { CopyPin(p); return 0; }
        if (ctrl && w == 'S') { SavePin(p); return 0; }
        if (ctrl && w == 'Z') { if (!p->objs.empty()) { p->objs.pop_back(); Refresh(p, true); } return 0; }
        return 0;
    }
    case WM_DESTROY:
        g_pins.erase(std::remove(g_pins.begin(), g_pins.end(), p), g_pins.end());
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete p;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void CreatePin(PinManager* mgr, Image img, std::vector<ed::ObjPtr> objs, const std::wstring& title, const POINT* at) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = PinProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_SIZEALL); wc.lpszClassName = L"SnapnabPin";
        RegisterClassW(&wc); reg = true;
    }
    if (img.empty()) return;
    auto* p = new PinWnd();
    p->mgr = mgr; p->base = std::move(img); p->objs = std::move(objs); p->title = title;
    p->shadow = RegGetDword(L"PinShadow", 1) != 0;
    p->topmost = true;
    POINT c; GetCursorPos(&c);
    RECT wa = MonitorRectFromPoint(at ? *at : c, true);
    // large images start reduced to fit 80 % of the work area
    int fit = (int)std::min({ 100.0, (wa.right - wa.left) * 80.0 / p->base.w, (wa.bottom - wa.top) * 80.0 / p->base.h });
    p->scale = std::max(20, fit);
    Compose(p);
    p->h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"SnapnabPin", title.empty() ? Tr(L"Imagem fixada") : title.c_str(),
                           WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(p->h, GWLP_USERDATA, (LONG_PTR)p);
    SIZE s = ImgSize(p);
    int x, y;
    if (at) { x = at->x - Margin(p); y = at->y - Margin(p); }
    else { x = (wa.left + wa.right - s.cx) / 2 - Margin(p); y = (wa.top + wa.bottom - s.cy) / 2 - Margin(p); }
    SetWindowPos(p->h, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    g_pins.push_back(p);
    Render(p);
    ShowWindow(p->h, mgr->hidden() ? SW_HIDE : SW_SHOW);
    SetForegroundWindow(p->h);
    Log(L"Pin: %dx%d '%s'", p->base.w, p->base.h, title.c_str());
}

} // namespace

void PinManager::Pin(const Image& img, const std::wstring& title, const POINT* at) {
    if (hidden_) ShowAll(true);
    CreatePin(this, img, {}, title, at);
}

bool PinManager::PinFile(const std::wstring& path) {
    if (!FileExists(path)) return false;
    if (FileExt(path) == L".snap") {
        auto d = ed::Document::Load(path);
        if (!d || !d->st.base) return false;
        if (hidden_) ShowAll(true);
        std::wstring name = CaptureName(path);
        CreatePin(this, *d->st.base, d->st.objs, name.empty() ? d->title : name, nullptr);
        return true;
    }
    Image img;
    if (!LoadImageFile(path, img)) return false;
    Pin(img, StripExt(FileNameOf(path)));
    return true;
}

Image PinManager::ColorCard(COLORREF c) {
    const int W = 220, H = 120, sw = 90;
    Image img(W, H, 0xFFFFFFFF);
    img.FillRect({ 0, 0, W, sw }, FromColorref(c));
    auto bmp = img.WrapGdip();
    Gdiplus::Graphics g(bmp.get());
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::Font f(L"Segoe UI", 12, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::SolidBrush b(Gdiplus::Color(255, 30, 30, 34));
    Gdiplus::StringFormat sf; sf.SetAlignment(Gdiplus::StringAlignmentCenter); sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    std::wstring t = color::ToText(c, color::Hex) + L"   " + color::ToText(c, color::Rgb);
    g.DrawString(t.c_str(), -1, &f, Gdiplus::RectF(0, (float)sw, (float)W, (float)(H - sw)), &sf, &b);
    g.Flush(Gdiplus::FlushIntentionSync);
    return img;
}

Image PinManager::TextCard(const std::wstring& textIn) {
    std::wstring text = textIn.size() > 4000 ? textIn.substr(0, 4000) + L"…" : textIn;
    Gdiplus::Font f(L"Segoe UI", 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::StringFormat sf(Gdiplus::StringFormat::GenericTypographic());
    sf.SetFormatFlags(sf.GetFormatFlags() & ~Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::RectF box;
    {
        Image probe(4, 4);
        auto bmp = probe.WrapGdip();
        Gdiplus::Graphics g(bmp.get());
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.MeasureString(text.c_str(), -1, &f, Gdiplus::SizeF(520, 4000), &sf, &box);
    }
    const int pad = 14;
    int W = std::max(80, (int)std::ceil(box.Width) + 2 * pad), H = std::max(40, (int)std::ceil(box.Height) + 2 * pad);
    Image img(W, H, 0xFFFFFDF2);
    auto bmp = img.WrapGdip();
    Gdiplus::Graphics g(bmp.get());
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::SolidBrush b(Gdiplus::Color(255, 32, 32, 36));
    g.DrawString(text.c_str(), -1, &f, Gdiplus::RectF((float)pad, (float)pad, (float)(W - 2 * pad), (float)(H - 2 * pad)), &sf, &b);
    g.Flush(Gdiplus::FlushIntentionSync);
    return img;
}

// #RRGGBB, RRGGBB, rgb(r, g, b) or "r, g, b"
static bool ParseColor(std::wstring s, COLORREF& c) {
    s = Trim(s);
    if (s.size() > 40) return false;
    int r, g, b;
    std::wstring low = ToLower(s);
    if (swscanf_s(low.c_str(), L"rgb(%d,%d,%d)", &r, &g, &b) == 3 || swscanf_s(low.c_str(), L"rgb(%d, %d, %d)", &r, &g, &b) == 3 ||
        swscanf_s(low.c_str(), L"%d, %d, %d", &r, &g, &b) == 3) {
        if (r < 0 || g < 0 || b < 0 || r > 255 || g > 255 || b > 255) return false;
        c = RGB(r, g, b); return true;
    }
    if (!s.empty() && s[0] == L'#') s = s.substr(1);
    if (s.size() != 6 || s.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) return false;
    c = HexToColor(L"#" + s, 0);
    return true;
}

bool PinManager::PinClipboard() {
    Image img;
    if (ClipboardHasImage() && GetClipboardImage(img)) { Pin(img, Tr(L"Área de transferência")); return true; }
    std::wstring text;
    if (OpenClipboard(nullptr)) {
        if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* t = (const wchar_t*)GlobalLock(hd)) { text = t; GlobalUnlock(hd); }
        }
        CloseClipboard();
    }
    if (Trim(text).empty()) {
        if (notify) notify(L"Snapnab", Tr(L"A área de transferência não tem imagem nem texto para fixar."));
        return false;
    }
    COLORREF c;
    if (ParseColor(text, c)) Pin(ColorCard(c), color::ToText(c, color::Hex));
    else Pin(TextCard(text), Tr(L"Texto"));
    return true;
}

void PinManager::ShowAll(bool show) {
    hidden_ = !show;
    for (auto* p : g_pins) ShowWindow(p->h, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void PinManager::CloseAll() {
    auto copy = g_pins;
    for (auto* p : copy) ClosePin(p);
}

void PinManager::ClickThroughOffAll() {
    for (auto* p : g_pins) if (p->clickThrough) { p->clickThrough = false; ApplyStyles(p); Render(p); }
}

size_t PinManager::count() const { return g_pins.size(); }

std::vector<std::wstring> PinManager::History() const {
    std::vector<std::wstring> files;
    std::wstring dir = PinsDir();
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\pin_*.png").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) { do files.push_back(dir + L"\\" + fd.cFileName); while (FindNextFileW(f, &fd)); FindClose(f); }
    std::sort(files.rbegin(), files.rend());
    return files;
}

bool PinManager::ReopenHistory(size_t i) {
    auto h = History();
    if (i >= h.size()) return false;
    Image img;
    if (!LoadImageFile(h[i], img)) return false;
    DeleteFileW(h[i].c_str());                   // back on the screen: it leaves the history (closing adds it again)
    Pin(img, Tr(L"Imagem fixada"));
    return true;
}

bool PinManager::ReopenLast() { return ReopenHistory(0); }

} // namespace snap
