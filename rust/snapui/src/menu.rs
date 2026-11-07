//! The menus of both programs: the title bar's menus, the toolbar's "More", the context menus. They are native Win32
//! popup menus, so Windows still does the hard parts (the keyboard, submenus, the screen edges, the modal loop, the
//! "#32768" windows UI Automation knows), but every item is owner-drawn: Snapnab paints it in the theme's colors and
//! font, with an icon column on the left, a rounded highlight, the shortcut on the right and its own submenu chevron,
//! as Windows 11's own menus look.
//!
//! How owner-drawn menus work: an item created with MFT_OWNERDRAW carries a pointer (its item data, here a boxed
//! `ItemData`); when the menu opens, Windows asks the menu's owner window for each item's size (WM_MEASUREITEM) and
//! then for its picture (WM_DRAWITEM). The owner is always a Snapnab window, whose theme subclass (`theme.rs`) passes
//! those messages here. Each item keeps its text too (MIIM_STRING), so screen readers and UI Automation read it.

use std::cell::RefCell;
use std::collections::HashMap;
use std::sync::{Mutex, OnceLock};

use egui::Color32;
use snapcore::icons::Icon;
use snapcore::image::Image;
use windows::Win32::Foundation::{HWND, LRESULT, RECT};
use windows::Win32::Graphics::Gdi::{
    BitBlt, CreateCompatibleDC, CreateFontW, CreateSolidBrush, DT_END_ELLIPSIS, DT_HIDEPREFIX, DT_LEFT, DT_NOPREFIX, DT_RIGHT,
    DT_SINGLELINE, DT_VCENTER, DeleteDC, DeleteObject, DrawTextW, ExcludeClipRect, GetDC, GetTextExtentPoint32W, HBRUSH, HDC, HFONT,
    HGDIOBJ, ReleaseDC, SRCCOPY, SelectObject, SetBkMode, SetTextColor, TRANSPARENT,
};
use windows::Win32::UI::Controls::{DRAWITEMSTRUCT, MEASUREITEMSTRUCT, ODS_CHECKED, ODS_DISABLED, ODS_GRAYED, ODS_NOACCEL, ODS_SELECTED,
                                   ODT_MENU};
use windows::Win32::UI::HiDpi::GetDpiForWindow;
use windows::Win32::UI::WindowsAndMessaging::{
    CheckMenuItem, CreateMenu, CreatePopupMenu, DestroyMenu, EnableMenuItem, GetMenuItemCount, GetMenuItemInfoW, HMENU,
    InsertMenuItemW, MENUINFO, MENUITEMINFOW, MF_BYCOMMAND, MF_CHECKED, MF_ENABLED, MF_GRAYED, MF_UNCHECKED, MFS_CHECKED,
    MFS_DISABLED, MFS_ENABLED, MFT_OWNERDRAW, MFT_SEPARATOR, MIIM_DATA, MIIM_FTYPE, MIIM_ID, MIIM_STATE, MIIM_STRING,
    MIIM_SUBMENU, MIM_BACKGROUND, MNC_EXECUTE, MNC_SELECT, SetMenuInfo, TPM_NOANIMATION, TPM_RETURNCMD, TPM_RIGHTBUTTON,
    TrackPopupMenu,
    SetForegroundWindow,
};
use windows::core::PWSTR;

use crate::theme::tokens;

// ---------------------------------------------------------------- building

/// One entry of a menu. The text may hold an `&` before the access key and a tab before the shortcut
/// ("&Save\tCtrl+S").
pub enum Item {
    Cmd(u32, String),
    /// a command and whether it is enabled
    Cmd2(u32, String, bool),
    Checked(u32, String, bool),
    Sub(String, Menu),
    /// a submenu with an icon
    SubIcon(String, Menu, Icon),
    Separator,
    /// a command with an icon of its own (the commands' icons otherwise come from [`set_icons`])
    Icon(u32, String, Icon),
}

/// A menu (popup, or a list of title bar menus). It owns its submenus and the data of its items.
pub struct Menu(pub HMENU);

