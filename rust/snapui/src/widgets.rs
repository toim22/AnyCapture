//! Windows 11 controls drawn with egui, as the C++ capture window had them (Widgets.cpp): a switch with its label on
//! the left, a combo box with a thin arrow (and, when the choice has settings, a gear inside the box), its drop-down
//! list with the selected item marked by an accent bar, and the "Label      Value ›" row that opens a menu.
//!
//! egui lets a program paint its own widgets: `allocate_exact_size` reserves a rectangle and says whether the mouse is
//! on it or clicked it, and the painter draws shapes there. Each widget also describes itself to UI Automation
//! (`widget_info`), so screen readers and the tests see a check box, a combo box or a list item.

use egui::{Align2, Color32, CornerRadius, FontId, Pos2, Rect, Response, Role, Sense, Stroke, StrokeKind, Ui, WidgetInfo,
           pos2, vec2};

use crate::droplist::{self, ListItem, Request};
use crate::icons::{self, Icon};
use crate::theme::tokens;

/// Height of a combo box or a switch row.
pub const ROW_H: f32 = 32.0;

fn body() -> FontId {
    FontId::proportional(tokens().body)
}

/// A switch row: the label on the left and the switch at the right end of `width`. The whole row is clickable.
pub fn toggle(ui: &mut Ui, on: &mut bool, label: &str, width: f32) -> Response {
    let t = tokens();
    let en = ui.is_enabled();
    let (rect, mut r) = ui.allocate_exact_size(vec2(width, 28.0), Sense::click());
    if r.clicked() {
        *on = !*on;
        r.mark_changed();
    }
    r.widget_info(|| WidgetInfo::selected(Role::CheckBox, en, *on, label));
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        let k = ui.ctx().animate_bool_with_time(r.id, *on, 0.12);
        let (tw, th) = (40.0, 20.0);
        let track = Rect::from_min_size(pos2(rect.right() - tw - 2.0, rect.center().y - th / 2.0), vec2(tw, th));
        let mut fill = lerp(t.switch_off, t.accent, k);
        if r.hovered() && en {
            fill = lerp(fill, if t.dark { Color32::WHITE } else { Color32::BLACK }, 0.08);
        }
        if !en {
            fill = lerp(fill, t.window, 0.55);
        }
        p.rect_filled(track, CornerRadius::same(10), fill);
        let d = th - 6.0 + if r.is_pointer_button_down_on() { 2.0 } else { 0.0 };
        let x = track.left() + 3.0 + d / 2.0 + k * (tw - 6.0 - d);
        let knob = if *on && t.dark { t.on_accent } else { Color32::WHITE };
        p.circle_filled(pos2(x, track.center().y + 0.6), d / 2.0, Color32::from_black_alpha(40));
        p.circle_filled(pos2(x, track.center().y), d / 2.0, knob);
        if r.has_focus() {
            p.rect_stroke(track.expand(3.0), CornerRadius::same(13), Stroke::new(1.5, t.text), StrokeKind::Outside);
        }
        let text_rect = Rect::from_min_max(rect.min, pos2(track.left() - 8.0, rect.bottom()));
        let g = p.layout_no_wrap(label.to_owned(), body(), if en { t.text } else { t.text_off });
        p.with_clip_rect(text_rect).galley(pos2(rect.left(), rect.center().y - g.size().y / 2.0), g, t.text);
    }
    r
}

fn lerp(a: Color32, b: Color32, k: f32) -> Color32 {
    let m = |x: u8, y: u8| (x as f32 + (y as f32 - x as f32) * k).round() as u8;
    Color32::from_rgb(m(a.r(), b.r()), m(a.g(), b.g()), m(a.b(), b.b()))
}

/// A thin "v" (or "^") arrow, as Windows 11 draws in its combo boxes.
pub fn chevron(p: &egui::Painter, c: Pos2, w: f32, color: Color32, up: bool) {
    let h = w / 4.0 * if up { -1.0 } else { 1.0 };
    let s = Stroke::new(1.3, color);
    p.line_segment([pos2(c.x - w / 2.0, c.y - h), pos2(c.x, c.y + h)], s);
    p.line_segment([pos2(c.x, c.y + h), pos2(c.x + w / 2.0, c.y - h)], s);
}

/// What a combo box returned this frame.
pub struct ComboOut {
    /// the box (a click opens the list)
    pub response: Response,
    /// the gear inside the box, when it has one
    pub gear: Option<Response>,
    /// the item the user picked in the list (its index)
    pub picked: Option<usize>,
}

