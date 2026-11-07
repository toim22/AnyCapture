//! The small modal forms built in code (Ui.cpp, RunForm), with the same window class and control IDs (field i is
//! control 1000 + i, OK is 1 and Cancel 2), which the e2e suite fills by ID. Only the field kinds the Rust app asks
//! for so far are here. A form runs its own message loop: call it from its own thread.

use std::cell::Cell;
use std::sync::Once;

use snapcore::screen;
use snapcore::wide::to_wide;
use snapnab_app::tr;
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{
    CLEARTYPE_QUALITY, CLIP_DEFAULT_PRECIS, CreateFontW, DEFAULT_CHARSET, DeleteObject, FF_DONTCARE, FW_NORMAL, GetSysColorBrush,
    HFONT, OUT_DEFAULT_PRECIS, COLOR_WINDOW,
};
use windows::Win32::UI::Controls::{UDM_SETBUDDY, UDM_SETPOS32, UDM_SETRANGE32, UDS_ALIGNRIGHT, UDS_ARROWKEYS, UDS_NOTHOUSANDS,
                                   UDS_SETBUDDYINT, UPDOWN_CLASSW};
use windows::Win32::UI::HiDpi::{AdjustWindowRectExForDpi, GetDpiForSystem, GetDpiForWindow};
use windows::Win32::UI::Input::KeyboardAndMouse::{EnableWindow, SetFocus, VK_ESCAPE};
use windows::Win32::UI::WindowsAndMessaging::{
    BS_DEFPUSHBUTTON, CreateWindowExW, DefWindowProcW, DestroyWindow, DispatchMessageW, ES_AUTOHSCROLL, ES_NUMBER, GetMessageW,
    GetWindowRect, GetWindowTextW, HMENU, IDC_ARROW, IsChild, IsDialogMessageW, LoadCursorW, MSG, RegisterClassW,
    SW_SHOW, SendMessageW, SetForegroundWindow, ShowWindow, TranslateMessage, WINDOW_STYLE, WM_CLOSE, WM_COMMAND,
    WM_KEYDOWN, WM_SETFONT, WNDCLASSW, WS_CAPTION, WS_CHILD, WS_CLIPCHILDREN, WS_EX_CLIENTEDGE, WS_EX_DLGMODALFRAME,
    WS_EX_TOPMOST, WS_POPUP, WS_SYSMENU, WS_TABSTOP, WS_VISIBLE,
};
use windows::core::{PCWSTR, w};

pub enum Field {
    Label(String),
    Int { label: String, value: i32, min: i32, max: i32 },
}

const ID_FIELD: i32 = 1000;
// static control styles (SS_LEFT, SS_CENTERIMAGE)
const SS_LEFT: u32 = 0;
const SS_CENTERIMAGE: u32 = 0x200;
const ID_OK: usize = 1;
const ID_CANCEL: usize = 2;

thread_local! {
    /// the answer of the form running on this thread (None: still open)
    static DONE: Cell<Option<bool>> = const { Cell::new(None) };
}

unsafe extern "system" fn form_proc(h: HWND, m: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    match m {
        WM_COMMAND if wp.0 & 0xFFFF == ID_OK || wp.0 & 0xFFFF == ID_CANCEL => {
            DONE.with(|d| d.set(Some(wp.0 & 0xFFFF == ID_OK)));
            LRESULT(0)
        }
        WM_CLOSE => {
            DONE.with(|d| d.set(Some(false)));
            LRESULT(0)
        }
        _ => unsafe { DefWindowProcW(h, m, wp, lp) },
    }
}

fn scale(v: i32, dpi: u32) -> i32 {
    (v as i64 * dpi as i64 / 96) as i32
}

/// UiFont: Segoe UI 9 pt at the window's DPI.
fn ui_font(dpi: u32) -> HFONT {
    unsafe {
        CreateFontW(-(9 * dpi as i32 / 72), 0, 0, 0, FW_NORMAL.0 as i32, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE.0 as u32, w!("Segoe UI"))
    }
}