// SAFETY: a menu handle is a plain USER handle; it is built on one thread and handed to the window's thread.
unsafe impl Send for Menu {}

/// What an item looks like, kept behind its item data.
struct ItemData {
    label: Vec<u16>,
    keys: Vec<u16>,
    icon: Option<Icon>,
    sep: bool,
    sub: bool,
}

static ICONS: OnceLock<fn(u32) -> Option<Icon>> = OnceLock::new();

/// The icon of each command id, for the whole program (each program has its own list).
pub fn set_icons(f: fn(u32) -> Option<Icon>) {
    let _ = ICONS.set(f);
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

fn background() -> HBRUSH {
    static B: OnceLock<isize> = OnceLock::new();
    HBRUSH(*B.get_or_init(|| unsafe { CreateSolidBrush(cref(colors().bg)).0 as isize }) as _)
}

impl Menu {
    fn fill(m: HMENU, items: Vec<Item>) {
        unsafe {
            // the menu's own background (around the items) in the theme's color
            let mi = MENUINFO { cbSize: size_of::<MENUINFO>() as u32, fMask: MIM_BACKGROUND, hbrBack: background(), ..Default::default() };
            let _ = SetMenuInfo(m, &mi);
        }
        for (pos, it) in items.into_iter().enumerate() {
            let (id, text, enabled, checked, sub, icon) = match it {
                Item::Cmd(id, t) => (id, t, true, false, None, None),
                Item::Cmd2(id, t, on) => (id, t, on, false, None, None),
                Item::Checked(id, t, on) => (id, t, true, on, None, None),
                Item::Icon(id, t, i) => (id, t, true, false, None, Some(i)),
                Item::Sub(t, s) => (0, t, true, false, Some(s), None),
                Item::SubIcon(t, s, i) => (0, t, true, false, Some(s), Some(i)),
                Item::Separator => (0, String::new(), true, false, None, None),
            };
            let sep = text.is_empty() && sub.is_none();
            let icon = icon.or_else(|| (id != 0).then(|| ICONS.get().and_then(|f| f(id))).flatten());
            let (label, keys) = match text.split_once('\t') {
                Some((l, k)) => (wide(l), wide(k)),
                None => (wide(&text), Vec::new()),
            };
            let data = Box::into_raw(Box::new(ItemData { label, keys, icon, sep, sub: sub.is_some() }));
            let mut name: Vec<u16> = wide(&text).into_iter().chain([0]).collect();
            let mut mii = MENUITEMINFOW {
                cbSize: size_of::<MENUITEMINFOW>() as u32,
                fMask: MIIM_FTYPE | MIIM_DATA | MIIM_ID | MIIM_STATE,
                fType: MFT_OWNERDRAW | if sep { MFT_SEPARATOR } else { Default::default() },
                fState: if enabled { MFS_ENABLED } else { MFS_DISABLED } | if checked { MFS_CHECKED } else { Default::default() },
                wID: id,
                dwItemData: data as usize,
                ..Default::default()
            };
            if !sep {
                mii.fMask |= MIIM_STRING;
                mii.dwTypeData = PWSTR(name.as_mut_ptr());
            }
            if let Some(s) = sub {
                mii.fMask |= MIIM_SUBMENU;
                mii.hSubMenu = s.0;
                std::mem::forget(s); // owned by the parent from now on
            }
            unsafe {
                let _ = InsertMenuItemW(m, pos as u32, true, &mii);
            }
        }
    }

    pub fn popup(items: Vec<Item>) -> Menu {
        let m = unsafe { CreatePopupMenu() }.unwrap_or_default();
        Self::fill(m, items);
        Menu(m)
    }

    /// A horizontal menu bar (kept for windows that still want one).
    pub fn bar(items: Vec<Item>) -> Menu {
        let m = unsafe { CreateMenu() }.unwrap_or_default();
        Self::fill(m, items);
        Menu(m)
    }

    /// Shows the popup at a screen point and returns the chosen command (0: none).
    pub fn choose(&self, owner: isize, x: i32, y: i32) -> u32 {
        self.track_at(owner, x, y, true)
    }

    /// Shows the popup at a screen point. With `ret` it returns the chosen command (0: none); otherwise the command
    /// arrives as WM_COMMAND at `owner`. Any window of this thread can own it: for the time the menu is open, a
    /// subclass answers the owner-draw messages (a Snapnab window's theme subclass already does).
    pub fn track_at(&self, owner: isize, x: i32, y: i32, ret: bool) -> u32 {
        use windows::Win32::UI::Shell::{RemoveWindowSubclass, SetWindowSubclass};
        follow_theme();
        let h = HWND(owner as _);
        unsafe {
            let _ = SetWindowSubclass(h, Some(owner_proc), 9, 0);
            let _ = SetForegroundWindow(h);
            let flags = TPM_RIGHTBUTTON | TPM_NOANIMATION | if ret { TPM_RETURNCMD } else { Default::default() };
            let r = TrackPopupMenu(self.0, flags, x, y, None, h, None).0 as u32;
            let _ = RemoveWindowSubclass(h, Some(owner_proc), 9);
            r
        }
    }

    /// The handle stays alive (a menu kept for a window's whole life).
    pub fn into_raw(self) -> HMENU {
        let h = self.0;
        std::mem::forget(self);
        h
    }
}

/// Frees the item data of a menu and its submenus.
fn free_data(m: HMENU) {
    let n = unsafe { GetMenuItemCount(Some(m)) };
    for i in 0..n.max(0) {
        let mut mii =
            MENUITEMINFOW { cbSize: size_of::<MENUITEMINFOW>() as u32, fMask: MIIM_DATA | MIIM_SUBMENU, ..Default::default() };
        if unsafe { GetMenuItemInfoW(m, i as u32, true, &mut mii) }.is_ok() {
            if !mii.hSubMenu.is_invalid() {
                free_data(mii.hSubMenu);
            }
            if mii.dwItemData != 0 {
                drop(unsafe { Box::from_raw(mii.dwItemData as *mut ItemData) });
            }
        }
    }
}

impl Drop for Menu {
    fn drop(&mut self) {
        free_data(self.0);
        unsafe {
            let _ = DestroyMenu(self.0);
        }
    }
}

// ---------------------------------------------------------------- states

/// Enabled and checked states of the menu commands, kept by the app after each frame and applied when a menu opens
/// (WM_INITMENUPOPUP comes inside the window procedure, where the app itself cannot be reached).
#[derive(Clone, Default)]
pub struct MenuStates {
    pub enabled: Vec<(u32, bool)>,
    pub checked: Vec<(u32, bool)>,
}

static STATES: Mutex<Option<MenuStates>> = Mutex::new(None);

pub fn set_states(s: MenuStates) {
    *STATES.lock().unwrap_or_else(|e| e.into_inner()) = Some(s);
}

/// WM_INITMENUPOPUP: applies the states to the menu that opens (commands not in it are ignored).
pub fn apply_states(menu: isize) {
    let g = STATES.lock().unwrap_or_else(|e| e.into_inner());
    let Some(s) = g.as_ref() else { return };
    let m = HMENU(menu as _);
    unsafe {
        for &(id, on) in &s.enabled {
            let _ = EnableMenuItem(m, id, MF_BYCOMMAND | if on { MF_ENABLED } else { MF_GRAYED });
        }
        for &(id, on) in &s.checked {
            let _ = CheckMenuItem(m, id, (MF_BYCOMMAND | if on { MF_CHECKED } else { MF_UNCHECKED }).0);
        }
    }
}

// ---------------------------------------------------------------- the look

/// Sizes in pixels at 96 DPI.
const ITEM_H: i32 = 30;
const SEP_H: i32 = 9;
/// the icon column: the text starts here
const TEXT_X: i32 = 40;
const ICON: i32 = 16;
const ICON_X: i32 = 12;
/// the highlight is inset from the menu's sides
const INSET: i32 = 4;
/// room on the right for the submenu chevron
const RIGHT: i32 = 28;
const KEYS_GAP: i32 = 36;
const MIN_W: i32 = 200;

struct Colors {
    bg: Color32,
    hot: Color32,
    text: Color32,
    dim: Color32,
    off: Color32,
    icon: Color32,
    accent: Color32,
    line: Color32,
    border: Color32,
}

fn colors() -> &'static Colors {
    static C: OnceLock<Colors> = OnceLock::new();
    C.get_or_init(|| {
        let t = tokens();
        if t.dark {
            Colors {
                bg: Color32::from_rgb(44, 44, 44),
                hot: Color32::from_rgb(61, 61, 61),
                text: t.text,
                dim: Color32::from_rgb(170, 170, 170),
                off: Color32::from_rgb(110, 110, 110),
                icon: Color32::from_rgb(214, 214, 214),
                accent: t.accent,
                line: Color32::from_rgb(64, 64, 64),
                border: Color32::from_rgb(68, 68, 68),
            }
        } else {
            Colors {
                bg: Color32::from_rgb(249, 249, 249),
                hot: Color32::from_rgb(234, 234, 234),
                text: t.text,
                dim: Color32::from_rgb(110, 110, 110),
                off: Color32::from_rgb(160, 160, 160),
                icon: Color32::from_rgb(64, 64, 64),
                accent: t.accent,
                line: Color32::from_rgb(222, 222, 222),
                border: Color32::from_rgb(216, 216, 216),
            }
        }
    })
}

