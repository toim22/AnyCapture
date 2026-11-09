//! The title bar of the windows with menus (the capture window and the Editor), as the C++ CaptionBar drew it: the
//! program's mark and the menus on the same strip as the minimize, maximize and close buttons, and the document's
//! name after the menus.
//!
//! How it works: the window keeps its normal frame (resize borders, Snap, the minimize and maximize animations, the
//! shadow), but WM_NCCALCSIZE hands the title strip to the client area, where egui draws it ([`bar`]). WM_NCHITTEST
//! tells Windows which parts of the strip are the caption (drag, double-click to maximize, right-click for the system
//! menu) and which are buttons that egui handles. A menu opens as a native popup (`menu.rs`) below its name; while it
//! is open, pointing at another name or pressing ← / → switches menus, as in a menu bar. Alt+letter and F10 open
//! them from the keyboard.

use std::collections::HashMap;
use std::sync::Mutex;
use std::time::{Duration, Instant};

use egui::{Align2, Color32, CornerRadius, FontId, Rect, Sense, Stroke, pos2, vec2};
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, POINT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{ClientToScreen, InvalidateRect, ScreenToClient};
use windows::Win32::UI::Input::KeyboardAndMouse::{GetKeyState, VK_CONTROL, VK_F10, VK_LEFT, VK_RIGHT};
use windows::Win32::UI::Shell::{DefSubclassProc, RemoveWindowSubclass, SetWindowSubclass};
use windows::Win32::UI::WindowsAndMessaging::*;

use crate::menu::Menu;
use crate::theme::tokens;

/// The strip's height in points.
pub const HEIGHT: f32 = 32.0;
/// Posted by a click on a menu's name: opens menu `wparam` (outside egui's frame, so that frame is on screen).
const WM_OPEN_MENU: u32 = WM_APP + 0x3A0;
const HIT_PAD: f32 = 4.0;

struct Cap {
    /// name, access key (uppercase), HMENU
    menus: Vec<(String, u16, isize)>,
    /// where each name is, in client pixels
    rects: Vec<RECT>,
    /// the other parts of the strip that are buttons, in client pixels
    buttons: Vec<RECT>,
    /// the strip's height in pixels
    height: i32,
    resizable: bool,
    open: Option<usize>,
    next: Option<usize>,
    /// the menu that closed a moment ago: the click that closed it must not open it again
    closed: Option<(usize, Instant)>,
    /// while a menu is open: whether the highlighted item opens a submenu, and the menu it is in
    sel_popup: bool,
    sel_menu: isize,
}

static CAPS: Mutex<Option<HashMap<isize, Cap>>> = Mutex::new(None);

fn with<R>(hwnd: isize, f: impl FnOnce(&mut Cap) -> R) -> Option<R> {
    let mut g = CAPS.lock().unwrap_or_else(|e| e.into_inner());
    g.get_or_insert_with(HashMap::new).get_mut(&hwnd).map(f)
}

fn dpi_of(hwnd: isize) -> i32 {
    (unsafe { windows::Win32::UI::HiDpi::GetDpiForWindow(HWND(hwnd as _)) } as i32).max(96)
}

fn scale(v: i32, dpi: i32) -> i32 {
    (v * dpi + 48) / 96
}

