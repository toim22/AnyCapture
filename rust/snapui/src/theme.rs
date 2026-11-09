//! The look of every AnyCapture window: one set of colors, radii, spacing and fonts applied to each egui Context, so a
//! theme changes everything at once. egui keeps all of this in `Visuals` (colors, corners, shadows) and `Style`
//! (spacing, text sizes); widgets read them every frame, so setting them once per window is enough.
//!
//! AnyCapture looks like Windows 11 (Fluent): Segoe UI Variable, 4 px controls, 8 px windows, the Windows accent color and
//! the Fluent gray surfaces, in light or dark. Options › Appearance picks the mode (follow Windows, light or dark); the
//! `ANYCAPTURE_THEME` variable (`light` / `dark`) overrides it, for the tests' screenshots.

use std::sync::Arc;

use egui::{Color32, CornerRadius, FontId, Stroke, TextStyle, Visuals};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Theme {
    Light,
    Dark,
}

impl Theme {
    pub fn resolve() -> Theme {
        if crate::fixed_look() {
            return Theme::Light;
        }
        let v = std::env::var("ANYCAPTURE_THEME").unwrap_or_default().to_ascii_lowercase();
        match v.as_str() {
            "light" | "win11-light" => Theme::Light,
            "dark" | "win11-dark" => Theme::Dark,
            _ => match appearance() {
                1 => Theme::Light,
                2 => Theme::Dark,
                _ if apps_use_light_theme() => Theme::Light,
                _ => Theme::Dark,
            },
        }
    }

    pub fn is_dark(self) -> bool {
        self == Theme::Dark
    }
}

/// The colors and shapes of the look.
#[derive(Clone, Copy, Debug)]
pub struct Tokens {
    pub dark: bool,
    /// the window background
    pub window: Color32,
    /// panels, cards and drop-down lists on the window
    pub card: Color32,
    pub card_border: Color32,
    /// text boxes, buttons, combo boxes
    pub field: Color32,
    pub field_border: Color32,
    pub hover: Color32,
    pub pressed: Color32,
    pub text: Color32,
    pub text_dim: Color32,
    /// text and icons of a disabled control
    pub text_off: Color32,
    pub accent: Color32,
    /// text on an accent fill
    pub on_accent: Color32,
    pub selection: Color32,
    pub separator: Color32,
    pub shadow: Color32,
    /// the track of a switch that is off
    pub switch_off: Color32,
    /// around the picture in the Editor
    pub canvas: Color32,
    pub radius_sm: u8,
    pub radius: u8,
    pub radius_lg: u8,
    pub body: f32,
}

impl Tokens {
    pub fn of(theme: Theme) -> Tokens {
        let rgb = Color32::from_rgb;
        match theme {
            Theme::Light => {
                let accent = accent_shade(4).unwrap_or(rgb(0, 95, 184));
                Tokens {
                    dark: false,
                    window: rgb(243, 243, 243),
                    card: rgb(251, 251, 251),
                    card_border: rgb(229, 229, 229),
                    field: rgb(255, 255, 255),
                    field_border: rgb(214, 214, 214),
                    hover: rgb(246, 246, 246),
                    pressed: rgb(240, 240, 240),
                    text: rgb(27, 27, 27),
                    text_dim: rgb(96, 96, 96),
                    text_off: rgb(160, 160, 160),
                    accent,
                    on_accent: Color32::WHITE,
                    selection: accent.gamma_multiply(0.25),
                    separator: rgb(229, 229, 229),
                    shadow: Color32::from_black_alpha(40),
                    switch_off: rgb(138, 138, 138),
                    canvas: rgb(232, 232, 232),
                    radius_sm: 4,
                    radius: 4,
                    radius_lg: 8,
                    body: 14.0,
                }
            }
            Theme::Dark => {
                let accent = accent_shade(1).unwrap_or(rgb(96, 205, 255));
                Tokens {
                    dark: true,
                    window: rgb(32, 32, 32),
                    card: rgb(43, 43, 43),
                    card_border: rgb(29, 29, 29),
                    field: rgb(45, 45, 45),
                    field_border: rgb(60, 60, 60),
                    hover: rgb(50, 50, 50),
                    pressed: rgb(39, 39, 39),
                    text: rgb(255, 255, 255),
                    text_dim: rgb(206, 206, 206),
                    text_off: rgb(120, 120, 120),
                    accent,
                    on_accent: Color32::BLACK,
                    selection: accent.gamma_multiply(0.35),
                    separator: rgb(56, 56, 56),
                    shadow: Color32::from_black_alpha(120),
                    switch_off: rgb(120, 120, 120),
                    canvas: rgb(28, 28, 28),
                    radius_sm: 4,
                    radius: 4,
                    radius_lg: 8,
                    body: 14.0,
                }
            }
        }
    }
}