fn cref(c: Color32) -> windows::Win32::Foundation::COLORREF {
    windows::Win32::Foundation::COLORREF(c.r() as u32 | (c.g() as u32) << 8 | (c.b() as u32) << 16)
}

fn argb(c: Color32) -> u32 {
    0xFF00_0000 | (c.r() as u32) << 16 | (c.g() as u32) << 8 | c.b() as u32
}

fn scale(v: i32, dpi: u32) -> i32 {
    (v * dpi as i32 + 48) / 96
}

thread_local! {
    static FONTS: RefCell<HashMap<u32, isize>> = RefCell::new(HashMap::new());
    static ICON_IMGS: RefCell<HashMap<(Icon, i32, u32), Image>> = RefCell::new(HashMap::new());
}

/// The menus' font at a DPI: the app's font (Inter, registered for GDI by `crate::install_gdi_fonts`), Segoe UI when
/// it is missing.
fn font(dpi: u32) -> HFONT {
    FONTS.with(|f| {
        let h = *f.borrow_mut().entry(dpi).or_insert_with(|| {
            crate::install_gdi_fonts();
            let px = -((crate::theme::tokens().body * dpi as f32 / 96.0).round() as i32);
            let face: Vec<u16> = crate::gdi_face().encode_utf16().chain([0]).collect();
            unsafe {
                CreateFontW(px, 0, 0, 0, 400, 0, 0, 0, Default::default(), Default::default(), Default::default(),
                            windows::Win32::Graphics::Gdi::CLEARTYPE_QUALITY, Default::default(),
                            windows::core::PCWSTR(face.as_ptr()))
                .0 as isize
            }
        });
        HFONT(h as _)
    })
}

