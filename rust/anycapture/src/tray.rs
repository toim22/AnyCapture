//! The notification-area icon (Shell_NotifyIcon, comp_1 §2): a click captures, a double click opens the capture
//! window, the right button opens the menu. The menu is a native Win32 popup menu drawn by snapui (the theme's look), and
//! its WM_COMMAND reaches the capture window like the C++ app's did.

use std::sync::Mutex;
use std::sync::atomic::{AtomicIsize, Ordering};

use windows::Win32::Foundation::{HWND, POINT};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::core::PCWSTR;
use windows::Win32::Graphics::Gdi::{
    BI_RGB, BITMAPINFO, BITMAPINFOHEADER, CreateBitmap, CreateDIBSection, DIB_RGB_COLORS, DeleteObject, HBITMAP,
};
use windows::Win32::UI::Shell::{
    NIF_ICON, NIF_INFO, NIF_MESSAGE, NIF_TIP, NIIF_INFO, NIIF_LARGE_ICON, NIIF_NOSOUND, NIIF_USER, NIM_ADD, NIM_DELETE, NIM_MODIFY, NIM_SETVERSION,
    NOTIFYICON_VERSION_4, NOTIFYICONDATAW, Shell_NotifyIconW,
};
use windows::Win32::UI::WindowsAndMessaging::{
    CreateIconIndirect, DestroyIcon, GetCursorPos, GetSystemMetrics, HICON, ICONINFO, IMAGE_ICON, LR_DEFAULTCOLOR, LoadImageW,
    PostMessageW, SM_CXICON, SM_CXSMICON, SetMenuDefaultItem, WM_APP, WM_NULL,
};

use snapcore::image::Image;

/// The tray icon's callback message (WM_SNAP_TRAY of the C++ app).
pub const WM_SNAP_TRAY: u32 = WM_APP + 0x41;

static ICON_SMALL: AtomicIsize = AtomicIsize::new(0);
static ICON_BUSY: AtomicIsize = AtomicIsize::new(0);

/// NIN_BALLOONUSERCLICK: the user clicked the notification.
pub const NIN_BALLOONUSERCLICK: u32 = 0x405;

/// The capture the last notification shows (its library file), for a click on it (#7); the icon shown in it.
static BALLOON: Mutex<(String, isize)> = Mutex::new((String::new(), 0));

/// The program's icon (resource 1 of AnyCapture.exe) at `size` pixels: the .ico has hand-tuned frames for the small
/// sizes of the notification area.
pub fn app_icon(size: i32) -> HICON {
    unsafe {
        let Ok(module) = GetModuleHandleW(None) else { return HICON::default() };
        LoadImageW(Some(module.into()), PCWSTR(1 as *const u16), IMAGE_ICON, size, size, LR_DEFAULTCOLOR)
            .map(|h| HICON(h.0))
            .unwrap_or_default()
    }
}

/// An icon of `size`×`size` ARGB pixels (straight alpha).
fn icon_from_pixels(px: &[u32], size: i32) -> HICON {
    unsafe {
        let bi = BITMAPINFO {
            bmiHeader: BITMAPINFOHEADER {
                biSize: std::mem::size_of::<BITMAPINFOHEADER>() as u32,
                biWidth: size,
                biHeight: -size,
                biPlanes: 1,
                biBitCount: 32,
                biCompression: BI_RGB.0,
                ..Default::default()
            },
            ..Default::default()
        };
        let mut bits = std::ptr::null_mut();
        let Ok(color) = CreateDIBSection(None, &bi, DIB_RGB_COLORS, &mut bits, None, 0) else { return HICON::default() };
        std::ptr::copy_nonoverlapping(px.as_ptr(), bits as *mut u32, px.len());
        let mask: HBITMAP = CreateBitmap(size, size, 1, 1, None);
        let info = ICONINFO { fIcon: true.into(), xHotspot: 0, yHotspot: 0, hbmMask: mask, hbmColor: color };
        let icon = CreateIconIndirect(&info).unwrap_or_default();
        let _ = DeleteObject(color.into());
        let _ = DeleteObject(mask.into());
        icon
    }
}

/// The notification area's icon size (16 pixels at 100%, 20 at 125%, 24 at 150%, 32 at 200%).
fn tray_size() -> i32 {
    match unsafe { GetSystemMetrics(SM_CXSMICON) } {
        n if n > 0 => n,
        _ => 16,
    }
}