/// Gives a window the AnyCapture title bar (called by the host as soon as the window exists).
pub fn install(hwnd: isize, resizable: bool) {
    {
        let mut g = CAPS.lock().unwrap_or_else(|e| e.into_inner());
        g.get_or_insert_with(HashMap::new).insert(hwnd, Cap {
            menus: Vec::new(),
            rects: Vec::new(),
            buttons: Vec::new(),
            height: scale(HEIGHT as i32, dpi_of(hwnd)),
            resizable,
            open: None,
            next: None,
            closed: None,
            sel_popup: false,
            sel_menu: 0,
        });
    }
    let h = HWND(hwnd as _);
    unsafe {
        let _ = SetWindowSubclass(h, Some(caption_proc), 3, 0);
        // the frame is measured again, without the title strip
        let _ = SetWindowPos(h, None, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

/// The access key of a menu's name: the letter after `&`, else its first letter.
fn key_of(name: &str) -> u16 {
    let c = name.split_once('&').and_then(|(_, r)| r.chars().next()).or_else(|| name.chars().next()).unwrap_or(' ');
    c.to_uppercase().next().unwrap_or(c) as u16
}

/// The window's menus (`&` marks the access key in a name); they live as long as the window.
pub fn set_menus(hwnd: isize, menus: Vec<(String, Menu)>) {
    let mut list: Vec<(String, u16, isize)> = Vec::new();
    for (name, m) in menus {
        let mut k = key_of(&name);
        // translated names may share an initial: the next menu takes another letter of its own name
        if list.iter().any(|(_, used, _)| *used == k) {
            k = name
                .chars()
                .filter(|c| c.is_alphabetic())
                .map(|c| c.to_uppercase().next().unwrap_or(c) as u16)
                .find(|c| !list.iter().any(|(_, used, _)| used == c))
                .unwrap_or(0);
        }
        list.push((name, k, m.into_raw().0 as isize));
    }
    let mut g = CAPS.lock().unwrap_or_else(|e| e.into_inner());
    let map = g.get_or_insert_with(HashMap::new);
    let cap = map.entry(hwnd).or_insert_with(|| Cap {
        menus: Vec::new(),
        rects: Vec::new(),
        buttons: Vec::new(),
        height: scale(HEIGHT as i32, 96),
        resizable: false,
        open: None,
        next: None,
        closed: None,
        sel_popup: false,
        sel_menu: 0,
    });
    cap.menus = list;
}

/// The names shown on the strip (without the `&`).
fn shown(name: &str) -> String {
    name.replacen('&', "", 1)
}

/// The program a title bar belongs to, for the mark drawn at its left.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mark {
    Capture,
    Editor,
}

/// The program's mark in one color, as the logo's outline: the four frame corners, with the capture area inside
/// (AnyCapture) or the pen nib (the Editor). The colored logo stays on the taskbar, Start and the installer; inside the
/// windows the mark takes the text color like the other icons. `r` is a square; `bg` is the strip's color.
fn mark(p: &egui::Painter, which: Mark, r: Rect, c: Color32, bg: Color32) {
    let w = r.width();
    let b = Rect::from_center_size(r.center(), vec2(w, w * 0.8)).shrink(0.75);
    // the logo's proportions: the corners leave the middle of each side open
    let (arm_x, arm_y, rad) = (b.width() * 0.25, b.height() * 0.27, w * 0.09);
    let stroke = Stroke::new(1.5, c);
    for (corner, dx, dy) in [(b.left_top(), 1.0, 1.0), (b.right_top(), -1.0, 1.0), (b.left_bottom(), 1.0, -1.0), (b.right_bottom(), -1.0, -1.0)] {
        // from the end of the upright arm, round the corner, to the end of the level one
        let center = pos2(corner.x + dx * rad, corner.y + dy * rad);
        let mut pts = vec![pos2(corner.x, corner.y + dy * arm_y)];
        pts.extend((0..=6).map(|i| {
            let t = i as f32 / 6.0 * std::f32::consts::FRAC_PI_2;
            pos2(center.x - dx * rad * t.cos(), center.y - dy * rad * t.sin())
        }));
        pts.push(pos2(corner.x + dx * arm_x, corner.y));
        p.add(egui::Shape::line(pts, stroke));
    }
    if which == Mark::Editor {
        let (h, cx) = (b.height() * 0.66, b.center().x);
        let (top, bottom, half) = (b.center().y - h / 2.0, b.center().y + h / 2.0, h * 0.45);
        let nib = vec![
            pos2(cx, top),
            pos2(cx + half, top + h * 0.64),
            pos2(cx + half * 0.55, bottom),
            pos2(cx - half * 0.55, bottom),
            pos2(cx - half, top + h * 0.64),
        ];
        p.add(egui::Shape::convex_polygon(nib, c, Stroke::NONE));
        p.line_segment([pos2(cx, top + h * 0.3), pos2(cx, top + h * 0.62)], Stroke::new(1.0, bg));
    } else {
        let card = b.shrink2(vec2(w * 0.19, w * 0.16));
        p.rect_filled(card, CornerRadius::same(2), c.gamma_multiply(0.35));
    }
}

/// Draws the title bar at the top of the window: call it first in the window's frame. `title` is shown dimmed after
/// the menus (the Editor's document); `hwnd` 0 (the tests) draws it without a window behind. `which` picks the mark.
pub fn bar(ui: &mut egui::Ui, hwnd: isize, title: &str, which: Mark) {
    let t = tokens();
    let frame = egui::Frame::NONE.fill(t.window);
    egui::Panel::top("snapui.caption").exact_size(HEIGHT).resizable(false).show_separator_line(false).frame(frame).show(ui, |ui| {
        strip(ui, hwnd, title, which);
    });
}

fn strip(ui: &mut egui::Ui, hwnd: isize, title: &str, which: Mark) {
    let t = tokens();
    let ctx = ui.ctx().clone();
    let ppp = ctx.pixels_per_point();
    let full = ui.max_rect();
    let focused = ctx.input(|i| i.viewport().focused).unwrap_or(true);
    let fg = if focused { t.text } else { t.text_dim };
    let zoomed = hwnd != 0 && unsafe { IsZoomed(HWND(hwnd as _)) }.as_bool();
    let (menus, open, resizable) = with(hwnd, |c| (c.menus.iter().map(|m| m.0.clone()).collect::<Vec<_>>(), c.open, c.resizable))
        .unwrap_or_default();
    let to_px = |r: Rect| RECT {
        left: (r.left() * ppp).floor() as i32,
        top: (r.top() * ppp).floor() as i32,
        right: (r.right() * ppp).ceil() as i32,
        bottom: (r.bottom() * ppp).ceil() as i32,
    };
    // the program's mark
    let isz = 16.0;
    mark(ui.painter(), which, Rect::from_center_size(pos2(full.left() + 12.0 + isz / 2.0, full.center().y), vec2(isz, isz)), fg, t.window);
    // menus
    let font = FontId::proportional(t.body);
    let mut x = full.left() + 38.0;
    let mut rects = Vec::new();
    for (i, name) in menus.iter().enumerate() {
        let label = shown(name);
        let w = ui.painter().layout_no_wrap(label.clone(), font.clone(), fg).size().x + 20.0;
        let r = Rect::from_min_size(pos2(x, full.top() + HIT_PAD), vec2(w, full.height() - 2.0 * HIT_PAD));
        let resp = ui.interact(r, ui.scope_id().with(("caption-menu", i)), Sense::click());
        crate::widgets::set_aid(ui, &resp, &format!("AnyCaptureMenu.{i}"));
        resp.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, &label));
        let lit = open == Some(i) || (open.is_none() && resp.hovered());
        if lit {
            ui.painter().rect_filled(r, CornerRadius::same(t.radius), t.hover_strong());
        }
        ui.painter().text(r.center(), Align2::CENTER_CENTER, &label, font.clone(), fg);
        if resp.clicked() && hwnd != 0 {
            let just = with(hwnd, |c| c.closed.is_some_and(|(k, at)| k == i && at.elapsed() < Duration::from_millis(250))).unwrap_or(false);
            if !just {
                unsafe {
                    let _ = PostMessageW(Some(HWND(hwnd as _)), WM_OPEN_MENU, WPARAM(i), LPARAM(0));
                }
            }
        }
        rects.push(to_px(Rect::from_min_max(pos2(r.left(), full.top()), pos2(r.right(), full.bottom()))));
        x += w + 2.0;
    }
    // window buttons: close, maximize/restore, minimize (from the right)
    let bw = 46.0;
    let mut buttons = Vec::new();
    let mut right = full.right();
    let kinds: &[u8] = if resizable || hwnd == 0 { &[2, 1, 0] } else { &[2, 0] };
    for &k in kinds {
        let r = Rect::from_min_max(pos2(right - bw, full.top()), pos2(right, full.bottom()));
        right -= bw;
        let resp = ui.interact(r, ui.scope_id().with(("caption-btn", k)), Sense::click());
        let name = match k {
            0 => "Minimize",
            1 if zoomed => "Restore",
            1 => "Maximize",
            _ => "Close",
        };
        crate::widgets::set_aid(ui, &resp, &format!("AnyCaptureCaption.{name}"));
        resp.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, name));
        let close = k == 2;
        let mut glyph = fg;
        if resp.hovered() {
            let fill = if close {
                if resp.is_pointer_button_down_on() { Color32::from_rgb(148, 20, 27) } else { Color32::from_rgb(196, 43, 28) }
            } else if resp.is_pointer_button_down_on() {
                t.pressed_strong()
            } else {
                t.hover_strong()
            };
            ui.painter().rect_filled(r, CornerRadius::ZERO, fill);
            if close {
                glyph = Color32::WHITE;
            }
        }
        let p = ui.painter();
        let c = r.center();
        let s = 5.0;
        let st = Stroke::new(1.0, glyph);
        match k {
            0 => {
                p.line_segment([pos2(c.x - s, c.y), pos2(c.x + s, c.y)], st);
            }
            1 if zoomed => {
                let o = 2.0;
                p.rect_stroke(Rect::from_min_max(pos2(c.x - s, c.y - s + o), pos2(c.x + s - o, c.y + s)), CornerRadius::same(1), st,
                              egui::StrokeKind::Middle);
                p.line_segment([pos2(c.x - s + o, c.y - s), pos2(c.x + s, c.y - s)], st);
                p.line_segment([pos2(c.x + s, c.y - s), pos2(c.x + s, c.y + s - o)], st);
            }
            1 => {
                p.rect_stroke(Rect::from_center_size(c, vec2(2.0 * s, 2.0 * s)), CornerRadius::same(1), st, egui::StrokeKind::Middle);
            }
            _ => {
                p.line_segment([pos2(c.x - s, c.y - s), pos2(c.x + s, c.y + s)], st);
                p.line_segment([pos2(c.x + s, c.y - s), pos2(c.x - s, c.y + s)], st);
            }
        }
        if resp.clicked() && hwnd != 0 {
            let h = Some(HWND(hwnd as _));
            unsafe {
                let _ = match k {
                    0 => PostMessageW(h, WM_SYSCOMMAND, WPARAM(SC_MINIMIZE as usize), LPARAM(0)),
                    1 => PostMessageW(h, WM_SYSCOMMAND, WPARAM(if zoomed { SC_RESTORE } else { SC_MAXIMIZE } as usize), LPARAM(0)),
                    _ => PostMessageW(h, WM_CLOSE, WPARAM(0), LPARAM(0)),
                };
            }
        }
        buttons.push(to_px(r));
    }
    // the document's name, dimmed, between the menus and the buttons
    if !title.is_empty() {
        let room = Rect::from_min_max(pos2(x + 16.0, full.top()), pos2(right - 12.0, full.bottom()));
        if room.width() > 40.0 {
            let mut job = egui::text::LayoutJob::simple_singleline(title.to_owned(), font.clone(), t.text_dim);
            job.wrap = egui::text::TextWrapping::truncate_at_width(room.width());
            let g = ui.painter().layout_job(job);
            ui.painter().galley(pos2(room.left(), room.center().y - g.size().y / 2.0), g, t.text_dim);
        }
    }
    if hwnd != 0 {
        let height = (full.bottom() * ppp).round() as i32;
        with(hwnd, |c| {
            c.rects = rects;
            c.buttons = buttons;
            c.height = height;
        });
    }
}