fn icon_image(icon: Icon, px: i32, color: u32) -> Image {
    ICON_IMGS.with(|m| {
        m.borrow_mut()
            .entry((icon, px, color))
            .or_insert_with(|| {
                snapcore::gdip::startup();
                snapcore::icons::render(icon, px, color)
            })
            .clone()
    })
}

fn text_width(dc: HDC, s: &[u16]) -> i32 {
    // the access key's & takes no room
    let shown: Vec<u16> = strip_amp(s);
    let mut sz = windows::Win32::Foundation::SIZE::default();
    unsafe {
        let _ = GetTextExtentPoint32W(dc, &shown, &mut sz);
    }
    sz.cx
}

fn strip_amp(s: &[u16]) -> Vec<u16> {
    let mut out = Vec::with_capacity(s.len());
    let mut i = 0;
    while i < s.len() {
        if s[i] == '&' as u16 && i + 1 < s.len() {
            i += 1;
        }
        out.push(s[i]);
        i += 1;
    }
    out
}

/// WM_MEASUREITEM for a menu item: its size at the window's DPI.
pub fn measure(hwnd: isize, lp: isize) -> bool {
    let mis = unsafe { &mut *(lp as *mut MEASUREITEMSTRUCT) };
    if mis.CtlType != ODT_MENU || mis.itemData == 0 {
        return false;
    }
    let d = unsafe { &*(mis.itemData as *const ItemData) };
    let dpi = unsafe { GetDpiForWindow(HWND(hwnd as _)) }.max(96);
    if d.sep {
        mis.itemWidth = 0;
        mis.itemHeight = scale(SEP_H, dpi) as u32;
        return true;
    }
    unsafe {
        let dc = GetDC(None);
        let old = SelectObject(dc, HGDIOBJ(font(dpi).0));
        let mut w = scale(TEXT_X, dpi) + text_width(dc, &d.label) + scale(RIGHT, dpi);
        if !d.keys.is_empty() {
            w += scale(KEYS_GAP, dpi) + text_width(dc, &d.keys);
        }
        SelectObject(dc, old);
        ReleaseDC(None, dc);
        // Windows adds the width of a check mark column to every owner-drawn item
        let check = windows::Win32::UI::HiDpi::GetSystemMetricsForDpi(
            windows::Win32::UI::WindowsAndMessaging::SM_CXMENUCHECK, dpi);
        mis.itemWidth = (w.max(scale(MIN_W, dpi)) - check + 1).max(0) as u32;
    }
    mis.itemHeight = scale(ITEM_H, dpi) as u32;
    true
}

