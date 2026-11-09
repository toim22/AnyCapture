//! Drop-down lists in a window of their own, as Windows (and the C++ AnyCaptureComboList) shows them: the list opens below
//! its box and may go past the edges of the window it belongs to. An egui popup cannot, since egui draws everything
//! inside one window.
//!
//! How it works: a widget asks for a list with [`open`] (the request waits in its Context's data); after that window's
//! frame the host creates a borderless popup window owned by it and draws the list there. A click on an item writes the
//! item's index back into the owner's Context data and repaints the owner, whose widget picks it up with
//! [`take_pick`]. The list closes when it loses the focus (a click anywhere else), with Esc, or after a pick.

use std::time::{Duration, Instant};

use egui::{Align2, Color32, CornerRadius, Id, Key, Rect, Sense, Stroke, pos2, vec2};

use crate::theme::tokens;

/// A row of a list.
#[derive(Clone, Debug)]
pub struct ListItem {
    pub text: String,
    /// the current choice: an accent bar on its left
    pub selected: bool,
    /// a check mark (an option that is on)
    pub check: bool,
    pub separator: bool,
    pub enabled: bool,
    /// its UI Automation id ("IDC_SELECTION.2")
    pub aid: String,
}

impl ListItem {
    pub fn new(text: impl Into<String>) -> ListItem {
        ListItem { text: text.into(), selected: false, check: false, separator: false, enabled: true, aid: String::new() }
    }
    pub fn separator() -> ListItem {
        ListItem { separator: true, enabled: false, ..ListItem::new("") }
    }
    pub fn selected(mut self, on: bool) -> ListItem {
        self.selected = on;
        self
    }
    pub fn check(mut self, on: bool) -> ListItem {
        self.check = on;
        self
    }
    pub fn enabled(mut self, on: bool) -> ListItem {
        self.enabled = on;
        self
    }
    pub fn aid(mut self, aid: impl Into<String>) -> ListItem {
        self.aid = aid.into();
        self
    }
}

pub const ITEM_H: f32 = 30.0;
const SEP_H: f32 = 9.0;
const PAD: f32 = 5.0;

/// A list waiting to be shown.
#[derive(Clone, Debug)]
pub struct Request {
    pub popup_id: Id,
    /// the box it drops from, in the owner's points
    pub anchor: Rect,
    pub min_width: f32,
    pub items: Vec<ListItem>,
    /// lines up with the anchor's right edge (a menu at the end of a row) instead of its left edge
    pub align_end: bool,
}

impl Request {
    /// The list's size in points.
    pub fn size(&self, ctx: &egui::Context) -> egui::Vec2 {
        let font = egui::FontId::proportional(tokens().body);
        let text_w = ctx.fonts_mut(|f| {
            self.items.iter().map(|i| f.layout_no_wrap(i.text.clone(), font.clone(), Color32::WHITE).size().x).fold(0.0, f32::max)
        });
        let check = self.items.iter().any(|i| i.check);
        let w = (text_w + if check { 44.0 } else { 28.0 } + 2.0 * PAD).max(self.min_width);
        let h: f32 = self.items.iter().map(|i| if i.separator { SEP_H } else { ITEM_H }).sum::<f32>() + 2.0 * PAD;
        vec2(w.ceil(), h.ceil())
    }
}

fn req_key() -> Id {
    Id::NULL.with("snapui.droplist.request")
}
fn open_key() -> Id {
    Id::NULL.with("snapui.droplist.open")
}
fn closed_key() -> Id {
    Id::NULL.with("snapui.droplist.closed")
}

fn hosted_key() -> Id {
    Id::NULL.with("snapui.droplist.hosted")
}
fn inline_key() -> Id {
    Id::NULL.with("snapui.droplist.inline")
}

/// The host marks the Contexts of its windows: they show lists in windows of their own. Without a host (the kittest
/// tests) a list is drawn inside the window instead, with the same rows and ids.
pub(crate) fn mark_hosted(ctx: &egui::Context) {
    ctx.data_mut(|d| d.insert_temp(hosted_key(), true));
}

fn hosted(ctx: &egui::Context) -> bool {
    ctx.data(|d| d.get_temp::<bool>(hosted_key())).unwrap_or(false)
}