/// Which menu name is at a client point.
fn menu_at(c: &Cap, p: POINT) -> Option<usize> {
    c.rects.iter().position(|r| p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom)
}

static ACTIVE: Mutex<isize> = Mutex::new(0);
static HOOK: Mutex<isize> = Mutex::new(0);

/// While a menu is open: moving to another name, or ← / → at the edge of the menus, switches to the neighbor.
unsafe extern "system" fn menu_filter(code: i32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    let hwnd = *ACTIVE.lock().unwrap_or_else(|e| e.into_inner());
    if code == MSGF_MENU as i32 && hwnd != 0 {
        let m = unsafe { &*(lp.0 as *const MSG) };
        if m.message == WM_MOUSEMOVE {
            let mut p = m.pt;
            unsafe {
                let _ = ScreenToClient(HWND(hwnd as _), &mut p);
            }
            let switch = with(hwnd, |c| match menu_at(c, p) {
                Some(k) if Some(k) != c.open => {
                    c.next = Some(k);
                    true
                }
                _ => false,
            });
            if switch == Some(true) {
                unsafe {
                    let _ = EndMenu();
                }
            }
        } else if m.message == WM_KEYDOWN && (m.wParam.0 == VK_LEFT.0 as usize || m.wParam.0 == VK_RIGHT.0 as usize) {
            let right = m.wParam.0 == VK_RIGHT.0 as usize;
            let switch = with(hwnd, |c| {
                let n = c.menus.len();
                let Some(open) = c.open else { return false };
                let top = c.menus.get(open).map_or(0, |m| m.2);
                // → opens a submenu and ← closes one, as usual; only at the top level they switch menus
                if (right && c.sel_popup) || (!right && c.sel_menu != 0 && c.sel_menu != top) || n < 2 {
                    return false;
                }
                c.next = Some(if right { (open + 1) % n } else { (open + n - 1) % n });
                true
            });
            if switch == Some(true) {
                unsafe {
                    let _ = EndMenu();
                }
            }
        }
    }
    let h = *HOOK.lock().unwrap_or_else(|e| e.into_inner());
    unsafe { CallNextHookEx(Some(HHOOK(h as _)), code, wp, lp) }
}

