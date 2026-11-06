#include "CaptureEngine.h"
#include "Clipboard.h"
#include "Cv.h"
#include "Hdr.h"
#include "Ocr.h"
#include "Sounds.h"
#include "Ui.h"
#include <dwmapi.h>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace snap {

static const wchar_t* StateName(CaptureEngine::State s) {
    static const wchar_t* n[] = { L"Idle", L"Activated", L"InputSelecting", L"InputSelected", L"Capturing", L"HaveInput", L"Outputting", L"Suspended" };
    return n[s];
}

void CaptureEngine::Init(HWND frame, ipc::Notify* ipc) { frame_ = frame; ipc_ = ipc; }

void CaptureEngine::SetState(State s) {
    if (s == state_) return;
    Log(L"CaptureEngine: %s → %s", StateName(state_), StateName(s));
    bool wasBusy = state_ != Idle;
    state_ = s;
    stateSince_ = GetTickCount64();
    if (s == Idle) clean_.End();                  // desktop icons / notifications back as they were
    if (onBusyChanged && wasBusy != (s != Idle)) onBusyChanged(s != Idle);
}

// ---------------------------------------------------------------- delay with countdown
namespace {
struct DelayState { int left; std::function<void()> then; };
LRESULT CALLBACK DelayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = (DelayState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT r; GetClientRect(h, &r);
        HBRUSH b = CreateSolidBrush(RGB(28, 28, 30)); FillRect(dc, &r, b); DeleteObject(b);
        int dpi = DpiForWindow(h);
        ui::DrawTextCentered(dc, st ? Format(Tr(L"Captura em %d s…"), st->left) : L"", r, RGB(255, 255, 255), ui::UiFont(dpi, 12, true));
        EndPaint(h, &ps); return 0;
    }
    case WM_TIMER:
        if (st && --st->left <= 0) {
            KillTimer(h, 1);
            auto then = std::move(st->then);
            delete st;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            DestroyWindow(h);
            if (then) then();
        } else InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}

void CaptureEngine::ShowDelay(int seconds, std::function<void()> then) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = DelayProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SnapnabDelay";
        RegisterClassW(&wc); reg = true;
    }
    POINT c; GetCursorPos(&c);
    RECT wa = MonitorRectFromPoint(c, true);
    int dpi = DpiForWindow(frame_);
    int W = ui::Scale(220, dpi), H = ui::Scale(54, dpi);
    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, L"SnapnabDelay", L"", WS_POPUP,
                             wa.right - W - ui::Scale(16, dpi), wa.bottom - H - ui::Scale(16, dpi), W, H, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    SetLayeredWindowAttributes(h, 0, 235, LWA_ALPHA);
    SetWindowRgn(h, CreateRoundRectRgn(0, 0, W + 1, H + 1, ui::Scale(14, dpi), ui::Scale(14, dpi)), FALSE);
    SetWindowDisplayAffinity(h, 0x11 /*WDA_EXCLUDEFROMCAPTURE*/);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)new DelayState{ seconds, std::move(then) });
    ShowWindow(h, SW_SHOWNOACTIVATE);
    SetTimer(h, 1, 1000, nullptr);
}

// ---------------------------------------------------------------- input
// Something of the capture still on the screen (selection, delay, recording HUD, panoramic bar) or running.
bool CaptureEngine::UiAlive() {
    if (recorder.state() != Recorder::Idle || (hud_.hwnd() && IsWindow(hud_.hwnd()))) return true;
    for (const wchar_t* cls : { L"SnapnabSelectWindowClass", L"SnapnabDelay", L"SnapnabPanoBar", L"SnapnabCountdown" })
        if (FindWindowW(cls, nullptr)) return true;
    return GetTickCount64() - stateSince_ < 15000;     // a capture being processed (scrolling, effects, outputs)
}

bool CaptureEngine::Activate(const Preset& preset) {
    if (state_ != Idle) {
        if (UiAlive()) {
            Log(L"CaptureEngine: busy (%s), activation ignored", StateName(state_));
            return false;
        }
        // nothing of a capture left on the screen: a capture that ended without coming back to Idle would block every
        // new one until Snapnab restarted (the record button "stopped working")
        Log(L"CaptureEngine: stale state %s with nothing on the screen, reset", StateName(state_));
        recorder.StopMonitor();
        hud_.Hide();
        StopWebcam();
        frozen_ = Image(); cursorImg_ = Image();
        SetState(Idle);
    }
    preset_ = preset;
    Log(L"CaptureEngine: preset \"%s\"%s", preset_.name.c_str(), preset_.effects.Any() ? L" with effects" : L"");
    SetState(Activated);
    if (frame_ && IsWindowVisible(frame_)) ShowWindow(frame_, SW_HIDE);
    // the screen-based selections: desktop icons hidden before the screen is frozen (C16)
    if (preset_.selection != Selection::Clipboard && preset_.selection != Selection::File) clean_.Begin(preset_.kind == CaptureKind::Video);
    int delay = preset_.kind == CaptureKind::Image ? preset_.delaySeconds : 0;
    if (preset_.selection == Selection::Menu && delay == 0) delay = 3;        // time to open the menu
    auto go = [this] {
        // short wait for the main window to disappear before freezing the screen
        SetTimer(frame_, 0x5A, 180, [](HWND h, UINT, UINT_PTR id, DWORD) {
            KillTimer(h, id);
            auto* self = (CaptureEngine*)GetPropW(h, L"SnapnabEngine");
            if (self) self->BeginInput();
        });
    };
    SetPropW(frame_, L"SnapnabEngine", this);
    if (delay > 0) ShowDelay(delay, go); else go();
    return true;
}

// the cursor as an image with transparency: drawn over black and over white, the difference gives the alpha (also
// for the old monochrome cursors, which have no alpha of their own)
static Image CursorBitmap(HCURSOR hc, POINT& hotspot) {
    ICONINFO ii{};
    if (!GetIconInfo(hc, &ii)) return {};
    BITMAP bm{};
    GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof bm, &bm);
    int w = bm.bmWidth, h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
    hotspot = { (LONG)ii.xHotspot, (LONG)ii.yHotspot };
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return {};
    auto render = [&](uint32_t bg) {
        Image im(w, h, bg);
        HBITMAP hb = im.ToHBITMAP();
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(dc, hb);
        DrawIconEx(dc, 0, 0, hc, w, h, 0, nullptr, DI_NORMAL);
        SelectObject(dc, o); DeleteDC(dc);
        Image r = Image::FromHBITMAP(hb);
        DeleteObject(hb);
        return r;
    };
    Image b = render(0xFF000000), wt = render(0xFFFFFFFF), out(w, h, 0);
    if (b.w != w || wt.w != w) return {};
    for (size_t i = 0; i < out.px.size(); i++) {
        uint32_t pb = b.px[i], pw = wt.px[i];
        int a = 255 - ((CR(pw) - CR(pb)) + (CG(pw) - CG(pb)) + (CB(pw) - CB(pb))) / 3;
        a = std::clamp(a, 0, 255);
        if (a == 0) continue;
        out.px[i] = ARGB(a, std::min(255, CR(pb) * 255 / a), std::min(255, CG(pb) * 255 / a), std::min(255, CB(pb) * 255 / a));
    }
    return out;
}