/// RunForm: true on OK, with the values written back into `fields`.
pub fn run(owner: isize, title: &str, fields: &mut [Field]) -> bool {
    static REG: Once = Once::new();
    REG.call_once(|| unsafe {
        let wc = WNDCLASSW {
            lpfnWndProc: Some(form_proc),
            hInstance: windows::Win32::System::LibraryLoader::GetModuleHandleW(None).unwrap_or_default().into(),
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap_or_default(),
            hbrBackground: GetSysColorBrush(COLOR_WINDOW),
            lpszClassName: w!("SnapnabForm"),
            ..Default::default()
        };
        RegisterClassW(&wc);
    });
    DONE.with(|d| d.set(None));
    let owner_h = (owner != 0).then(|| HWND(owner as _));
    let dpi = match owner_h {
        Some(o) => unsafe { GetDpiForWindow(o) },
        None => unsafe { GetDpiForSystem() },
    }
    .max(96);
    let (width, pad, row_h, ctl_h, label_w) = (scale(420, dpi), scale(14, dpi), scale(30, dpi), scale(24, dpi), scale(150, dpi));
    // long explanatory labels wrap over two rows
    let span: Vec<i32> = fields.iter().map(|f| if matches!(f, Field::Label(t) if t.chars().count() > 58) { 2 } else { 1 }).collect();
    let rows: i32 = span.iter().sum();
    let client_h = pad * 2 + rows * row_h + scale(46, dpi);
    let style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    let mut wr = RECT { left: 0, top: 0, right: width, bottom: client_h };
    let mut done = false;
    unsafe {
        let _ = AdjustWindowRectExForDpi(&mut wr, style, false, WS_EX_DLGMODALFRAME, dpi);
        let area = match owner_h {
            Some(o) => {
                let mut r = RECT::default();
                let _ = GetWindowRect(o, &mut r);
                screen::to_rect(r)
            }
            None => {
                let (cx, cy) = screen::cursor_pos();
                screen::monitor_rect(cx, cy, true)
            }
        };
        let (ww, wh) = (wr.right - wr.left, wr.bottom - wr.top);
        let (x, y) = ((area.left + area.right - ww) / 2, (area.top + area.bottom - wh) / 2);
        let t = to_wide(title);
        let Ok(h) = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, w!("SnapnabForm"), PCWSTR(t.as_ptr()), style, x, y, ww, wh,
                                    owner_h, None, None, None) else { return false };
        let font = ui_font(dpi);
        let child = |ex, class: PCWSTR, text: &str, st: u32, x, y, w, hh, id: i32| {
            let t = to_wide(text);
            let c = CreateWindowExW(ex, class, PCWSTR(t.as_ptr()), WS_CHILD | WS_VISIBLE | WINDOW_STYLE(st), x, y, w, hh, Some(h),
                                    (id != 0).then(|| HMENU(id as isize as _)), None, None)
                .unwrap_or_default();
            SendMessageW(c, WM_SETFONT, Some(WPARAM(font.0 as usize)), Some(LPARAM(1)));
            c
        };
        let mut yy = pad;
        let mut first = None;
        for (i, f) in fields.iter().enumerate() {
            let id = ID_FIELD + i as i32;
            let (cx, sp) = (pad + label_w, span[i]);
            match f {
                Field::Label(text) => {
                    let st = SS_LEFT | if sp > 1 { 0 } else { SS_CENTERIMAGE };
                    child(Default::default(), w!("STATIC"), text, st, pad, yy + if sp > 1 { scale(3, dpi) } else { 0 }, width - 2 * pad,
                          ctl_h + (sp - 1) * row_h, id);
                }
                Field::Int { label, value, min, max } => {
                    child(Default::default(), w!("STATIC"), label, SS_LEFT | SS_CENTERIMAGE, pad, yy, label_w - scale(8, dpi),
                          ctl_h, 0);
                    let st = WS_TABSTOP.0 | ES_AUTOHSCROLL as u32 | if *min >= 0 { ES_NUMBER as u32 } else { 0 };
                    let e = child(WS_EX_CLIENTEDGE, w!("EDIT"), &value.to_string(), st, cx, yy, scale(90, dpi), ctl_h, id);
                    let ud = child(Default::default(), UPDOWN_CLASSW,
                                   "", (UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS) as u32, 0, 0, 0, 0, 0);
                    SendMessageW(ud, UDM_SETBUDDY, Some(WPARAM(e.0 as usize)), None);
                    SendMessageW(ud, UDM_SETRANGE32, Some(WPARAM(*min as isize as usize)), Some(LPARAM(*max as isize)));
                    SendMessageW(ud, UDM_SETPOS32, None, Some(LPARAM(*value as isize)));
                    first.get_or_insert(e);
                }
            }
            yy += row_h * sp;
        }
        let (bw, bh) = (scale(96, dpi), scale(28, dpi));
        yy = pad + rows * row_h + scale(6, dpi);
        let ok = child(Default::default(), w!("BUTTON"), &tr("OK"), WS_TABSTOP.0 | BS_DEFPUSHBUTTON as u32,
                       width - pad - bw * 2 - scale(8, dpi), yy, bw, bh, ID_OK as i32);
        child(Default::default(), w!("BUTTON"), &tr("Cancelar"), WS_TABSTOP.0, width - pad - bw, yy, bw, bh, ID_CANCEL as i32);

        if let Some(o) = owner_h {
            let _ = EnableWindow(o, false);
        }
        let _ = ShowWindow(h, SW_SHOW);
        let _ = SetForegroundWindow(h);
        let _ = SetFocus(Some(first.unwrap_or(ok)));
        let mut msg = MSG::default();
        while DONE.with(|d| d.get()).is_none() && GetMessageW(&mut msg, None, 0, 0).as_bool() {
            if msg.message == WM_KEYDOWN && msg.wParam.0 == VK_ESCAPE.0 as usize && (msg.hwnd == h || IsChild(h, msg.hwnd).as_bool()) {
                DONE.with(|d| d.set(Some(false)));
                continue;
            }
            if !IsDialogMessageW(h, &msg).as_bool() {
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if DONE.with(|d| d.get()) == Some(true) {
            done = true;
            for (i, f) in fields.iter_mut().enumerate() {
                if let Field::Int { value, min, max, .. } = f {
                    let e = windows::Win32::UI::WindowsAndMessaging::GetDlgItem(Some(h), ID_FIELD + i as i32).unwrap_or_default();
                    let mut buf = [0u16; 32];
                    let n = GetWindowTextW(e, &mut buf) as usize;
                    let s: String = String::from_utf16_lossy(&buf[..n]).chars().filter(|c| c.is_ascii_digit() || *c == '-').collect();
                    *value = s.parse::<i32>().unwrap_or(*value).clamp(*min, *max);
                }
            }
        }
        if let Some(o) = owner_h {
            let _ = EnableWindow(o, true);
            let _ = SetForegroundWindow(o);
        }
        let _ = DestroyWindow(h);
        let _ = DeleteObject(font.into());
    }
    done
}