/// Opens menu `i` below its name, and the next ones while the user switches.
fn open_menu(hwnd: isize, mut i: usize) {
    let h = HWND(hwnd as _);
    loop {
        let Some((menu, r, dpi)) = with(hwnd, |c| {
            c.open = Some(i);
            c.next = None;
            c.sel_popup = false;
            c.sel_menu = 0;
            Some((c.menus.get(i)?.2, *c.rects.get(i)?, 0))
        })
        .flatten() else {
            break;
        };
        let _ = dpi;
        unsafe {
            let _ = InvalidateRect(Some(h), None, false);
            let mut tl = POINT { x: r.left, y: r.top };
            let mut br = POINT { x: r.right, y: r.bottom };
            let _ = ClientToScreen(h, &mut tl);
            let _ = ClientToScreen(h, &mut br);
            let exclude = RECT { left: tl.x, top: tl.y, right: br.x, bottom: br.y };
            let tp = TPMPARAMS { cbSize: size_of::<TPMPARAMS>() as u32, rcExclude: exclude };
            *ACTIVE.lock().unwrap_or_else(|e| e.into_inner()) = hwnd;
            let hook = SetWindowsHookExW(WH_MSGFILTER, Some(menu_filter), None, windows::Win32::System::Threading::GetCurrentThreadId());
            *HOOK.lock().unwrap_or_else(|e| e.into_inner()) = hook.as_ref().map_or(0, |k| k.0 as isize);
            let _ = TrackPopupMenuEx(HMENU(menu as _), (TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL | TPM_NOANIMATION).0, tl.x, br.y - scale(2, dpi_of(hwnd)), h,
                                     Some(&tp));
            if let Ok(k) = hook {
                let _ = UnhookWindowsHookEx(k);
            }
            *ACTIVE.lock().unwrap_or_else(|e| e.into_inner()) = 0;
        }
        match with(hwnd, |c| c.next.take()).flatten() {
            Some(n) => i = n,
            None => break,
        }
    }
    with(hwnd, |c| {
        if let Some(k) = c.open.take() {
            c.closed = Some((k, Instant::now()));
        }
    });
    unsafe {
        let _ = InvalidateRect(Some(h), None, false);
    }
}