Image CaptureEngine::FreezeScreen(RECT vs, bool withCursor) {
    HDC screen = GetDC(nullptr);
    Image img = Image::FromDC(screen, vs);
    ReleaseDC(nullptr, screen);
    if (hdr::Enabled() && hdr::IsHdrArea(vs)) hdr::FixCapture(vs, img);     // HDR monitors: tone-mapped copy (C02)
    if (withCursor && RegGetDword(L"CursorAsObject", 0)) {
        // kept apart: InputCaptured makes it an object (or draws it in when effects change the geometry)
        CURSORINFO ci{ sizeof(ci) };
        cursorImg_ = Image();
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
            POINT hot{};
            cursorImg_ = CursorBitmap(ci.hCursor, hot);
            cursorPos_ = { ci.ptScreenPos.x - hot.x, ci.ptScreenPos.y - hot.y };
        }
        return img;
    }
    if (withCursor) {
        CURSORINFO ci{ sizeof(ci) };
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
            HBITMAP hb = img.ToHBITMAP();
            HDC dc = CreateCompatibleDC(nullptr);
            HGDIOBJ o = SelectObject(dc, hb);
            ICONINFO ii{};
            GetIconInfo(ci.hCursor, &ii);
            DrawIconEx(dc, ci.ptScreenPos.x - vs.left - ii.xHotspot, ci.ptScreenPos.y - vs.top - ii.yHotspot, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
            SelectObject(dc, o); DeleteDC(dc);
            img = Image::FromHBITMAP(hb);
            img.MakeOpaque();
            DeleteObject(hb);
        }
    }
    return img;
}

void CaptureEngine::BeginInput() {
    SetState(InputSelecting);
    cursorImg_ = Image();
    if (preset_.kind == CaptureKind::Video && preset_.audioOnly) {      // audio only (C32): no area to select
        POINT c; GetCursorPos(&c);
        StartVideoHud(MonitorRectFromPoint(c));
        return;
    }
    vs_ = VirtualScreenRect();
    const bool video = preset_.kind == CaptureKind::Video;
    CaptureInfo info; info.mode = SelectionName(preset_.selection);

    const bool fixedRegion = preset_.selection == Selection::FixedRegion || (preset_.selection == Selection::Region && preset_.regionFixed);
    if (fixedRegion && preset_.regionUsePos) {
        // fixed region with a position: the input is ready without a selector (INPUT_REGION_FIXED, comp_1 §6)
        RECT r{ preset_.regionX, preset_.regionY, preset_.regionX + preset_.fixedW, preset_.regionY + preset_.fixedH };
        IntersectRect(&r, &r, &vs_);
        if (IsRectEmpty(&r)) {
            if (notify) notify(L"Snapnab", Tr(L"A região fixa está fora da tela. Ajuste a posição na engrenagem da Seleção."));
            InputCanceled(); return;
        }
        info.mode = Tr(L"Região fixa");
        if (video) { StartVideoHud(r); return; }
        SetState(Capturing);
        InputCaptured(FreezeScreen(r, preset_.captureCursor), info, {}, { r.left, r.top });
        return;
    }
    switch (preset_.selection) {
    case Selection::FullScreen:
    case Selection::Monitor: {
        POINT c; GetCursorPos(&c);
        RECT r = preset_.selection == Selection::FullScreen ? vs_ : MonitorRectFromPoint(c);
        if (video) { StartVideoHud(r); return; }
        SetState(Capturing);
        Image img = FreezeScreen(r, preset_.captureCursor);
        std::vector<CapturedLink> links = preset_.captureLinks ? FindLinks(r) : std::vector<CapturedLink>{};
        if (preset_.windowObjects) {
            // each window as a separate object on top of the desktop background (the outputs get the screen as it was)
            Layers ly;
            if (CaptureWindowsAsObjects(r, FreezeScreen(r, false), ly)) {
                sounds::PlayCaptureSound();
                haveOrigin_ = false;
                for (auto& l : links)
                    ly.objects.add("Link").seti("X1", l.r.left - r.left).seti("Y1", l.r.top - r.top).seti("X2", l.r.right - r.left)
                      .seti("Y2", l.r.bottom - r.top).setW("Url", l.url);
                info.mode = Tr(L"Janelas como objetos");
                SetState(Outputting);
                Output(img, info, &ly);
                SetState(Idle);
                return;
            }
        }
        InputCaptured(std::move(img), info, std::move(links), { r.left, r.top });
        return;
    }
    case Selection::Clipboard: {
        Image img;
        if (!GetClipboardImage(img)) {
            if (notify) notify(L"Snapnab", Tr(L"A área de transferência não contém imagem."));
            InputCanceled(); return;
        }
        info.application = Tr(L"Área de transferência");
        InputCaptured(std::move(img), info);
        return;
    }
    case Selection::File: {
        std::wstring path;
        if (!ui::OpenFileDialog(nullptr, ImageOpenFilter(), path, Tr(L"Capturar de arquivo gráfico"))) { InputCanceled(); return; }
        Image img;
        if (!LoadImageFile(path, img)) { MessageBoxW(nullptr, Tr(L"Não foi possível abrir a imagem."), L"Snapnab", MB_ICONWARNING); InputCanceled(); return; }
        info.windowTitle = FileNameOf(path);
        InputCaptured(std::move(img), info);
        return;
    }
    case Selection::Menu: {
        SetState(Capturing);
        Image img;
        if (CaptureMenus(img)) { InputCaptured(std::move(img), info); return; }
        if (notify) notify(L"Snapnab", Tr(L"Nenhum menu aberto encontrado; selecione a região."));
        break;   // falls back to region selection
    }
    default: break;
    }

    frozen_ = FreezeScreen(vs_, false);
    Image shown = preset_.captureCursor && !video ? FreezeScreen(vs_, true) : frozen_;
    if (preset_.captureCursor && !video) {
        frozen_ = shown;
        // cursor as an object: the crosshair shows it, the frozen copy stays without it
        if (!cursorImg_.empty()) shown.Blit(cursorImg_, cursorPos_.x - vs_.left, cursorPos_.y - vs_.top, true);
    }
    if (!video) clean_.End();                     // the frozen copy is taken: the icons can come back
    SelectMode mode = fixedRegion ? SelectMode::Fixed : SelectMode::Region;
    switch (preset_.selection) {
    case Selection::Window: mode = SelectMode::Window; break;
    case Selection::Object: mode = SelectMode::Object; break;
    case Selection::FixedRegion: mode = SelectMode::Fixed; break;
    case Selection::Freehand: mode = SelectMode::Freehand; break;
    case Selection::MultiArea: mode = SelectMode::MultiArea; break;
    case Selection::Scrolling: case Selection::ExtendedWindow: mode = SelectMode::PickWindow; break;
    default: break;          // Panoramic: a normal region; scrolling starts after the selection
    }
    SelectOptions so;
    so.quickCrop = RegGetDword(L"QuickCrop", 1) != 0;
    so.openCapture = [this](const std::wstring& path) { DeliverToEditor(path); };     // H in the crosshair
    RunSelectOverlay(shown, vs_, mode, { preset_.fixedW, preset_.fixedH }, video,
                     [this](const SelectResult& r) { if (r.ok) OnInputSelected(r); else InputCanceled(); }, so);
}