/// A rounded rectangle in GDI+.
fn round_rect(g: &snapcore::gdip::Graphics, x: f32, y: f32, w: f32, h: f32, r: f32, argb: u32) {
    let p = snapcore::gdip::Path::new();
    let d = r * 2.0;
    p.arc(x, y, d, d, 180.0, 90.0);
    p.arc(x + w - d, y, d, d, 270.0, 90.0);
    p.arc(x + w - d, y + h - d, d, d, 0.0, 90.0);
    p.arc(x, y + h - d, d, d, 90.0, 90.0);
    p.close_figure();
    g.fill_path(&snapcore::gdip::Brush::solid(argb), &p);
}

/// WM_DRAWITEM for a menu item: the background, highlight, icon or check, chevron (in GDI+, antialiased), then the
/// text and the shortcut (in GDI, ClearType).
pub fn draw(hwnd: isize, lp: isize) -> bool {
    use snapcore::gdip::{Bitmap, Graphics, Pen};
    use windows::Win32::Graphics::GdiPlus::{PixelOffsetModeHalf, SmoothingModeAntiAlias};
    let dis = unsafe { &*(lp as *const DRAWITEMSTRUCT) };
    if dis.CtlType != ODT_MENU || dis.itemData == 0 {
        return false;
    }
    let d = unsafe { &*(dis.itemData as *const ItemData) };
    let dpi = unsafe { GetDpiForWindow(HWND(hwnd as _)) }.max(96);
    let c = colors();
    let rc = dis.rcItem;
    let (w, h) = (rc.right - rc.left, rc.bottom - rc.top);
    if w <= 0 || h <= 0 {
        return true;
    }
    let st = dis.itemState.0;
    let enabled = st & (ODS_GRAYED.0 | ODS_DISABLED.0) == 0;
    let hot = st & ODS_SELECTED.0 != 0 && enabled;
    let checked = st & ODS_CHECKED.0 != 0;
    let s = dpi as f32 / 96.0;
    let mut img = Image::sized(w, h, argb(c.bg));
    {
        let bmp = Bitmap::wrap(&mut img);
        let g = Graphics::new(&bmp);
        g.smoothing(SmoothingModeAntiAlias);
        g.pixel_offset(PixelOffsetModeHalf);
        if d.sep {
            let y = (h / 2) as f32;
            g.fill_rect(&snapcore::gdip::Brush::solid(argb(c.line)), snapcore::gdip::rect(12.0 * s, y, w as f32 - 24.0 * s, s.round().max(1.0)));
        } else {
            if hot {
                round_rect(&g, INSET as f32 * s, 2.0 * s, w as f32 - 2.0 * INSET as f32 * s, h as f32 - 4.0 * s, 4.0 * s, argb(c.hot));
            }
            let px = scale(ICON, dpi);
            let (ix, iy) = (scale(ICON_X, dpi) as f32, ((h - px) / 2) as f32);
            if checked && d.icon.is_some() {
                // a checked command with an icon: the icon on a tinted square
                let pad = 3.0 * s;
                round_rect(&g, ix - pad, iy - pad, px as f32 + 2.0 * pad, px as f32 + 2.0 * pad, 4.0 * s,
                           argb(c.accent) & 0x00FF_FFFF | 0x4000_0000);
            }
            let icon = match d.icon {
                Some(i) => Some((i, if !enabled { c.off } else if checked { c.accent } else { c.icon })),
                None if checked => Some((Icon::Check, if enabled { c.accent } else { c.off })),
                None => None,
            };
            if let Some((i, col)) = icon {
                let im = icon_image(i, px, argb(col));
                let ib = Bitmap::wrap_read(&im);
                g.draw_image(&ib, ix, iy, px as f32, px as f32);
            }
            if d.sub {
                let pen = Pen::new(argb(if enabled { c.dim } else { c.off }), 1.2 * s);
                let (cx, cy, k) = (w as f32 - 16.0 * s, h as f32 / 2.0, 3.5 * s);
                g.draw_line(&pen, cx - k / 2.0, cy - k, cx + k / 2.0, cy);
                g.draw_line(&pen, cx + k / 2.0, cy, cx - k / 2.0, cy + k);
            }
        }
    }
    unsafe {
        let hdc = dis.hDC;
        if let Some(hb) = snapcore::screen::to_hbitmap(&img) {
            let mem = CreateCompatibleDC(Some(hdc));
            let old = SelectObject(mem, HGDIOBJ(hb.0));
            let _ = BitBlt(hdc, rc.left, rc.top, w, h, Some(mem), 0, 0, SRCCOPY);
            SelectObject(mem, old);
            let _ = DeleteDC(mem);
            let _ = DeleteObject(HGDIOBJ(hb.0));
        }
        if !d.sep {
            let old = SelectObject(hdc, HGDIOBJ(font(dpi).0));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, cref(if enabled { c.text } else { c.off }));
            let mut r = RECT { left: rc.left + scale(TEXT_X, dpi), top: rc.top, right: rc.right - scale(RIGHT, dpi), bottom: rc.bottom };
            let mut label = d.label.clone();
            let mut fmt = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
            if st & ODS_NOACCEL.0 != 0 {
                fmt |= DT_HIDEPREFIX;
            }
            DrawTextW(hdc, &mut label, &mut r, fmt);
            if !d.keys.is_empty() {
                SetTextColor(hdc, cref(if enabled { c.dim } else { c.off }));
                let mut k = d.keys.clone();
                let mut r = RECT { left: rc.left, top: rc.top, right: rc.right - scale(RIGHT, dpi), bottom: rc.bottom };
                DrawTextW(hdc, &mut k, &mut r, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            }
            SelectObject(hdc, old);
        }
        // Windows would paint its own submenu arrow over the item afterwards
        let _ = ExcludeClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    }
    true
}