unsafe extern "system" fn caption_proc(hwnd: HWND, msg: u32, wp: WPARAM, lp: LPARAM, _id: usize, _data: usize) -> LRESULT {
    let key = hwnd.0 as isize;
    unsafe {
        match msg {
            WM_NCCALCSIZE if wp.0 != 0 => {
                let resizable = with(key, |c| c.resizable).unwrap_or(false);
                if resizable {
                    // keeps the resize borders (invisible on Windows 11) and drops only the title strip; maximized, the
                    // window overflows the screen by its frame, so the top moves down by as much
                    let p = &mut *(lp.0 as *mut NCCALCSIZE_PARAMS);
                    let top = p.rgrc[0].top;
                    DefSubclassProc(hwnd, msg, wp, lp);
                    p.rgrc[0].top = top;
                    if IsZoomed(hwnd).as_bool() {
                        let dpi = dpi_of(key) as u32;
                        p.rgrc[0].top += windows::Win32::UI::HiDpi::GetSystemMetricsForDpi(SM_CYFRAME, dpi)
                            + windows::Win32::UI::HiDpi::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    }
                }
                LRESULT(0)
            }
            WM_NCHITTEST => {
                let Some((height, resizable, hot)) = with(key, |c| {
                    let mut hot = c.rects.clone();
                    hot.extend(c.buttons.iter().copied());
                    (c.height, c.resizable, hot)
                }) else {
                    return DefSubclassProc(hwnd, msg, wp, lp);
                };
                let zoomed = IsZoomed(hwnd).as_bool();
                if resizable && !zoomed {
                    let d = DefSubclassProc(hwnd, msg, wp, lp);
                    if !matches!(d.0 as u32, HTCLIENT | HTCAPTION | HTNOWHERE) {
                        return d; // the side and bottom borders
                    }
                }
                let mut p = POINT { x: (lp.0 & 0xFFFF) as i16 as i32, y: ((lp.0 >> 16) & 0xFFFF) as i16 as i32 };
                let _ = ScreenToClient(hwnd, &mut p);
                if p.y < 0 || p.y >= height {
                    return LRESULT(HTCLIENT as isize);
                }
                let dpi = dpi_of(key);
                if resizable && !zoomed && p.y < scale(5, dpi) {
                    // the top border (the title strip took its place)
                    let mut cr = RECT::default();
                    let _ = GetClientRect(hwnd, &mut cr);
                    let corner = scale(12, dpi);
                    return LRESULT(if p.x < corner { HTTOPLEFT } else if p.x >= cr.right - corner { HTTOPRIGHT } else { HTTOP } as isize);
                }
                let on_button = hot.iter().any(|r| p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom);
                LRESULT(if on_button { HTCLIENT } else { HTCAPTION } as isize)
            }
            WM_NCRBUTTONUP if wp.0 == HTCAPTION as usize => {
                // the system menu (Restore, Move, Size, Minimize, Maximize, Close)
                let sys = GetSystemMenu(hwnd, false);
                let x = (lp.0 & 0xFFFF) as i16 as i32;
                let y = ((lp.0 >> 16) & 0xFFFF) as i16 as i32;
                let cmd = TrackPopupMenu(sys, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, None, hwnd, None).0;
                if cmd != 0 {
                    let _ = PostMessageW(Some(hwnd), WM_SYSCOMMAND, WPARAM(cmd as usize), LPARAM(0));
                }
                LRESULT(0)
            }
            WM_OPEN_MENU => {
                open_menu(key, wp.0);
                LRESULT(0)
            }
            WM_MENUSELECT => {
                let flags = (wp.0 >> 16) as u32 & 0xFFFF;
                with(key, |c| {
                    c.sel_popup = flags & MF_POPUP.0 != 0;
                    c.sel_menu = lp.0;
                });
                DefSubclassProc(hwnd, msg, wp, lp)
            }
            WM_SYSKEYDOWN => {
                let ctrl = GetKeyState(VK_CONTROL.0 as i32) < 0;
                let vk = wp.0 as u16;
                let target = if vk == VK_F10.0 && !ctrl {
                    with(key, |c| (!c.menus.is_empty()).then_some(0)).flatten()
                } else if lp.0 & (1 << 29) != 0 && !ctrl {
                    with(key, |c| c.menus.iter().position(|m| m.1 == vk && vk != 0)).flatten()
                } else {
                    None
                };
                match target {
                    Some(i) => {
                        let _ = PostMessageW(Some(hwnd), WM_OPEN_MENU, WPARAM(i), LPARAM(0));
                        LRESULT(0)
                    }
                    None => DefSubclassProc(hwnd, msg, wp, lp),
                }
            }
            // Alt alone or Alt+letter would put the window in the system menu's keyboard mode (no menu bar to show):
            // only Alt+Space keeps opening the system menu
            WM_SYSCOMMAND if (wp.0 & 0xFFF0) as u32 == SC_KEYMENU && lp.0 != ' ' as isize => LRESULT(0),
            WM_NCDESTROY => {
                let _ = RemoveWindowSubclass(hwnd, Some(caption_proc), 3);
                CAPS.lock().unwrap_or_else(|e| e.into_inner()).get_or_insert_with(HashMap::new).remove(&key);
                DefSubclassProc(hwnd, msg, wp, lp)
            }
            _ => DefSubclassProc(hwnd, msg, wp, lp),
        }
    }
}