void CaptureEngine::OnInputSelected(const SelectResult& r) {
    SetState(InputSelected);
    // regions recalled with Tab in the crosshair (C18)
    if (preset_.selection == Selection::Region || preset_.selection == Selection::Window || preset_.selection == Selection::Object)
        RememberRegion(r.rect);
    CaptureInfo info;
    info.mode = SelectionName(preset_.selection);
    if (r.window) { info.windowTitle = WindowTitle(r.window); info.application = WindowProcessName(r.window); }

    if (preset_.kind == CaptureKind::Video) {
        RECT a = r.rect;
        if (preset_.selection == Selection::Window && r.window) a = WindowFrameRect(r.window);
        IntersectRect(&a, &a, &vs_);
        StartVideoHud(a);
        return;
    }
    SetState(Capturing);
    if (preset_.selection == Selection::Panoramic) {
        // the frozen screen is no use: the region is copied live while the user scrolls
        frozen_ = Image();
        pano_.Start(r.rect, [this, info](Image img) {
            if (img.empty()) InputCanceled();
            else InputCaptured(std::move(img), info);
        });
        return;
    }
    RECT local = r.rect;
    OffsetRect(&local, -vs_.left, -vs_.top);
    // links of web pages and documents under the captured area (found before the screen changes)
    std::vector<CapturedLink> links;
    if (preset_.captureLinks && preset_.selection != Selection::Scrolling && preset_.selection != Selection::ExtendedWindow)
        links = FindLinks(r.rect);
    Image img;
    switch (preset_.selection) {
    case Selection::Scrolling:
        cursorImg_ = Image();
        img = ScrollCapture(r.window, r.child, { (r.rect.left + r.rect.right) / 2, (r.rect.top + r.rect.bottom) / 2 });
        break;
    case Selection::ExtendedWindow:
        cursorImg_ = Image();
        img = ExtendedWindowCapture(r.window);
        break;
    case Selection::Freehand: {
        img = frozen_.Crop(local);
        std::vector<POINT> pts;
        for (auto p : r.path) pts.push_back({ p.x - r.rect.left, p.y - r.rect.top });
        cv::Mask m = cv::PolygonMask(img.w, img.h, pts);
        for (size_t i = 0; i < m.size(); i++) if (!m[i]) img.px[i] = 0;    // outside the outline = transparent
        break;
    }
    case Selection::MultiArea: {
        img = Image(local.right - local.left, local.bottom - local.top, 0);
        for (RECT a : r.rects) {
            OffsetRect(&a, -vs_.left, -vs_.top);
            img.Blit(frozen_.Crop(a), a.left - local.left, a.top - local.top, false);
        }
        break;
    }
    case Selection::Window:
        img = frozen_.Crop(local);
        // transparent rounded corners and, optionally, a shadow like the window's own (C06)
        if (r.window && RegGetDword(L"WindowRoundCorners", 1)) RoundWindowCorners(img, r.window);
        if (r.window && RegGetDword(L"WindowShadow", 0)) {
            if (!cursorImg_.empty()) { img.Blit(cursorImg_, cursorPos_.x - r.rect.left, cursorPos_.y - r.rect.top, true); cursorImg_ = Image(); }
            img = fx::Shadow(img, 0, 14, 45);
        }
        break;
    default:
        img = frozen_.Crop(local);
    }
    frozen_ = Image();
    if (img.empty()) { InputCanceled(); return; }
    InputCaptured(std::move(img), info, std::move(links), { r.rect.left, r.rect.top });
}

void CaptureEngine::InputCanceled() {
    frozen_ = Image();
    cursorImg_ = Image();
    SetState(Idle);
}

void CaptureEngine::InputCaptured(Image img, const CaptureInfo& info, std::vector<CapturedLink> links, POINT origin) {
    SetState(HaveInput);
    origin_ = origin;
    haveOrigin_ = preset_.selection != Selection::Clipboard && preset_.selection != Selection::File && !preset_.effects.Any();
    if (preset_.selection != Selection::Clipboard && preset_.selection != Selection::File) sounds::PlayCaptureSound();   // C31
    // captured cursor (C25): an object over the image, or drawn in when an effect changes the geometry
    Image cursor = std::move(cursorImg_);
    cursorImg_ = Image();
    POINT cpos{ cursorPos_.x - origin.x, cursorPos_.y - origin.y };
    if (!cursor.empty()) {
        RECT cr{ cpos.x, cpos.y, cpos.x + cursor.w, cpos.y + cursor.h }, ir{ 0, 0, img.w, img.h }, x;
        if (!IntersectRect(&x, &cr, &ir)) cursor = Image();
        else if (preset_.effects.Any()) { img.Blit(cursor, cpos.x, cpos.y, true); cursor = Image(); }
    }
    Image out = fx::ApplyCaptureEffects(img, preset_.effects);          // preset effects (CEffectSettings*)
    SetState(Outputting);
    // the links follow the image: scale and border move them; other effects that change the geometry drop them
    const auto& e = preset_.effects;
    bool keep = !links.empty() && !e.trim && !e.rotate && !e.caption && !e.edge && !e.shadow;
    Layers ly;
    ly.base = out;
    if (keep) {
        double k = e.scale ? e.scalePercent / 100.0 : 1.0;
        int off = e.border ? e.borderWidth : 0;
        for (auto& l : links) {
            RECT a{ (LONG)std::lround((l.r.left - origin.x) * k) + off, (LONG)std::lround((l.r.top - origin.y) * k) + off,
                    (LONG)std::lround((l.r.right - origin.x) * k) + off, (LONG)std::lround((l.r.bottom - origin.y) * k) + off };
            RECT bounds{ 0, 0, out.w, out.h }, vis;
            if (!IntersectRect(&vis, &a, &bounds)) continue;
            ly.objects.add("Link").seti("X1", vis.left).seti("Y1", vis.top).seti("X2", vis.right).seti("Y2", vis.bottom).setW("Url", l.url);
        }
    }
    if (!cursor.empty()) {
        ly.embedded["Cursor.png"] = cursor;
        ly.objects.add("Image").seti("X1", cpos.x).seti("Y1", cpos.y).seti("X2", cpos.x + cursor.w).seti("Y2", cpos.y + cursor.h)
            .set("StreamName", "Cursor.png").seti("PenWidth", 0).seti("Shadow", 0).setW("Name", Tr(L"Cursor do mouse"));
    }
    if (!ly.objects.children.empty()) {
        Image flat = out;                       // the outputs (file, clipboard) get the image as seen, with the cursor
        if (!cursor.empty()) flat.Blit(cursor, cpos.x, cpos.y, true);
        Output(flat, info, &ly);
        SetState(Idle);
        return;
    }
    Output(out, info);
    SetState(Idle);
}