impl Tokens {
    /// The hover fill of a flat button on the window background (the title bar's buttons and menus).
    pub fn hover_strong(&self) -> Color32 {
        if self.dark { Color32::from_white_alpha(18) } else { Color32::from_black_alpha(12) }
    }
    pub fn pressed_strong(&self) -> Color32 {
        if self.dark { Color32::from_white_alpha(11) } else { Color32::from_black_alpha(8) }
    }
}

/// The theme of this process, read once.
pub fn current() -> Theme {
    static T: std::sync::OnceLock<Theme> = std::sync::OnceLock::new();
    *T.get_or_init(Theme::resolve)
}

/// The tokens of this process' theme.
pub fn tokens() -> Tokens {
    static T: std::sync::OnceLock<Tokens> = std::sync::OnceLock::new();
    *T.get_or_init(|| Tokens::of(current()))
}

/// Sets the theme's visuals and style on a window's Context (both egui's light and dark branches, so the theme holds
/// whatever Windows reports).
pub fn apply(ctx: &egui::Context) {
    let t = tokens();
    let mut v = if t.dark { Visuals::dark() } else { Visuals::light() };
    v.panel_fill = t.window;
    v.window_fill = t.card;
    v.window_stroke = Stroke::new(1.0, t.card_border);
    v.extreme_bg_color = t.field;
    v.faint_bg_color = t.card;
    v.code_bg_color = t.field;
    v.override_text_color = Some(t.text);
    v.hyperlink_color = t.accent;
    v.window_corner_radius = CornerRadius::same(t.radius_lg);
    v.menu_corner_radius = CornerRadius::same(t.radius_lg);
    v.window_shadow = egui::Shadow { offset: [0, 8], blur: 24, spread: 0, color: t.shadow };
    v.popup_shadow = egui::Shadow { offset: [0, 4], blur: 16, spread: 0, color: t.shadow };
    v.selection.bg_fill = t.selection;
    v.selection.stroke = Stroke::new(1.0, t.accent);
    v.slider_trailing_fill = true;
    v.handle_shape = egui::style::HandleShape::Circle;
    let r = CornerRadius::same(t.radius_sm);
    let w = &mut v.widgets;
    w.noninteractive.bg_fill = t.card;
    w.noninteractive.weak_bg_fill = t.card;
    w.noninteractive.bg_stroke = Stroke::new(1.0, t.separator);
    w.noninteractive.fg_stroke = Stroke::new(1.0, t.text_dim);
    w.noninteractive.corner_radius = r;
    for (wv, bg, stroke) in [
        (&mut w.inactive, t.field, t.field_border),
        (&mut w.hovered, t.hover, t.field_border),
        (&mut w.active, t.pressed, t.accent),
        (&mut w.open, t.hover, t.field_border),
    ] {
        wv.bg_fill = bg;
        wv.weak_bg_fill = bg;
        wv.bg_stroke = Stroke::new(1.0, stroke);
        wv.fg_stroke = Stroke::new(1.0, t.text);
        wv.corner_radius = r;
        wv.expansion = 0.0;
    }
    ctx.set_visuals(v);
    ctx.global_style_mut(|s| {
        s.text_styles = [
            (TextStyle::Small, FontId::proportional(t.body - 2.0)),
            (TextStyle::Body, FontId::proportional(t.body)),
            (TextStyle::Button, FontId::proportional(t.body)),
            (TextStyle::Heading, FontId::proportional(t.body + 6.0)),
            (TextStyle::Monospace, FontId::monospace(t.body - 1.0)),
        ]
        .into();
        s.spacing.item_spacing = egui::vec2(8.0, 6.0);
        s.spacing.button_padding = egui::vec2(11.0, 5.0);
        s.spacing.interact_size = egui::vec2(28.0, 28.0);
        s.spacing.menu_margin = egui::Margin::same(6);
        s.spacing.window_margin = egui::Margin::same(16);
        s.spacing.scroll = egui::style::ScrollStyle::thin();
    });
    let style = ctx.global_style();
    ctx.set_style_of(egui::Theme::Light, Arc::clone(&style));
    ctx.set_style_of(egui::Theme::Dark, style);
}