/// WM_MENUCHAR: the access keys of owner-drawn items (Windows cannot see their `&` letters). Returns the answer, or
/// None to let Windows beep.
pub fn menu_char(wp: usize, lp: isize) -> Option<LRESULT> {
    let ch = char::from_u32((wp & 0xFFFF) as u32)?.to_lowercase().next()?;
    let m = HMENU(lp as _);
    let n = unsafe { GetMenuItemCount(Some(m)) }.max(0) as u32;
    let mut hits = Vec::new();
    for i in 0..n {
        let mut mii = MENUITEMINFOW {
            cbSize: size_of::<MENUITEMINFOW>() as u32,
            fMask: MIIM_DATA | MIIM_STATE,
            ..Default::default()
        };
        if unsafe { GetMenuItemInfoW(m, i, true, &mut mii) }.is_err() || mii.dwItemData == 0 {
            continue;
        }
        let d = unsafe { &*(mii.dwItemData as *const ItemData) };
        if d.sep || mii.fState.0 & MFS_DISABLED.0 != 0 {
            continue;
        }
        let key = d.label.windows(2).find(|w| w[0] == '&' as u16 && w[1] != '&' as u16).map(|w| w[1]);
        if key.and_then(|k| char::from_u32(k as u32)).and_then(|k| k.to_lowercase().next()) == Some(ch) {
            hits.push(i);
        }
    }
    match hits.len() {
        0 => None,
        1 => Some(LRESULT((MNC_EXECUTE << 16) as isize | hits[0] as isize)),
        _ => Some(LRESULT((MNC_SELECT << 16) as isize | hits[0] as isize)),
    }
}