// ---------------------------------------------------------------- each window as an object (F31)
namespace {
bool IsCloaked(HWND h) { DWORD c = 0; return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof c)) && c; }

// image of one window (even the parts covered by other windows): PrintWindow with the DWM content; if the program
// does not draw that way (all black), the visible part of the screen capture is used
Image WindowImage(HWND h, RECT frame, const Image& screen, RECT area) {
    RECT wr; GetWindowRect(h, &wr);
    int w = wr.right - wr.left, hh = wr.bottom - wr.top;
    Image img;
    if (w > 0 && hh > 0 && (long long)w * hh < 80000000) {
        HDC sdc = GetDC(nullptr);
        BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -hh, 1, 32, BI_RGB };
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC mem = CreateCompatibleDC(sdc);
        HGDIOBJ o = SelectObject(mem, dib);
        BOOL ok = PrintWindow(h, mem, PW_RENDERFULLCONTENT);
        GdiFlush();
        if (ok && bits) { img = Image(w, hh); memcpy(img.px.data(), bits, img.px.size() * 4); }
        SelectObject(mem, o); DeleteObject(dib); DeleteDC(mem);
        ReleaseDC(nullptr, sdc);
        if (!img.empty()) {
            img.MakeOpaque();
            RECT crop{ frame.left - wr.left, frame.top - wr.top, frame.right - wr.left, frame.bottom - wr.top };
            if (crop.right > crop.left && crop.bottom > crop.top) img = img.Crop(crop);
            size_t dark = 0;
            for (uint32_t p : img.px) if ((p & 0x00FFFFFF) == 0) dark++;
            if (dark > img.px.size() * 97 / 100) img = Image();      // nothing was drawn
        }
    }
    if (img.empty()) {                          // fallback: what was visible on the screen
        RECT vis;
        if (!IntersectRect(&vis, &frame, &area)) return {};
        OffsetRect(&vis, -area.left, -area.top);
        img = screen.Crop(vis);
    }
    if (!img.empty()) RoundWindowCorners(img, h);
    return img;
}
}

void RoundWindowCorners(Image& img, HWND h) {
    if (img.empty() || !h) return;
    WINDOWPLACEMENT wp{ sizeof wp };
    OSVERSIONINFOEXW ov{ sizeof ov }; ov.dwBuildNumber = 22000;
    bool win11 = VerifyVersionInfoW(&ov, VER_BUILDNUMBER, VerSetConditionMask(0, VER_BUILDNUMBER, VER_GREATER_EQUAL)) != 0;
    if (!win11 || !GetWindowPlacement(GetAncestor(h, GA_ROOT), &wp) || wp.showCmd == SW_SHOWMAXIMIZED) return;
    DWORD pref = 0;                                   // DWMWCP_DEFAULT / ROUND = 8 px, ROUNDSMALL = 4 px, DONOTROUND
    DwmGetWindowAttribute(GetAncestor(h, GA_ROOT), 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof pref);
    if (pref == 1 /*DWMWCP_DONOTROUND*/) return;
    int rad = std::max(2, MulDiv(pref == 3 /*DWMWCP_ROUNDSMALL*/ ? 4 : 8, (int)GetDpiForWindow(h), 96));
    for (int y = 0; y < std::min(rad, img.h); y++)
        for (int x = 0; x < std::min(rad, img.w); x++) {
            float dx = rad - x - 0.5f, dy = rad - y - 0.5f, d = std::sqrt(dx * dx + dy * dy);
            int a = (int)std::clamp((rad - d + 0.5f) * 255.0f, 0.0f, 255.0f);
            if (a >= 255) continue;
            for (auto [px, py] : std::initializer_list<std::pair<int, int>>{ { x, y }, { img.w - 1 - x, y }, { x, img.h - 1 - y }, { img.w - 1 - x, img.h - 1 - y } }) {
                uint32_t& p = img.at(px, py);
                p = (p & 0x00FFFFFF) | ((uint32_t)(CA(p) * a / 255) << 24);
            }
        }
}

bool CaptureEngine::CaptureWindowsAsObjects(RECT area, const Image& screen, Layers& out) {
    struct Win { HWND h; RECT frame; };
    std::vector<Win> wins;                      // top to bottom (EnumWindows order)
    std::vector<HWND> bars;                     // taskbars: they stay in the background
    struct Ctx { RECT area; std::vector<Win>* wins; std::vector<HWND>* bars; } ctx{ area, &wins, &bars };
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto* c = (Ctx*)p;
        if (!IsWindowVisible(h) || IsIconic(h) || IsCloaked(h)) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId()) return TRUE;
        wchar_t cls[64] = L""; GetClassNameW(h, cls, 64);
        if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) { c->bars->push_back(h); return TRUE; }
        if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) return TRUE;
        LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
        if (ex & WS_EX_TRANSPARENT) return TRUE;
        if (ex & WS_EX_LAYERED) {
            BYTE alpha = 255; DWORD flags = 0;
            if (GetLayeredWindowAttributes(h, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha < 8) return TRUE;
        }
        RECT fr = WindowFrameRect(h), x;
        if (fr.right - fr.left < 16 || fr.bottom - fr.top < 16 || !IntersectRect(&x, &fr, &c->area)) return TRUE;
        if (c->wins->size() < 40) c->wins->push_back({ h, fr });
        return TRUE;
    }, (LPARAM)&ctx);
    if (wins.empty()) return false;
    int w = area.right - area.left, h = area.bottom - area.top;
    // background: the desktop wallpaper (as Windows paints it) and the taskbars from the screen capture
    {
        HDC sdc = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(sdc);
        HBITMAP bmp = CreateCompatibleBitmap(sdc, w, h);
        HGDIOBJ o = SelectObject(mem, bmp);
        HBRUSH desk = CreateSolidBrush(GetSysColor(COLOR_DESKTOP));
        RECT all{ 0, 0, w, h }; FillRect(mem, &all, desk); DeleteObject(desk);
        SetViewportOrgEx(mem, -area.left, -area.top, nullptr);
        PaintDesktop(mem);
        SetViewportOrgEx(mem, 0, 0, nullptr);
        SelectObject(mem, o);
        out.base = Image::FromHBITMAP(bmp);
        out.base.MakeOpaque();
        DeleteObject(bmp); DeleteDC(mem);
        ReleaseDC(nullptr, sdc);
    }
    if (out.base.w != w || out.base.h != h) out.base = Image(w, h, 0xFF000000 | FromColorref(GetSysColor(COLOR_DESKTOP)));
    for (HWND b : bars) {
        RECT br; GetWindowRect(b, &br);
        RECT vis;
        if (!IntersectRect(&vis, &br, &area)) continue;
        OffsetRect(&vis, -area.left, -area.top);
        out.base.Blit(screen.Crop(vis), vis.left, vis.top, false);
    }
    // windows from the back to the front, named after their titles (layers panel)
    int counter = 1;
    for (auto it = wins.rbegin(); it != wins.rend(); ++it) {
        Image wi = WindowImage(it->h, it->frame, screen, area);
        if (wi.empty()) continue;
        std::string stream = FormatA("Image_%d.png", counter++);
        out.embedded[stream] = std::move(wi);
        std::wstring title = WindowTitle(it->h);
        if (title.empty()) title = WindowProcessName(it->h);
        out.objects.add("Image").seti("X1", it->frame.left - area.left).seti("Y1", it->frame.top - area.top)
            .seti("X2", it->frame.right - area.left).seti("Y2", it->frame.bottom - area.top)
            .set("StreamName", stream).seti("PenWidth", 0).seti("Shadow", 0).setW("Name", title);
    }
    Log(L"Windows as objects: %d window(s)", counter - 1);
    return counter > 1;
}