/// Options › Appearance: 0 follows Windows, 1 light, 2 dark.
pub fn appearance() -> u32 {
    snapcore::reg::get_dword(std::ffi::OsStr::new("Appearance"), 0, None)
}

/// Windows' "app mode" (Settings › Personalization › Colors).
pub fn apps_use_light_theme() -> bool {
    read_reg(r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme")
        .map(|b| b.first().copied().unwrap_or(1) != 0)
        .unwrap_or(true)
}

/// One of the 8 shades Windows derives from the accent color (0 = lightest … 6 = darkest, 3 = the accent itself).
/// Fluent uses a light shade on dark surfaces and a dark one on light surfaces, for contrast.
pub fn accent_shade(i: usize) -> Option<Color32> {
    if crate::fixed_look() {
        return None;
    }
    let b = read_reg(r"Software\Microsoft\Windows\CurrentVersion\Explorer\Accent", "AccentPalette")?;
    let c = b.get(i * 4..i * 4 + 3)?;
    Some(Color32::from_rgb(c[0], c[1], c[2]))
}

fn read_reg(key: &str, value: &str) -> Option<Vec<u8>> {
    use windows::Win32::System::Registry::{HKEY_CURRENT_USER, RRF_RT_ANY, RegGetValueW};
    use windows::core::HSTRING;
    let (k, v) = (HSTRING::from(key), HSTRING::from(value));
    let mut buf = vec![0u8; 64];
    let mut len = buf.len() as u32;
    let rc = unsafe {
        RegGetValueW(HKEY_CURRENT_USER, &k, &v, RRF_RT_ANY, None, Some(buf.as_mut_ptr().cast()), Some(&mut len))
    };
    if rc.is_err() {
        return None;
    }
    buf.truncate(len as usize);
    Some(buf)
}

/// Makes the parts Windows draws follow the theme: the title bar (its color and dark mode), the popup menus (drawn by
/// `menu.rs`) and a native menu bar, which Windows always paints light. The menu bar is painted through the undocumented messages Windows sends to a
/// window to draw it (WM_UAHDRAWMENU and WM_UAHDRAWMENUITEM), as other Win32 programs with a dark mode do.
pub fn style_window(hwnd: isize) {
    use windows::Win32::Foundation::{COLORREF, HWND};
    use windows::Win32::Graphics::Dwm::{DWMWA_CAPTION_COLOR, DWMWA_TEXT_COLOR, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                        DwmSetWindowAttribute};
    use windows::Win32::UI::Shell::SetWindowSubclass;
    let t = tokens();
    let h = HWND(hwnd as _);
    let dark: i32 = t.dark as i32;
    let cref = |c: Color32| COLORREF(c.r() as u32 | (c.g() as u32) << 8 | (c.b() as u32) << 16);
    unsafe {
        let _ = DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark as *const i32 as _, 4);
        let (cap, txt) = (cref(t.window), cref(t.text));
        let _ = DwmSetWindowAttribute(h, DWMWA_CAPTION_COLOR, &cap as *const COLORREF as _, 4);
        let _ = DwmSetWindowAttribute(h, DWMWA_TEXT_COLOR, &txt as *const COLORREF as _, 4);
        let _ = SetWindowSubclass(h, Some(menu_bar_proc), 2, 0);
    }
    crate::menu::follow_theme();
}