/// A combo box `width` wide showing `text`; `gear` adds the settings gear inside the box, at the right (Some(false)
/// draws it grayed). `items` makes the list's rows when it opens (a click on the box, or `open_now`); the list is a
/// window of its own (see `droplist`).
pub fn combo(ui: &mut Ui, id_salt: impl egui::AsIdSalt, text: &str, width: f32, gear: Option<bool>,
             items: impl FnOnce() -> Vec<ListItem>, open_now: bool) -> ComboOut {
    let t = tokens();
    let en = ui.is_enabled();
    let id = ui.make_persistent_id(id_salt);
    let popup_id = id.with("list");
    let (rect, _) = ui.allocate_exact_size(vec2(width, ROW_H), Sense::hover());
    let gw = if gear.is_some() { 32.0 } else { 0.0 };
    let main = Rect::from_min_max(rect.min, pos2(rect.right() - gw, rect.bottom()));
    let response = ui.interact(main, id, Sense::click());
    let gear_r = gear.map(|_| ui.interact(Rect::from_min_max(pos2(main.right(), rect.top()), rect.max), id.with("gear"), Sense::click()));
    let ctx = ui.ctx().clone();
    if en && (open_now || (response.clicked() && !droplist::just_closed(&ctx, popup_id))) {
        droplist::open(&ctx, Request { popup_id, anchor: rect, min_width: rect.width(), items: items(), align_end: false });
    }
    let open = droplist::is_open(&ctx, popup_id);
    response.widget_info(|| {
        let mut info = WidgetInfo::new(Role::ComboBox);
        info.enabled = en;
        info.current_text_value = Some(text.to_owned());
        info
    });
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        let hover = (response.hovered() || gear_r.as_ref().is_some_and(|g| g.hovered())) && en;
        let fill = if !en { t.window } else if hover && !open { t.hover } else { t.field };
        let rad = CornerRadius::same(t.radius + 1);
        p.rect(rect, rad, fill, Stroke::new(1.0, t.field_border), StrokeKind::Inside);
        if en && (open || response.has_focus()) {
            // the accent line at the bottom, as Windows 11 marks the control in use
            let y = rect.bottom() - 1.0;
            p.line_segment([pos2(rect.left() + 3.0, y), pos2(rect.right() - 3.0, y)], Stroke::new(2.0, t.accent));
        }
        let fg = if en { t.text } else { t.text_off };
        chevron(p, pos2(main.right() - 16.0, rect.center().y), 9.0, if en { t.text_dim } else { t.text_off }, false);
        let text_rect = Rect::from_min_max(pos2(rect.left() + 11.0, rect.top()), pos2(main.right() - 30.0, rect.bottom()));
        let g = p.layout_no_wrap(text.to_owned(), body(), fg);
        p.with_clip_rect(text_rect).galley(pos2(text_rect.left(), rect.center().y - g.size().y / 2.0), g, fg);
        if let (Some(on), Some(g)) = (gear, &gear_r) {
            let gr = g.rect.shrink2(vec2(3.0, 4.0));
            if en && on && g.hovered() {
                p.rect_filled(gr, CornerRadius::same(t.radius), t.pressed);
            }
            let sep_x = main.right() + 0.5;
            p.line_segment([pos2(sep_x, rect.top() + 8.0), pos2(sep_x, rect.bottom() - 8.0)], Stroke::new(1.0, t.field_border));
            let c = if en && on { t.text_dim } else { t.text_off };
            icons::draw(p, Icon::Gear, Rect::from_center_size(g.rect.center(), vec2(16.0, 16.0)), c);
        }
    }
    ComboOut { response, gear: gear_r, picked: droplist::take_pick(&ctx, popup_id) }
}

/// A combo box over a list of names; `sel` changes when the user picks one (the response is then marked changed).
pub fn combo_index(ui: &mut Ui, id_salt: impl egui::AsIdSalt, sel: &mut usize, items: &[String], width: f32) -> Response {
    let cur = (*sel).min(items.len().saturating_sub(1));
    let text = items.get(cur).cloned().unwrap_or_default();
    let rows = || items.iter().enumerate().map(|(i, s)| ListItem::new(s.clone()).selected(i == cur)).collect();
    let mut out = combo(ui, id_salt, &text, width, None, rows, false);
    if let Some(i) = out.picked
        && i != *sel
    {
        *sel = i;
        out.response.mark_changed();
    }
    out.response
}