fn nid(hwnd: isize) -> NOTIFYICONDATAW {
    NOTIFYICONDATAW {
        cbSize: std::mem::size_of::<NOTIFYICONDATAW>() as u32,
        hWnd: HWND(hwnd as _),
        uID: 1,
        ..Default::default()
    }
}

fn copy(dst: &mut [u16], s: &str) {
    let w: Vec<u16> = s.encode_utf16().take(dst.len() - 1).collect();
    dst[..w.len()].copy_from_slice(&w);
    dst[w.len()] = 0;
}

pub fn add(hwnd: isize, tip: &str) {
    let mut icon = ICON_SMALL.load(Ordering::Relaxed);
    if icon == 0 {
        icon = app_icon(tray_size()).0 as isize;
        ICON_SMALL.store(icon, Ordering::Relaxed);
    }
    let mut n = nid(hwnd);
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = WM_SNAP_TRAY;
    n.hIcon = HICON(icon as _);
    copy(&mut n.szTip, tip);
    unsafe {
        let _ = Shell_NotifyIconW(NIM_ADD, &n);
        n.Anonymous.uVersion = NOTIFYICON_VERSION_4;
        let _ = Shell_NotifyIconW(NIM_SETVERSION, &n);
    }
}

pub fn remove(hwnd: isize) {
    unsafe {
        let _ = Shell_NotifyIconW(NIM_DELETE, &nid(hwnd));
    }
}

/// MakeBusyIcon: the app icon with a red dot (the interval capture runs).
fn busy_icon(size: i32) -> HICON {
    let base = app_icon(size);
    let mut px = vec![0u32; (size * size) as usize];
    unsafe {
        use windows::Win32::Graphics::Gdi::{CreateCompatibleDC, DeleteDC, SelectObject};
        use windows::Win32::UI::WindowsAndMessaging::{DI_NORMAL, DestroyIcon, DrawIconEx};
        let bi = BITMAPINFO {
            bmiHeader: BITMAPINFOHEADER {
                biSize: std::mem::size_of::<BITMAPINFOHEADER>() as u32,
                biWidth: size,
                biHeight: -size,
                biPlanes: 1,
                biBitCount: 32,
                biCompression: BI_RGB.0,
                ..Default::default()
            },
            ..Default::default()
        };
        let mut bits = std::ptr::null_mut();
        let Ok(color) = CreateDIBSection(None, &bi, DIB_RGB_COLORS, &mut bits, None, 0) else { return base };
        let dc = CreateCompatibleDC(None);
        let o = SelectObject(dc, color.into());
        let _ = DrawIconEx(dc, 0, 0, base, size, size, 0, None, DI_NORMAL);
        SelectObject(dc, o);
        let _ = DeleteDC(dc);
        std::ptr::copy_nonoverlapping(bits as *const u32, px.as_mut_ptr(), px.len());
        let r = (size * 3 / 8) as f32;
        for y in 0..size {
            for x in 0..size {
                let (dx, dy) = (x as f32 + 0.5 - (size as f32 - r), y as f32 + 0.5 - (size as f32 - r));
                if dx * dx + dy * dy <= r * r {
                    px[(y * size + x) as usize] = 0xFFE5_3935;
                }
            }
        }
        std::ptr::copy_nonoverlapping(px.as_ptr(), bits as *mut u32, px.len());
        let mask: HBITMAP = CreateBitmap(size, size, 1, 1, None);
        let info = ICONINFO { fIcon: true.into(), xHotspot: 0, yHotspot: 0, hbmMask: mask, hbmColor: color };
        let icon = CreateIconIndirect(&info).unwrap_or_default();
        let _ = DeleteObject(color.into());
        let _ = DeleteObject(mask.into());
        let _ = DestroyIcon(base);
        icon
    }
}

/// UpdateTrayStatus: the red dot and the count while the interval capture runs.
pub fn set_status(hwnd: isize, busy: bool, tip: &str) {
    let icon = if busy {
        let mut b = ICON_BUSY.load(Ordering::Relaxed);
        if b == 0 {
            b = busy_icon(tray_size()).0 as isize;
            ICON_BUSY.store(b, Ordering::Relaxed);
        }
        b
    } else {
        ICON_SMALL.load(Ordering::Relaxed)
    };
    let mut n = nid(hwnd);
    n.uFlags = NIF_ICON | NIF_TIP;
    n.hIcon = HICON(icon as _);
    copy(&mut n.szTip, tip);
    unsafe {
        let _ = Shell_NotifyIconW(NIM_MODIFY, &n);
    }
}