/// Asks the host to show a list (after this frame).
pub fn open(ctx: &egui::Context, req: Request) {
    let id = req.popup_id;
    let key = if hosted(ctx) { req_key() } else { inline_key() };
    ctx.data_mut(|d| {
        d.insert_temp(key, Some(req));
        d.insert_temp(open_key(), Some(id));
    });
    ctx.request_repaint();
}

/// The request a frame left, for the host.
pub(crate) fn take_request(ctx: &egui::Context) -> Option<Request> {
    ctx.data_mut(|d| d.get_temp_mut_or_default::<Option<Request>>(req_key()).take())
}

/// Whether this list is open now.
pub fn is_open(ctx: &egui::Context, popup_id: Id) -> bool {
    ctx.data(|d| d.get_temp::<Option<Id>>(open_key())).flatten() == Some(popup_id)
}

/// Whether this list closed a moment ago: the click that closed it (on its own box) must not open it again.
pub fn just_closed(ctx: &egui::Context, popup_id: Id) -> bool {
    ctx.data(|d| d.get_temp::<(Id, Instant)>(closed_key()))
        .is_some_and(|(id, t)| id == popup_id && t.elapsed() < Duration::from_millis(250))
}

/// The item the user picked in this list, once. (Without a host, this also draws the open list inside the window.)
pub fn take_pick(ctx: &egui::Context, popup_id: Id) -> Option<usize> {
    if !hosted(ctx) {
        draw_inline(ctx, popup_id);
    }
    ctx.data_mut(|d| d.remove_temp::<usize>(popup_id.with("picked")))
}

/// The list in an egui area over the window, for a Context without a host.
fn draw_inline(ctx: &egui::Context, popup_id: Id) {
    let Some(req) = ctx.data(|d| d.get_temp::<Option<Request>>(inline_key())).flatten().filter(|r| r.popup_id == popup_id) else {
        return;
    };
    let t = tokens();
    let size = req.size(ctx);
    let mut picked = None;
    egui::Area::new(popup_id.with("inline")).order(egui::Order::Foreground).fixed_pos(req.anchor.left_bottom() + vec2(0.0, 4.0)).show(
        ctx,
        |ui| {
            egui::Frame::new().fill(t.card).stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(t.radius_lg))
                .inner_margin(egui::Margin::same(PAD as i8))
                .show(ui, |ui| {
                    ui.spacing_mut().item_spacing = vec2(0.0, 0.0);
                    let w = size.x - 2.0 * PAD;
                    for (i, it) in req.items.iter().enumerate() {
                        if it.separator {
                            let (r, _) = ui.allocate_exact_size(vec2(w, SEP_H), Sense::hover());
                            ui.painter().hline(r.x_range().shrink(6.0), r.center().y, Stroke::new(1.0, t.separator));
                            continue;
                        }
                        let (rect, r) = ui.allocate_exact_size(vec2(w, ITEM_H), Sense::click());
                        let (en, sel, label) = (it.enabled, it.selected, it.text.clone());
                        r.widget_info(|| egui::WidgetInfo::selected(egui::Role::ListBoxOption, en, sel, &label));
                        if !it.aid.is_empty() {
                            let aid = it.aid.clone();
                            ui.ctx().accesskit_node_builder(r.id, |nb| nb.set_author_id(aid));
                        }
                        if r.clicked() && en {
                            picked = Some(i);
                        }
                        paint_row(ui.painter(), rect, it, r.hovered(), r.is_pointer_button_down_on());
                    }
                });
        },
    );
    let escape = ctx.input(|i| i.key_pressed(Key::Escape));
    if picked.is_some() || escape {
        ctx.data_mut(|d| d.insert_temp(inline_key(), None::<Request>));
        finish(ctx, popup_id, picked);
    }
}

/// The owner learns how its list ended (a pick or not) and redraws.
pub(crate) fn finish(owner: &egui::Context, popup_id: Id, picked: Option<usize>) {
    owner.data_mut(|d| {
        if d.get_temp::<Option<Id>>(open_key()).flatten() == Some(popup_id) {
            d.insert_temp(open_key(), None::<Id>);
        }
        d.insert_temp(closed_key(), (popup_id, Instant::now()));
        if let Some(i) = picked {
            d.insert_temp(popup_id.with("picked"), i);
        }
    });
    owner.request_repaint();
}