/// A menu in a list window under `anchor` (a gear, a "Value ›" row): it opens on a click and returns the picked row.
pub fn menu_list(ui: &Ui, anchor: &Response, items: impl FnOnce() -> Vec<ListItem>, align_end: bool) -> Option<usize> {
    let ctx = ui.ctx();
    let popup_id = anchor.id.with("menu");
    if anchor.clicked() && !droplist::just_closed(ctx, popup_id) {
        droplist::open(ctx, Request { popup_id, anchor: anchor.rect, min_width: 0.0, items: items(), align_end });
    }
    droplist::take_pick(ctx, popup_id)
}

/// A row of an egui menu drawn like a list row (the selected one with an accent bar, `check` with a check mark).
pub fn list_item_ex(ui: &mut Ui, selected: bool, check: bool, text: &str) -> Response {
    let en = ui.is_enabled();
    let w = ui.available_width().max(ui.min_rect().width());
    let (rect, r) = ui.allocate_exact_size(vec2(w, droplist::ITEM_H), Sense::click());
    r.widget_info(|| WidgetInfo::selected(Role::ListBoxOption, en, selected, text));
    if ui.is_rect_visible(rect) {
        let it = ListItem::new(text).selected(selected).check(check).enabled(en);
        droplist::paint_row(ui.painter(), rect, &it, r.hovered(), r.is_pointer_button_down_on());
    }
    r
}

/// "Label            Value ›": the value in the accent color at the right end of `width`; a click opens a menu.
pub fn link_row(ui: &mut Ui, label: &str, value: &str, width: f32) -> Response {
    let t = tokens();
    let en = ui.is_enabled();
    let (rect, r) = ui.allocate_exact_size(vec2(width, 28.0), Sense::click());
    r.widget_info(|| {
        let mut info = WidgetInfo::labeled(Role::Button, en, if label.is_empty() { value } else { label });
        info.current_text_value = Some(value.to_owned());
        info
    });
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        let vc = if !en { t.text_off } else { t.accent };
        let g = p.layout_no_wrap(value.to_owned(), body(), vc);
        let arrow_x = rect.right() - 8.0;
        let vx = arrow_x - 10.0 - g.size().x;
        if en && (r.hovered() || r.is_pointer_button_down_on()) {
            let hr = Rect::from_min_max(pos2(vx - 6.0, rect.top() + 1.0), pos2(rect.right(), rect.bottom() - 1.0));
            p.rect_filled(hr, CornerRadius::same(t.radius), if r.is_pointer_button_down_on() { t.pressed } else { t.hover });
        }
        p.galley(pos2(vx, rect.center().y - g.size().y / 2.0), g, vc);
        let s = Stroke::new(1.6, vc);
        let c = pos2(arrow_x, rect.center().y);
        p.line_segment([c + vec2(-2.0, -4.5), c + vec2(2.0, 0.0)], s);
        p.line_segment([c + vec2(2.0, 0.0), c + vec2(-2.0, 4.5)], s);
        p.text(pos2(rect.left(), rect.center().y), Align2::LEFT_CENTER, label, body(), if en { t.text } else { t.text_off });
    }
    r
}

/// A flat button: an icon (in the accent color) and its text; `menu` adds a small arrow after the text.
pub fn flat_button(ui: &mut Ui, icon: Option<Icon>, text: &str, accent_text: bool, menu: bool) -> Response {
    let t = tokens();
    let en = ui.is_enabled();
    let font = body();
    let g = ui.painter().layout_no_wrap(text.to_owned(), font, t.text);
    let iw = if icon.is_some() { 16.0 + if text.is_empty() { 0.0 } else { 7.0 } } else { 0.0 };
    let mw = if menu { 13.0 } else { 0.0 };
    let w = 20.0 + iw + g.size().x + mw;
    let (rect, r) = ui.allocate_exact_size(vec2(w, 30.0), Sense::click());
    r.widget_info(|| WidgetInfo::labeled(Role::Button, en, text));
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        if en && (r.hovered() || r.is_pointer_button_down_on()) {
            p.rect_filled(rect, CornerRadius::same(t.radius), if r.is_pointer_button_down_on() { t.pressed } else { t.hover });
        }
        let fg = if !en { t.text_off } else if accent_text { t.accent } else { t.text };
        let mut x = rect.left() + 10.0;
        if let Some(i) = icon {
            icons::draw(p, i, Rect::from_min_size(pos2(x, rect.center().y - 8.0), vec2(16.0, 16.0)), if en { t.accent } else { t.text_off });
            x += iw;
        }
        p.text(pos2(x, rect.center().y), Align2::LEFT_CENTER, text, body(), fg);
        if menu {
            chevron(p, pos2(rect.right() - 13.0, rect.center().y + 1.0), 8.0, fg, false);
        }
    }
    r
}