/// A balloon notification from the tray icon.
pub fn notify(hwnd: isize, title: &str, text: &str) {
    set_balloon(String::new(), 0);
    let mut n = nid(hwnd);
    n.uFlags = NIF_INFO;
    copy(&mut n.szInfoTitle, title);
    copy(&mut n.szInfo, text);
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    unsafe {
        let _ = Shell_NotifyIconW(NIM_MODIFY, &n);
    }
}

/// The notification after a capture that only went to the clipboard (#7): a miniature of the capture in it, and a click
/// on it opens `lib` in the Editor (`take_balloon_capture`).
pub fn notify_capture(hwnd: isize, title: &str, text: &str, img: &Image, lib: &str) {
    let size = unsafe { GetSystemMetrics(SM_CXICON) }.max(32);
    let thumb = snapcore::gdip::make_thumbnail(img, size, size);
    let mut sq = Image::sized(size, size, 0);
    sq.blit(&thumb, (size - thumb.w as i32) / 2, (size - thumb.h as i32) / 2, false);
    let icon = if thumb.is_empty() { HICON::default() } else { icon_from_pixels(&sq.px, size) };
    set_balloon(lib.to_owned(), icon.0 as isize);
    let mut n = nid(hwnd);
    n.uFlags = NIF_INFO;
    copy(&mut n.szInfoTitle, title);
    copy(&mut n.szInfo, text);
    n.dwInfoFlags = NIIF_NOSOUND | if icon.is_invalid() { NIIF_INFO } else { NIIF_USER | NIIF_LARGE_ICON };
    n.hBalloonIcon = icon;
    unsafe {
        let _ = Shell_NotifyIconW(NIM_MODIFY, &n);
    }
}

/// The capture of the notification just clicked (once), if it showed one.
pub fn take_balloon_capture() -> Option<String> {
    let mut b = BALLOON.lock().unwrap_or_else(|e| e.into_inner());
    Some(std::mem::take(&mut b.0)).filter(|p| !p.is_empty())
}

fn set_balloon(lib: String, icon: isize) {
    let mut b = BALLOON.lock().unwrap_or_else(|e| e.into_inner());
    if b.1 != 0 {
        // the shell copied the previous icon when it showed it
        unsafe {
            let _ = DestroyIcon(HICON(b.1 as _));
        }
    }
    *b = (lib, icon);
}

/// A popup menu under construction: snapui's owner-drawn native menu (the theme's colors and font), with the
/// borrowed texts the capture app builds its menus from.
pub struct Menu(snapui::menu::Menu);

pub enum Item<'a> {
    Cmd(u32, &'a str),
    Checked(u32, &'a str, bool),
    Sub(&'a str, Menu),
    /// grayed out
    Disabled(&'a str),
    Separator,
}

impl Menu {
    pub fn new(items: Vec<Item>) -> Menu {
        use snapui::menu::Item as I;
        let items = items
            .into_iter()
            .map(|it| match it {
                Item::Cmd(id, t) => I::Cmd(id, t.to_owned()),
                Item::Checked(id, t, on) => I::Checked(id, t.to_owned(), on),
                Item::Sub(t, sub) => I::Sub(t.to_owned(), sub.0),
                Item::Disabled(t) => I::Cmd2(0, t.to_owned(), false),
                Item::Separator => I::Separator,
            })
            .collect();
        Menu(snapui::menu::Menu::popup(items))
    }

    /// Shows the menu at the cursor; the chosen item arrives as WM_COMMAND at `owner`.
    pub fn track(&self, owner: isize, default_item: Option<u32>) {
        unsafe {
            if let Some(d) = default_item {
                let _ = SetMenuDefaultItem(self.0.0, d, 0);
            }
            let mut p = POINT::default();
            let _ = GetCursorPos(&mut p);
            self.0.track_at(owner, p.x, p.y, false);
            let _ = PostMessageW(Some(HWND(owner as _)), WM_NULL, Default::default(), Default::default());
        }
    }

    /// Shows the menu at a screen point and returns the chosen item (0: none).
    pub fn choose(&self, owner: isize, x: i32, y: i32) -> u32 {
        self.0.choose(owner, x, y)
    }
}