unsafe extern "system" fn menu_bar_proc(hwnd: windows::Win32::Foundation::HWND, msg: u32,
                                        wp: windows::Win32::Foundation::WPARAM, lp: windows::Win32::Foundation::LPARAM,
                                        _id: usize, _data: usize) -> windows::Win32::Foundation::LRESULT {
    use windows::Win32::Foundation::{LRESULT, POINT, RECT};
    use windows::Win32::Graphics::Gdi::*;
    use windows::Win32::UI::Controls::{DRAWITEMSTRUCT, ODS_DISABLED, ODS_GRAYED, ODS_HOTLIGHT, ODS_INACTIVE, ODS_NOACCEL, ODS_SELECTED};
    use windows::Win32::UI::Shell::{DefSubclassProc, RemoveWindowSubclass};
    use windows::Win32::UI::WindowsAndMessaging::*;
    const WM_UAHDRAWMENU: u32 = 0x0091;
    const WM_UAHDRAWMENUITEM: u32 = 0x0092;
    let t = tokens();
    let cref = |c: Color32| windows::Win32::Foundation::COLORREF(c.r() as u32 | (c.g() as u32) << 8 | (c.b() as u32) << 16);
    // the owner-drawn menus this window opens
    if let Some(r) = crate::menu::handle(hwnd.0 as isize, msg, wp.0, lp.0) {
        return r;
    }
    unsafe {
        match msg {
            WM_UAHDRAWMENU => {
                // lParam: { HMENU, HDC, flags }
                let hdc = HDC(*((lp.0 as *const u8).add(8) as *const *mut core::ffi::c_void));
                let mut mbi = MENUBARINFO { cbSize: size_of::<MENUBARINFO>() as u32, ..Default::default() };
                let _ = GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mut mbi);
                let mut wr = RECT::default();
                let _ = GetWindowRect(hwnd, &mut wr);
                let mut r = mbi.rcBar;
                let _ = OffsetRect(&mut r, -wr.left, -wr.top);
                let b = CreateSolidBrush(cref(t.window));
                FillRect(hdc, &r, b);
                let _ = DeleteObject(b.into());
                LRESULT(0)
            }
            WM_UAHDRAWMENUITEM => {
                // lParam: { DRAWITEMSTRUCT (64 bytes), UAHMENU { HMENU, HDC, flags } (24), UAHMENUITEM { iPosition… } }
                let p = lp.0 as *const u8;
                let dis = &*(p as *const DRAWITEMSTRUCT);
                let hmenu = HMENU(*(p.add(64) as *const *mut core::ffi::c_void));
                let pos = *(p.add(88) as *const i32);
                let mut buf = [0u16; 256];
                let n = GetMenuStringW(hmenu, pos as u32, Some(&mut buf), MF_BYPOSITION);
                let state = dis.itemState;
                let hot = state.0 & (ODS_HOTLIGHT.0 | ODS_SELECTED.0) != 0;
                let grayed = state.0 & (ODS_GRAYED.0 | ODS_DISABLED.0 | ODS_INACTIVE.0) != 0;
                let b = CreateSolidBrush(cref(if hot { t.hover } else { t.window }));
                FillRect(dis.hDC, &dis.rcItem, b);
                let _ = DeleteObject(b.into());
                SetBkMode(dis.hDC, TRANSPARENT);
                SetTextColor(dis.hDC, cref(if grayed { t.text_dim } else { t.text }));
                let mut r = dis.rcItem;
                let mut fmt = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
                if state.0 & ODS_NOACCEL.0 != 0 {
                    fmt |= DT_HIDEPREFIX;
                }
                DrawTextW(dis.hDC, &mut buf[..n.max(0) as usize], &mut r, fmt);
                LRESULT(0)
            }
            WM_NCPAINT | WM_NCACTIVATE => {
                let r = DefSubclassProc(hwnd, msg, wp, lp);
                // the light line Windows leaves under the menu bar
                if GetMenu(hwnd).0.is_null() {
                    return r;
                }
                let mut cr = RECT::default();
                let _ = GetClientRect(hwnd, &mut cr);
                let mut pts = [POINT { x: cr.left, y: cr.top }, POINT { x: cr.right, y: cr.bottom }];
                MapWindowPoints(Some(hwnd), None, &mut pts);
                let mut wr = RECT::default();
                let _ = GetWindowRect(hwnd, &mut wr);
                let line = RECT { left: pts[0].x - wr.left, top: pts[0].y - wr.top - 1, right: pts[1].x - wr.left,
                                  bottom: pts[0].y - wr.top };
                let hdc = GetWindowDC(Some(hwnd));
                let b = CreateSolidBrush(cref(t.window));
                FillRect(hdc, &line, b);
                let _ = DeleteObject(b.into());
                ReleaseDC(Some(hwnd), hdc);
                r
            }
            WM_NCDESTROY => {
                let _ = RemoveWindowSubclass(hwnd, Some(menu_bar_proc), 2);
                DefSubclassProc(hwnd, msg, wp, lp)
            }
            _ => DefSubclassProc(hwnd, msg, wp, lp),
        }
    }
}
