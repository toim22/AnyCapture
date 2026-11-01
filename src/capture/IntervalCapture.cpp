#include "IntervalCapture.h"
#include "Hdr.h"
#include "Library.h"
#include "SelectOverlay.h"
#include "Ui.h"

namespace snap {

namespace {
const wchar_t* const kExt[] = { L".png", L".jpg", L".webp" };

Image Grab(RECT r, bool cursor) {
    HDC screen = GetDC(nullptr);
    Image img = Image::FromDC(screen, r);
    ReleaseDC(nullptr, screen);
    if (img.empty()) return img;
    if (hdr::Enabled() && hdr::IsHdrArea(r)) hdr::FixCapture(r, img);
    if (cursor) {
        CURSORINFO ci{ sizeof ci };
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
            HBITMAP hb = img.ToHBITMAP();
            HDC dc = CreateCompatibleDC(nullptr);
            HGDIOBJ o = SelectObject(dc, hb);
            ICONINFO ii{};
            GetIconInfo(ci.hCursor, &ii);
            DrawIconEx(dc, ci.ptScreenPos.x - r.left - ii.xHotspot, ci.ptScreenPos.y - r.top - ii.yHotspot, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
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
}

bool IntervalCapture::Setup(HWND owner) {
    if (running_) { Stop(); return false; }
    using F = ui::Field;
    std::vector<F> f = {
        F::MakeCombo(Tr(L"Área"), { Tr(L"Tela inteira"), Tr(L"Monitor do cursor"), Tr(L"Região (escolher agora)"), Tr(L"Janela (escolher agora)") },
                     (int)RegGetDword(L"IntervalArea", 0)),
        F::MakeInt(Tr(L"Intervalo (segundos)"), (int)RegGetDword(L"IntervalSeconds", 10), 1, 86400),
        F::MakeInt(Tr(L"Duração (minutos, 0 = até parar)"), (int)RegGetDword(L"IntervalMinutes", 0), 0, 10080),
        F::MakeCombo(Tr(L"Formato"), { L"PNG", L"JPEG", L"WebP" }, (int)std::min<DWORD>(RegGetDword(L"IntervalFormat", 0), 2)),
        F::MakeText(Tr(L"Prefixo do nome"), RegGetString(L"IntervalPrefix", Tr(L"Intervalo"))),
        F::MakeText(Tr(L"Pasta"), RegGetString(L"IntervalFolder", DefaultSaveDir() + L"\\" + Tr(L"Intervalo"))),
        F::MakeCheck(Tr(L"Capturar o cursor"), RegGetDword(L"IntervalCursor", 0) != 0),
        F::MakeLabel(Tr(L"Um ponto vermelho no ícone da bandeja indica que está capturando; pare pelo menu da bandeja.")),
    };
    auto validate = [](std::vector<F>& f) -> std::wstring {
        if (f[4].text.empty() || f[4].text.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return Tr(L"O prefixo contém caracteres inválidos.");
        if (Trim(f[5].text).empty()) return Tr(L"Escolha uma pasta.");
        return L"";
    };
    if (!ui::RunForm(owner, Tr(L"Captura por intervalo"), f, validate)) return false;
    area_ = f[0].value; seconds_ = f[1].value; minutes_ = f[2].value; format_ = f[3].value;
    prefix_ = f[4].text; folder_ = Trim(f[5].text); cursor_ = f[6].value != 0;
    RegSetDword(L"IntervalArea", area_); RegSetDword(L"IntervalSeconds", seconds_); RegSetDword(L"IntervalMinutes", minutes_);
    RegSetDword(L"IntervalFormat", format_); RegSetString(L"IntervalPrefix", prefix_); RegSetString(L"IntervalFolder", folder_);
    RegSetDword(L"IntervalCursor", cursor_);
    if (area_ == 2 || area_ == 3) {
        // the region / window is chosen with the crosshair; the capture starts when it is chosen
        RECT vs = VirtualScreenRect();
        HDC screen = GetDC(nullptr);
        Image frozen = Image::FromDC(screen, vs);
        ReleaseDC(nullptr, screen);
        RunSelectOverlay(frozen, vs, area_ == 2 ? SelectMode::Region : SelectMode::Window, { 640, 480 }, false, [this](const SelectResult& r) {
            if (!r.ok) return;
            region_ = r.rect; window_ = area_ == 3 ? r.window : nullptr;
            Begin();
        });
        return true;
    }
    Begin();
    return true;
}

void IntervalCapture::Start() {
    area_ = (int)std::min<DWORD>(RegGetDword(L"IntervalArea", 0), 1);    // without a dialog: screen or monitor only
    seconds_ = (int)std::clamp<DWORD>(RegGetDword(L"IntervalSeconds", 10), 1, 86400);
    minutes_ = (int)RegGetDword(L"IntervalMinutes", 0);
    format_ = (int)std::min<DWORD>(RegGetDword(L"IntervalFormat", 0), 2);
    prefix_ = RegGetString(L"IntervalPrefix", Tr(L"Intervalo"));
    folder_ = RegGetString(L"IntervalFolder", DefaultSaveDir() + L"\\" + Tr(L"Intervalo"));
    cursor_ = RegGetDword(L"IntervalCursor", 0) != 0;
    Begin();
}

void IntervalCapture::Begin() {
    if (running_) return;
    if (!EnsureDir(folder_)) { if (notify) notify(L"Snapnab", Tr(L"Não foi possível criar a pasta: ") + folder_); return; }
    if (!wnd_) {
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SnapnabInterval";
        RegisterClassW(&wc);
        wnd_ = CreateWindowExW(0, L"SnapnabInterval", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(wnd_, GWLP_USERDATA, (LONG_PTR)this);
    }
    running_ = true;
    count_ = 0;
    endAt_ = minutes_ > 0 ? GetTickCount64() + (ULONGLONG)minutes_ * 60000 : 0;
    Log(L"Interval capture: every %d s, area %d, %d min, %s", seconds_, area_, minutes_, folder_.c_str());
    SetTimer(wnd_, 1, 250, nullptr);              // first capture right away (250 ms, as the reference)
    if (onStatus) onStatus(true, 0);
}

void IntervalCapture::Stop() {
    if (!running_) return;
    running_ = false;
    if (wnd_) KillTimer(wnd_, 1);
    Log(L"Interval capture: stopped after %d capture(s)", count_);
    if (onStatus) onStatus(false, count_);
    if (notify) notify(L"Snapnab", Format(Tr(L"Captura por intervalo parada: %d imagem(ns) em %s"), count_, folder_.c_str()));
}

void IntervalCapture::Tick() {
    if (!running_) return;
    if (endAt_ && GetTickCount64() >= endAt_) { Stop(); return; }
    SetTimer(wnd_, 1, (UINT)seconds_ * 1000, nullptr);
    RECT r{};
    switch (area_) {
    case 1: { POINT c; GetCursorPos(&c); r = MonitorRectFromPoint(c); break; }
    case 2: r = region_; break;
    case 3:
        if (!window_ || !IsWindow(window_)) { Stop(); return; }    // the window was closed
        if (IsIconic(window_)) return;                              // minimized: skips this time
        r = WindowFrameRect(window_);
        break;
    default: r = VirtualScreenRect();
    }
    RECT vs = VirtualScreenRect();
    IntersectRect(&r, &r, &vs);
    Image img = Grab(r, cursor_);
    if (img.empty()) return;
    std::wstring path = NextAutoName(folder_, prefix_, kExt[std::clamp(format_, 0, 2)], 4);
    if (SaveImageFile(path, img)) count_++;
    else Log(L"Interval capture: failed to save %s", path.c_str());
    if (onStatus) onStatus(true, count_);
}

LRESULT CALLBACK IntervalCapture::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = (IntervalCapture*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_TIMER && self) { self->Tick(); return 0; }
    return DefWindowProcW(h, m, w, l);
}

} // namespace snap