/// The list window's state.
pub(crate) struct DropState {
    pub owner_ctx: egui::Context,
    pub req: Request,
    pub hot: Option<usize>,
    pub had_focus: bool,
    /// how it ended: Some(None) = closed without a pick
    pub result: Option<Option<usize>>,
}

/// Draws the list window's frame.
pub(crate) fn ui(ui: &mut egui::Ui, d: &mut DropState) {
    let t = tokens();
    let n = d.req.items.len();
    let pickable = |i: usize, items: &[ListItem]| !items[i].separator && items[i].enabled;
    ui.input(|i| {
        if i.key_pressed(Key::Escape) || i.key_pressed(Key::Tab) {
            d.result = Some(None);
        }
        let mut step = |dir: i32| {
            let mut k = d.hot.map_or(if dir > 0 { -1 } else { n as i32 }, |h| h as i32);
            for _ in 0..n {
                k += dir;
                if k < 0 || k >= n as i32 {
                    return;
                }
                if pickable(k as usize, &d.req.items) {
                    d.hot = Some(k as usize);
                    return;
                }
            }
        };
        if i.key_pressed(Key::ArrowDown) {
            step(1);
        }
        if i.key_pressed(Key::ArrowUp) {
            step(-1);
        }
        if (i.key_pressed(Key::Enter) || i.key_pressed(Key::Space)) && d.hot.is_some() {
            d.result = Some(d.hot);
        }
    });
    let frame = egui::Frame::new().fill(t.card).inner_margin(egui::Margin::same(PAD as i8));
    egui::CentralPanel::default().frame(frame).show(ui, |ui| {
        egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
            ui.spacing_mut().item_spacing = vec2(0.0, 0.0);
            let w = ui.available_width();
            for i in 0..n {
                let it = &d.req.items[i];
                if it.separator {
                    let (r, _) = ui.allocate_exact_size(vec2(w, SEP_H), Sense::hover());
                    ui.painter().hline(r.x_range().shrink(6.0), r.center().y, Stroke::new(1.0, t.separator));
                    continue;
                }
                let (rect, r) = ui.allocate_exact_size(vec2(w, ITEM_H), Sense::click());
                let label = it.text.clone();
                let (en, sel) = (it.enabled, it.selected);
                r.widget_info(|| egui::WidgetInfo::selected(egui::Role::ListBoxOption, en, sel, &label));
                if !it.aid.is_empty() {
                    let aid = it.aid.clone();
                    ui.ctx().accesskit_node_builder(r.id, |nb| nb.set_author_id(aid));
                }
                if r.hovered() && it.enabled {
                    d.hot = Some(i);
                }
                if r.clicked() && it.enabled {
                    d.result = Some(Some(i));
                }
                paint_row(ui.painter(), rect, it, d.hot == Some(i), r.is_pointer_button_down_on());
            }
        });
    });
}

/// One row: hover or keyboard highlight, the selected item's light fill and accent bar, the check mark, the text.
pub fn paint_row(p: &egui::Painter, rect: Rect, it: &ListItem, hot: bool, pressed: bool) {
    let t = tokens();
    let rad = CornerRadius::same(t.radius);
    let r = rect.shrink2(vec2(0.0, 1.0));
    if hot && it.enabled {
        p.rect_filled(r, rad, if pressed { t.pressed } else { t.hover });
    } else if it.selected {
        p.rect_filled(r, rad, t.hover);
    }
    if it.selected {
        let bar = Rect::from_center_size(pos2(r.left() + 1.5, r.center().y), vec2(3.0, 16.0));
        p.rect_filled(bar, CornerRadius::same(2), t.accent);
    }
    if it.check {
        let c = pos2(r.left() + 16.0, r.center().y);
        let s = Stroke::new(1.6, t.accent);
        p.line_segment([c + vec2(-4.0, 0.0), c + vec2(-1.2, 3.0)], s);
        p.line_segment([c + vec2(-1.2, 3.0), c + vec2(4.0, -3.2)], s);
    }
    let x = r.left() + if it.check { 32.0 } else { 14.0 };
    let font = egui::FontId::proportional(tokens().body);
    p.text(pos2(x, r.center().y), Align2::LEFT_CENTER, &it.text, font, if it.enabled { t.text } else { t.text_off });
}