// ---------------------------------------------------------------- output (COutputter)
// Grab Text: recognizes the text, copies it to the clipboard and shows it in a window
void CaptureEngine::GrabText(const Image& img) {
    std::wstring text, err;
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    bool ok = RunOcr(img, text, err);
    SetCursor(old);
    if (!ok) { MessageBoxW(nullptr, (Tr(L"Não foi possível reconhecer o texto.\n\n") + err).c_str(), Tr(L"Snapnab — Capturar texto"), MB_ICONWARNING); return; }
    if (Trim(text).empty()) { if (notify) notify(Tr(L"Snapnab — Capturar texto"), Tr(L"Nenhum texto encontrado na captura.")); return; }
    if (OpenClipboard(frame_)) {
        EmptyClipboard();
        size_t cb = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, cb)) {
            memcpy(GlobalLock(hg), text.c_str(), cb);
            GlobalUnlock(hg);
            if (!SetClipboardData(CF_UNICODETEXT, hg)) GlobalFree(hg);
        }
        CloseClipboard();
    }
    ui::ShowTextWindow(nullptr, Tr(L"Capturar texto — copiado para a área de transferência"), text);
}

void CaptureEngine::Output(const Image& img, const CaptureInfo& info, const Layers* layers) {
    if (preset_.grabText) GrabText(img);
    std::wstring lib = layers ? AddLayeredToLibrary(layers->base.empty() ? img : layers->base, img, info, layers->objects, layers->embedded)
                              : AddImageToLibrary(img, info);
    std::vector<std::wstring> done;
    // every capture goes to the clipboard (owner's request, Options › CopyEveryCapture, on by default) besides the
    // preset's own "Copy to the clipboard"; Grab Text and Read QR code keep their text there instead
    const int mode = preset_.outputClipboardOnly ? AfterEditor : AfterCaptureMode();
    const bool autoCopy = RegGetDword(L"CopyEveryCapture", 1) != 0 || mode == AfterCopyOnly;
    if ((preset_.copyToClipboard || autoCopy || preset_.outputClipboardOnly) && !preset_.grabText && !preset_.readQr && CopyImageToClipboard(frame_, img))
        done.push_back(Tr(L"copiada para a área de transferência"));
    const std::wstring title = info.windowTitle.empty() ? info.mode : info.windowTitle;
    if (!preset_.outputPath.empty()) {                // command line /output "file"
        EnsureDir(DirOf(preset_.outputPath));
        if (SaveImageFile(preset_.outputPath, img)) done.push_back(Tr(L"salva em ") + preset_.outputPath);
        else Log(L"Output: failed to save %s", preset_.outputPath.c_str());
    }
    if (preset_.readQr && onReadQr) onReadQr(img);
    if (preset_.pinToScreen && onPin) onPin(img, title, haveOrigin_ ? &origin_ : nullptr);
    if (preset_.office >= 0 && onOffice) onOffice(img, preset_.office);
    if (preset_.upload && onUpload) onUpload(img, lib, title);
    std::wstring saved;
    if (preset_.saveToFile) {
        std::wstring folder = preset_.folder.empty() ? DefaultSaveDir() : preset_.folder;
        EnsureDir(folder);
        // the default prefix follows the language; one the user typed is kept as is
        std::wstring prefix = preset_.filePrefix == L"Captura" ? std::wstring(Tr(L"Captura")) : preset_.filePrefix;
        saved = NextAutoName(folder, prefix, FileFormatExt(preset_.fileFormat));
        if (SaveImageFile(saved, img)) done.push_back(Tr(L"salva em ") + saved);
        else { Log(L"Output: failed to save %s", saved.c_str()); saved.clear(); }
    }
    if (preset_.openInProgram && !preset_.programPath.empty()) {
        std::wstring file = saved;
        if (file.empty()) {
            wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
            file = std::wstring(tmp) + L"Snapnab_" + TimeStamp() + L".png";
            SaveImageFile(file, img);
        }
        ShellExecuteW(nullptr, L"open", preset_.programPath.c_str(), (L"\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    }
    if (ipc_) ipc_->SendInt(ipc::kEditor, ipc::LibraryChanged, 0);
    // after the capture: the Editor (as the preset says), the quick access overlay (C08) instead of it, both, or
    // neither (only the clipboard, with the balloon below)
    const bool other = preset_.grabText || preset_.readQr || preset_.pinToScreen;      // outputs that are already the feedback
    const bool overlay = mode == AfterOverlay || mode == AfterBoth;
    const bool editor = mode == AfterEditor || mode == AfterBoth;
    if (overlay && !other && onOverlay && !lib.empty()) onOverlay(img, lib, false, title);
    if (preset_.previewInEditor && !lib.empty() && editor && !preset_.outputClipboardOnly) DeliverToEditor(lib);
    else if (other || overlay) {}               // the text window, the QR results, the pin or the overlay are the feedback
    else if (notify) {
        std::wstring t = Tr(L"Captura adicionada à biblioteca");
        for (auto& d : done) t += L"; " + d;
        notify(L"Snapnab", t + L".");
    }
}

void CaptureEngine::DeliverToEditor(const std::wstring& path) {
    // Editor open: command line via WM_COPYDATA 0x8010 + IPC 6 (show); closed: /Open "<file>"
    std::wstring cmd = L"\"" + ExeDir() + L"\\" + kEditorExe + L"\" /Open \"" + path + L"\"";
    HWND ed = FindWindowW(kEditorWndClass, nullptr);
    AllowSetForegroundWindow(ASFW_ANY);
    if (ed && SendCommandLine(ed, cmd)) {
        if (ipc_) ipc_->SendInt(ipc::kEditor, ipc::ShowEditor, 1);
        return;
    }
    LaunchSibling(kEditorExe, L"/Open \"" + path + L"\"");
}

// ---------------------------------------------------------------- menu
bool CaptureEngine::CaptureMenus(Image& out) {
    std::vector<RECT> rects;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        wchar_t cls[64]; GetClassNameW(h, cls, 64);
        if (IsWindowVisible(h) && wcscmp(cls, L"#32768") == 0) {
            RECT r; GetWindowRect(h, &r);
            ((std::vector<RECT>*)lp)->push_back(r);
        }
        return TRUE;
    }, (LPARAM)&rects);
    if (rects.empty()) return false;
    RECT u = rects[0];
    for (auto& r : rects) UnionRect(&u, &u, &r);
    // includes the active window's menu bar, if any
    HWND fg = GetForegroundWindow();
    MENUBARINFO mbi{ sizeof(mbi) };
    if (fg && GetMenuBarInfo(fg, OBJID_MENU, 0, &mbi) && !IsRectEmpty(&mbi.rcBar)) UnionRect(&u, &u, &mbi.rcBar);
    Image full = FreezeScreen(u, preset_.captureCursor);
    out = Image(full.w, full.h, 0);                     // outside the menus = transparent
    for (auto r : rects) { OffsetRect(&r, -u.left, -u.top); out.Blit(full.Crop(r), r.left, r.top, false); }
    if (!IsRectEmpty(&mbi.rcBar)) { RECT r = mbi.rcBar; OffsetRect(&r, -u.left, -u.top); out.Blit(full.Crop(r), r.left, r.top, false); }
    return true;
}

// ---------------------------------------------------------------- scrolling (CScroll::Capture, §3.2)
static bool SameImage(const Image& a, const Image& b) {
    if (a.w != b.w || a.h != b.h) return false;
    size_t diff = 0;
    for (size_t i = 0; i < a.px.size(); i += 7)
        if ((a.px[i] & 0xFFFFFF) != (b.px[i] & 0xFFFFFF)) diff++;
    return diff * 7 < a.px.size() / 2000 + 1;
}

static void Wheel(int notches, bool horizontal) {
    INPUT in{}; in.type = INPUT_MOUSE;
    in.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    in.mi.mouseData = (DWORD)(notches * WHEEL_DELTA);
    SendInput(1, &in, sizeof(in));
}

Image CaptureEngine::ScrollCapture(HWND top, HWND child, POINT at) {
    if (!top) return {};
    // scrollable area: the control under the click (or the window's client area)
    HWND area = child && child != top ? child : top;
    RECT r;
    GetClientRect(area, &r);
    MapWindowPoints(area, nullptr, (POINT*)&r, 2);
    RECT fr = WindowFrameRect(top);
    IntersectRect(&r, &r, &fr);
    IntersectRect(&r, &r, &vs_);
    if (r.right - r.left < 32 || r.bottom - r.top < 32) return {};
    const bool horizontal = false;      // vertical scrolling (the original's main direction)

    SetForegroundWindow(top);
    POINT saved; GetCursorPos(&saved);
    SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    Sleep(300);
    HDC screen = GetDC(nullptr);
    auto grab = [&] { return Image::FromDC(screen, r); };
    auto canceled = [] { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; };

    // starts from the top
    Image cur = grab();
    for (int i = 0; i < 40 && !canceled(); i++) {
        Wheel(10, horizontal);
        Sleep(120);
        Image n = grab();
        if (SameImage(n, cur)) break;
        cur = std::move(n);
    }
    Sleep(250);
    cur = grab();
    Image result = cur;
    int stable = 0;
    for (int iter = 0; iter < 400 && !canceled(); iter++) {
        Wheel(-3, horizontal);
        Sleep(320);
        Image next = grab();
        if (SameImage(next, cur)) { if (++stable >= 2) break; continue; }
        stable = 0;
        double conf = 0;
        POINT t = cv::DetectTranslation(cur, next, &conf);
        int s = -t.y;                    // content moved up by s pixels: next(y) = cur(y + s)
        if (s <= 0 || s >= cur.h) {
            Log(L"Scrolling: invalid offset (%d, conf %.1f) — stopping", s, conf);
            break;
        }
        Image grown(result.w, result.h + s);
        memcpy(grown.px.data(), result.px.data(), result.px.size() * 4);
        for (int y = 0; y < s; y++) memcpy(grown.row(result.h + y), next.row(next.h - s + y), next.w * 4);
        result = std::move(grown);
        cur = std::move(next);
        if (result.h > 30000) break;
    }
    ReleaseDC(nullptr, screen);
    SetCursorPos(saved.x, saved.y);
    Log(L"Scrolling: result %dx%d", result.w, result.h);
    return result;
}

// ---------------------------------------------------------------- extended window (SnapEx64, §6)
Image CaptureEngine::ExtendedWindowCapture(HWND top) {
    if (!top) return {};
    RECT wr; GetWindowRect(top, &wr);
    int curW = wr.right - wr.left, curH = wr.bottom - wr.top;
    std::vector<ui::Field> f = {
        ui::Field::MakeLabel(Tr(L"A janela será ampliada além da tela, capturada e restaurada.")),
        ui::Field::MakeInt(Tr(L"Nova largura (px)"), curW, 100, 19500),
        ui::Field::MakeInt(Tr(L"Nova altura (px)"), std::min(19500, curH * 3), 100, 19500),
    };
    if (!ui::RunForm(nullptr, Tr(L"Janela estendida"), f)) return {};
    int nw = f[1].value, nh = f[2].value;

    HMODULE dll = LoadLibraryW((ExeDir() + L"\\SnapEx64.dll").c_str());
    using Snap5000_t = BOOL(*)(UINT, UINT, DWORD);
    using Snap5001_t = void(*)();
    auto s5000 = dll ? (Snap5000_t)GetProcAddress(dll, "Snap5000") : nullptr;
    auto s5001 = dll ? (Snap5001_t)GetProcAddress(dll, "Snap5001") : nullptr;
    UINT msgEnter = RegisterWindowMessageW(L"SnapnabExtendedWindowEnter");
    UINT msgLeave = RegisterWindowMessageW(L"SnapnabExtendedWindowLeave");
    bool hooked = false;
    if (s5000 && s5001) {
        DWORD tid = GetWindowThreadProcessId(top, nullptr);
        hooked = s5000(msgEnter, msgLeave, tid) != FALSE;
        if (hooked) SendMessageTimeoutW(top, msgEnter, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr);    // the window is subclassed
    }
    if (!hooked) Log(L"Extended window: hook unavailable (DLL or 32-bit process); trying without it");
    WINDOWPLACEMENT wp{ sizeof(wp) };
    GetWindowPlacement(top, &wp);
    if (IsZoomed(top)) ShowWindow(top, SW_RESTORE);
    GetWindowRect(top, &wr);
    SetWindowPos(top, nullptr, wr.left, wr.top, nw, nh, SWP_NOZORDER | SWP_NOACTIVATE);
    Sleep(700);
    RECT big; GetWindowRect(top, &big);
    int bw = big.right - big.left, bh = big.bottom - big.top;
    Image img;
    {
        HDC screen = GetDC(nullptr);
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bw; bi.bmiHeader.biHeight = -bh; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC mem = CreateCompatibleDC(screen);
        HGDIOBJ o = SelectObject(mem, dib);
        PrintWindow(top, mem, PW_RENDERFULLCONTENT);
        GdiFlush();
        img = Image(bw, bh);
        memcpy(img.px.data(), bits, img.px.size() * 4);
        img.MakeOpaque();
        SelectObject(mem, o); DeleteObject(dib); DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }
    // removes the invisible border (DWM shadow)
    RECT fr = WindowFrameRect(top);
    RECT crop{ fr.left - big.left, fr.top - big.top, fr.right - big.left, fr.bottom - big.top };
    if (crop.right > crop.left && crop.bottom > crop.top) img = img.Crop(crop);
    SetWindowPlacement(top, &wp);
    SetWindowPos(top, nullptr, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (hooked) { SendMessageTimeoutW(top, msgLeave, 0, 0, SMTO_ABORTIFHUNG, 2000, nullptr); s5001(); }
    if (dll) FreeLibrary(dll);
    Log(L"Extended window: %dx%d", img.w, img.h);
    return img;
}

// ---------------------------------------------------------------- video
void CaptureEngine::StartVideoHud(RECT area) {
    // even size
    area.right = area.left + ((area.right - area.left) & ~1);
    area.bottom = area.top + ((area.bottom - area.top) & ~1);
    videoArea_ = area;
    SetState(InputSelected);
    HudCallbacks cb;
    cb.onRecord = [this] { VideoStartPauseResume(); };
    cb.onStop = [this] { VideoStop(); };
    cb.onRestart = [this] {
        // C33: asks first (the recording pauses meanwhile); "do not ask again" turns the question off
        if (RegGetDword(L"ConfirmRestart", 1)) {
            bool wasRecording = recorder.state() == Recorder::Recording;
            if (wasRecording) recorder.Pause();
            TASKDIALOGCONFIG tc{ sizeof tc };
            tc.hwndParent = hud_.hwnd();
            tc.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
            tc.pszWindowTitle = L"Snapnab";
            tc.pszMainIcon = TD_WARNING_ICON;
            std::wstring main = Tr(L"Recomeçar a gravação?"), content = Tr(L"O que foi gravado até agora será descartado.");
            std::wstring verify = Tr(L"Não perguntar de novo");
            tc.pszMainInstruction = main.c_str();
            tc.pszContent = content.c_str();
            tc.pszVerificationText = verify.c_str();
            tc.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
            tc.nDefaultButton = IDNO;
            int btn = 0; BOOL dontAsk = FALSE;
            TaskDialogIndirect(&tc, &btn, nullptr, &dontAsk);
            if (btn != IDYES) { if (wasRecording) recorder.Resume(); hud_.Refresh(); return; }
            if (dontAsk) RegSetDword(L"ConfirmRestart", 0);
        }
        recorder.Cancel();
        hud_.Refresh();
        hud_.Countdown(3, [this] { StartRecording(); });
    };
    cb.onZoom = [this] { VideoToggleZoom(); };
    cb.onOverlay = [this](POINT p) { VideoOverlayMenu(p); };
    cb.onCancel = [this] { VideoCancel(); };
    cb.onSnapshot = [this] {
        Image snap = recorder.TakeSnapshot();
        CaptureInfo info; info.mode = Tr(L"Imagem durante gravação");
        AddImageToLibrary(snap, info);
        if (ipc_) ipc_->SendInt(ipc::kEditor, ipc::LibraryChanged, 0);
        if (notify) notify(L"Snapnab", Tr(L"Imagem capturada sem parar a gravação."));
    };
    cb.onWebcam = [this](bool on) { webcam_.SetVisible(on); };
    cb.onVolume = [this](int mic, int sys, bool autoGain) {
        preset_.micVolume = mic; preset_.sysVolume = sys; preset_.autoGain = autoGain;
        if (onPresetVolumes) onPresetVolumes(preset_);
    };
    hud_.camAvailable = hud_.camOn = false;
    if (preset_.webcam) {
        std::wstring err;
        WebcamStyle st;
        st.shape = (CamShape)std::clamp(preset_.webcamShape, 0, (int)CamShape::Count - 1);
        st.chroma = preset_.chromaKey; st.key = preset_.chromaColor; st.tolerance = preset_.chromaTolerance;
        if (webcam_.Start(preset_.webcamDevice, area, st, err)) hud_.camAvailable = hud_.camOn = true;
        else if (notify) notify(Tr(L"Snapnab — webcam"), err);
    }
    // volumes of the preset; the meters already work before the recording (monitor of the sources)
    hud_.micVolume = preset_.micVolume; hud_.sysVolume = preset_.sysVolume; hud_.autoGain = preset_.autoGain;
    recorder.SetMicVolume(preset_.micVolume); recorder.SetSysVolume(preset_.sysVolume); recorder.SetAutoGain(preset_.autoGain);
    recorder.StartMonitor(preset_.microphone, preset_.systemAudio);
    hud_.audioOnly = preset_.audioOnly;
    if (preset_.audioOnly) hud_.camAvailable = hud_.camOn = false;
    hud_.Show(area, &recorder, cb, preset_.microphone, preset_.systemAudio);
    // audio only (C32): there is no area to frame, so it starts by itself after the countdown (Skip or Cancel still work)
    if (preset_.audioOnly) hud_.Countdown(3, [this] { StartRecording(); });
}

void CaptureEngine::StopWebcam() { webcam_.Stop(); }

void CaptureEngine::StartRecording() {
    RecordSettings rs;
    rs.rect = videoArea_;
    rs.cursor = preset_.captureCursor;
    rs.microphone = hud_.micOn;
    rs.systemAudio = hud_.sysOn;
    rs.micVolume = hud_.micVolume; rs.sysVolume = hud_.sysVolume; rs.autoGain = hud_.autoGain;
    rs.fps = preset_.fps; rs.quality = preset_.quality;
    rs.downscale1080 = preset_.downscale1080; rs.hardware = preset_.hardwareEncode;
    rs.audioOnly = preset_.audioOnly;
    rs.path = preset_.audioOnly ? NewAudioPath(preset_.audioFormat == 1) : NewVideoPath();
    // drawn in the video (C07), zoom (C15) and crash safety (C14): Options › Video options
    rs.highlightCursor = RegGetDword(L"RecHighlightCursor", 0) != 0;
    rs.highlightColor = RegGetDword(L"RecHighlightColor", RGB(255, 214, 0));
    rs.highlightSize = std::clamp((int)RegGetDword(L"RecHighlightSize", 44), 16, 200);
    rs.highlightOpacity = std::clamp((int)RegGetDword(L"RecHighlightOpacity", 35), 5, 100);
    rs.clickEffects = RegGetDword(L"RecClickEffects", 0) != 0;
    rs.leftClickColor = RegGetDword(L"RecLeftClickColor", RGB(229, 50, 45));
    rs.rightClickColor = RegGetDword(L"RecRightClickColor", RGB(0, 120, 215));
    rs.keyMode = std::clamp((int)RegGetDword(L"RecKeyMode", 0), 0, 2);
    rs.autoZoom = RegGetDword(L"RecAutoZoom", 0) != 0;
    rs.zoomPercent = std::clamp((int)RegGetDword(L"RecZoomPercent", 200), 120, 400);
    rs.crashSafe = RegGetDword(L"RecCrashSafe", 1) != 0;
    if (!recorder.Start(rs, frame_, WM_SNAP_DONE)) {
        MessageBoxW(nullptr, (Tr(L"Não foi possível iniciar a gravação.\n\n") + recorder.lastError).c_str(), L"Snapnab", MB_ICONERROR);
        recorder.StopMonitor();
        hud_.Hide();
        StopWebcam();
        SetState(Idle);
        return;
    }
    SetState(Capturing);
    hud_.Refresh();
}

void CaptureEngine::VideoStartPauseResume() {
    if (hud_.counting()) { hud_.SkipCountdown(); return; }       // hotkey during the countdown: start right away
    if (state_ == InputSelected && recorder.state() == Recorder::Idle) {
        hud_.Countdown(3, [this] { StartRecording(); });
        return;
    }
    if (recorder.state() == Recorder::Recording) { recorder.Pause(); SetState(Suspended); }    // SuspendCapture
    else if (recorder.state() == Recorder::Paused) { recorder.Resume(); SetState(Capturing); } // ResumeCapture
    hud_.Refresh();
}

void CaptureEngine::VideoToggleZoom() {
    if (recorder.state() == Recorder::Idle || preset_.audioOnly) return;
    recorder.ToggleZoom();
    hud_.Refresh();
}

void CaptureEngine::VideoOverlayMenu(POINT p) {
    bool hl = recorder.state() != Recorder::Idle ? recorder.highlightOn() : RegGetDword(L"RecHighlightCursor", 0) != 0;
    bool cl = recorder.state() != Recorder::Idle ? recorder.clicksOn() : RegGetDword(L"RecClickEffects", 0) != 0;
    int km = recorder.state() != Recorder::Idle ? recorder.keyMode() : (int)RegGetDword(L"RecKeyMode", 0);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (hl ? MF_CHECKED : 0), 1, Tr(L"Destacar o cursor"));
    AppendMenuW(m, MF_STRING | (cl ? MF_CHECKED : 0), 2, Tr(L"Mostrar os cliques"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (km == 0 ? MF_CHECKED : 0), 10, Tr(L"Teclas: não mostrar"));
    AppendMenuW(m, MF_STRING | (km == 1 ? MF_CHECKED : 0), 11, Tr(L"Teclas: só atalhos (Ctrl, Alt, Win, F1…)"));
    AppendMenuW(m, MF_STRING | (km == 2 ? MF_CHECKED : 0), 12, Tr(L"Teclas: todas (inclusive o que for digitado)"));
    SetForegroundWindow(hud_.hwnd());
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, hud_.hwnd(), nullptr);
    DestroyMenu(m);
    if (!cmd) return;
    if (cmd == 1) hl = !hl;
    else if (cmd == 2) cl = !cl;
    else km = cmd - 10;
    RegSetDword(L"RecHighlightCursor", hl); RegSetDword(L"RecClickEffects", cl); RegSetDword(L"RecKeyMode", km);
    recorder.SetOverlay(hl, cl, km);
    hud_.Refresh();
}

void CaptureEngine::VideoStop() {
    if (recorder.state() == Recorder::Idle) { if (state_ == InputSelected) VideoCancel(); return; }
    SetState(Outputting);
    bool ok = recorder.Stop();
    FinishVideo(ok);
}

void CaptureEngine::VideoCancel() {
    if (recorder.state() != Recorder::Idle) recorder.Cancel();
    recorder.StopMonitor();
    hud_.Hide();
    StopWebcam();
    SetState(Idle);
}

void CaptureEngine::FinishVideo(bool ok) {
    recorder.StopMonitor();
    hud_.Hide();
    StopWebcam();
    std::wstring path = recorder.path();
    if (ok && FileExists(path) && preset_.audioOnly) {          // audio only (C32): outside the library
        if (RegGetDword(L"CopyEveryCapture", 1)) clip::CopyFiles(frame_, { path });
        if (notify) notify(Tr(L"Snapnab — áudio salvo"), path);
        SetState(Idle);
        return;
    }
    if (ok && FileExists(path)) {
        if (!recorder.firstFrame.empty()) WriteThumbnail(path, recorder.firstFrame);
        const int mode = AfterCaptureMode();
        if (RegGetDword(L"CopyEveryCapture", 1) || mode == AfterCopyOnly) clip::CopyFiles(frame_, { path });     // videos: the file
        if (ipc_) ipc_->SendInt(ipc::kEditor, ipc::LibraryChanged, 0);
        const bool overlay = mode == AfterOverlay || mode == AfterBoth;
        if (overlay && onOverlay && !recorder.firstFrame.empty()) onOverlay(recorder.firstFrame, path, true, FileNameOf(path));
        if (preset_.previewInEditor && (mode == AfterEditor || mode == AfterBoth)) DeliverToEditor(path);
        else if (overlay) {}
        else if (notify) notify(L"Snapnab", Tr(L"Vídeo salvo na biblioteca: ") + FileNameOf(path));
    } else {
        MessageBoxW(nullptr, (Tr(L"A gravação falhou.\n\n") + recorder.lastError).c_str(), L"Snapnab", MB_ICONERROR);
    }
    SetState(Idle);
}

void CaptureEngine::OnRecorderNote(WPARAM note) {
    switch (note) {
    case Recorder::NoteDiskLow:
        if (notify) notify(Tr(L"Snapnab — pouco espaço em disco"), Tr(L"Restam menos de 1 GB livres na unidade da biblioteca."));
        break;
    case Recorder::NoteDiskFull:
        if (notify) notify(L"Snapnab", Tr(L"Gravação interrompida: espaço em disco insuficiente."));
        VideoStop();
        break;
    case Recorder::NoteError:
        VideoStop();
        break;
    }
}

} // namespace snap