/// Makes Windows draw the menus' frames (and the tray's plain menus) in the theme: dark or light, whatever the
/// system's mode. Windows has no documented switch for this; uxtheme's SetPreferredAppMode (ordinal 135, Windows 10
/// 1903 and later) is the one other Win32 programs use. Called once.
pub fn follow_theme() {
    static DONE: OnceLock<()> = OnceLock::new();
    DONE.get_or_init(|| unsafe {
        use windows::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};
        let Ok(ux) = LoadLibraryW(windows::core::w!("uxtheme.dll")) else { return };
        if let Some(f) = GetProcAddress(ux, windows::core::PCSTR(135 as *const u8)) {
            let set: extern "system" fn(i32) -> i32 = std::mem::transmute(f);
            set(if tokens().dark { 2 } else { 3 }); // ForceDark / ForceLight
        }
        if let Some(f) = GetProcAddress(ux, windows::core::PCSTR(136 as *const u8)) {
            let flush: extern "system" fn() = std::mem::transmute(f);
            flush();
        }
    });
    hook_menu_windows();
}

unsafe extern "system" fn owner_proc(hwnd: HWND, msg: u32, wp: windows::Win32::Foundation::WPARAM,
                                     lp: windows::Win32::Foundation::LPARAM, _id: usize, _data: usize) -> LRESULT {
    match handle(hwnd.0 as isize, msg, wp.0, lp.0) {
        Some(r) => r,
        None => unsafe { windows::Win32::UI::Shell::DefSubclassProc(hwnd, msg, wp, lp) },
    }
}

/// The menu windows ("#32768") of this thread get Windows 11's rounded corners and a frame in the theme's colors:
/// Windows paints a classic light frame around a menu whose background is set (MIM_BACKGROUND). A thread hook sees
/// each menu window being created; a subclass then paints its frame.
fn hook_menu_windows() {
    use windows::Win32::UI::WindowsAndMessaging::{SetWindowsHookExW, WH_CALLWNDPROC};
    thread_local!(static HOOKED: std::cell::Cell<bool> = const { std::cell::Cell::new(false) });
    if HOOKED.with(|h| h.replace(true)) {
        return;
    }
    unsafe {
        let _ = SetWindowsHookExW(WH_CALLWNDPROC, Some(call_wnd_proc), None, windows::Win32::System::Threading::GetCurrentThreadId());
    }
}

unsafe extern "system" fn call_wnd_proc(code: i32, wp: windows::Win32::Foundation::WPARAM, lp: windows::Win32::Foundation::LPARAM)
                                        -> LRESULT {
    use windows::Win32::UI::WindowsAndMessaging::{CWPSTRUCT, CallNextHookEx, GetClassNameW, WM_CREATE};
    if code >= 0 {
        let m = unsafe { &*(lp.0 as *const CWPSTRUCT) };
        if m.message == WM_CREATE {
            let mut cls = [0u16; 16];
            let n = unsafe { GetClassNameW(m.hwnd, &mut cls) } as usize;
            if &cls[..n] == wide("#32768").as_slice() {
                style_menu_window(m.hwnd);
            }
        }
    }
    unsafe { CallNextHookEx(None, code, wp, lp) }
}

fn style_menu_window(h: HWND) {
    use windows::Win32::Graphics::Dwm::{DWMWA_BORDER_COLOR, DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND, DwmSetWindowAttribute};
    use windows::Win32::UI::Shell::SetWindowSubclass;
    let border = cref(colors().border);
    unsafe {
        let pref = DWMWCP_ROUND;
        let _ = DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref as *const _ as _, 4);
        let _ = DwmSetWindowAttribute(h, DWMWA_BORDER_COLOR, &border as *const _ as _, 4);
        let _ = SetWindowSubclass(h, Some(menu_frame_proc), 7, 0);
    }
}

/// Paints a menu window's frame (between its outer edge and the items) in the menu's background.
unsafe extern "system" fn menu_frame_proc(hwnd: HWND, msg: u32, wp: windows::Win32::Foundation::WPARAM,
                                          lp: windows::Win32::Foundation::LPARAM, _id: usize, _data: usize) -> LRESULT {
    use windows::Win32::Graphics::Gdi::{CombineRgn, CreateRectRgn, FillRgn, GetWindowDC, RGN_DIFF};
    use windows::Win32::UI::Shell::{DefSubclassProc, RemoveWindowSubclass};
    use windows::Win32::UI::WindowsAndMessaging::{GetClientRect, GetWindowRect, WM_NCDESTROY, WM_NCPAINT, WM_PRINT};
    unsafe {
        match msg {
            WM_NCPAINT | WM_PRINT => {
                let r = if msg == WM_PRINT { DefSubclassProc(hwnd, msg, wp, lp) } else { LRESULT(0) };
                let mut wr = RECT::default();
                let _ = GetWindowRect(hwnd, &mut wr);
                let mut cr = RECT::default();
                let _ = GetClientRect(hwnd, &mut cr);
                let mut tl = windows::Win32::Foundation::POINT { x: 0, y: 0 };
                let _ = windows::Win32::Graphics::Gdi::ClientToScreen(hwnd, &mut tl);
                let (ox, oy) = (tl.x - wr.left, tl.y - wr.top);
                let outer = CreateRectRgn(0, 0, wr.right - wr.left, wr.bottom - wr.top);
                let inner = CreateRectRgn(ox, oy, ox + cr.right, oy + cr.bottom);
                let _ = CombineRgn(Some(outer), Some(outer), Some(inner), RGN_DIFF);
                let dc = if msg == WM_PRINT { HDC(wp.0 as _) } else { GetWindowDC(Some(hwnd)) };
                let _ = FillRgn(dc, outer, background());
                if msg != WM_PRINT {
                    ReleaseDC(Some(hwnd), dc);
                }
                let _ = DeleteObject(HGDIOBJ(outer.0));
                let _ = DeleteObject(HGDIOBJ(inner.0));
                r
            }
            WM_NCDESTROY => {
                let _ = RemoveWindowSubclass(hwnd, Some(menu_frame_proc), 7);
                DefSubclassProc(hwnd, msg, wp, lp)
            }
            _ => DefSubclassProc(hwnd, msg, wp, lp),
        }
    }
}

/// For the menus' window procedure (theme.rs): handles the owner-draw messages. Returns the answer when handled.
pub fn handle(hwnd: isize, msg: u32, wp: usize, lp: isize) -> Option<LRESULT> {
    use windows::Win32::UI::WindowsAndMessaging::{WM_DRAWITEM, WM_INITMENUPOPUP, WM_MEASUREITEM, WM_MENUCHAR};
    match msg {
        WM_MEASUREITEM if wp == 0 && measure(hwnd, lp) => Some(LRESULT(1)),
        WM_DRAWITEM if wp == 0 && draw(hwnd, lp) => Some(LRESULT(1)),
        WM_MENUCHAR => menu_char(wp, lp),
        WM_INITMENUPOPUP => {
            apply_states(wp as isize);
            None
        }
        _ => None,
    }
}

