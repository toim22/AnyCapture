//! The vector icons in the egui windows. They are drawn by `snapcore::icons` (GDI+, the same code as the C++ icons)
//! at the exact pixel size on screen and shown as a texture; each window keeps the textures it made, one per icon,
//! size and color.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use egui::{Color32, ColorImage, Painter, Rect, Response, Sense, TextureHandle, TextureOptions, Ui, vec2};

pub use snapcore::icons::Icon;

#[derive(Clone, Default)]
struct Cache(Arc<Mutex<HashMap<(Icon, u32, Color32), TextureHandle>>>);

fn texture(ctx: &egui::Context, icon: Icon, px: u32, c: Color32) -> TextureHandle {
    let cache = ctx.data_mut(|d| d.get_temp_mut_or_default::<Cache>(egui::Id::unique("snapnab-icons")).clone());
    let mut map = cache.0.lock().unwrap();
    map.entry((icon, px, c))
        .or_insert_with(|| {
            let argb = (c.a() as u32) << 24 | (c.r() as u32) << 16 | (c.g() as u32) << 8 | c.b() as u32;
            let img = snapcore::icons::render(icon, px as i32, 0xFF00_0000 | argb & 0x00FF_FFFF);
            let mut rgba = Vec::with_capacity(img.px.len() * 4);
            for p in &img.px {
                // the icon's own alpha times the color's
                let a = ((p >> 24) * c.a() as u32 / 255) as u8;
                rgba.extend_from_slice(&[(p >> 16) as u8, (p >> 8) as u8, *p as u8, a]);
            }
            let ci = ColorImage::from_rgba_unmultiplied([img.w as usize, img.h as usize], &rgba);
            ctx.load_texture(format!("icon-{icon:?}-{px}"), ci, TextureOptions::LINEAR)
        })
        .clone()
}

/// The icon in `r` (points), in the color `c`.
pub fn draw(p: &Painter, icon: Icon, r: Rect, c: Color32) {
    let ppp = p.ctx().pixels_per_point();
    let px = (r.width().min(r.height()) * ppp).round().max(1.0) as u32;
    let side = px as f32 / ppp;
    let at = Rect::from_center_size(r.center(), vec2(side, side));
    // whole pixels, so the icon is not resampled
    let at = Rect::from_min_size(egui::pos2((at.min.x * ppp).round() / ppp, (at.min.y * ppp).round() / ppp), at.size());
    let t = texture(p.ctx(), icon, px, c);
    p.image(t.id(), at, Rect::from_min_max(egui::pos2(0.0, 0.0), egui::pos2(1.0, 1.0)), Color32::WHITE);
}

/// A button with the icon above its label, as in the Editor's toolbar: `on` marks the active tool, `primary` fills it
/// with `primary_fill` (the red Capture button).
/// The width a [`tile`] with this label takes.
pub fn tile_width(ui: &Ui, label: &str) -> f32 {
    let galley = ui.painter().layout_no_wrap(label.to_owned(), egui::FontId::proportional(11.5), Color32::WHITE);
    (galley.size().x + 14.0).max(52.0)
}

pub fn tile(ui: &mut Ui, icon: Icon, label: &str, on: bool, primary: Option<Color32>) -> Response {
    let font = egui::FontId::proportional(11.5);
    let w = tile_width(ui, label);
    let (rect, r) = ui.allocate_exact_size(vec2(w, 52.0), Sense::click());
    if ui.is_rect_visible(rect) {
        let v = ui.visuals();
        let (fill, fg) = if let Some(c) = primary {
            let c = if r.is_pointer_button_down_on() { c.gamma_multiply(0.85) } else if r.hovered() { c.gamma_multiply(1.1) } else { c };
            (c, Color32::WHITE)
        } else if on {
            (v.selection.bg_fill, readable_on(v.selection.bg_fill, v.selection.stroke.color))
        } else if r.hovered() {
            (v.widgets.hovered.weak_bg_fill, v.text_color())
        } else {
            (Color32::TRANSPARENT, v.text_color())
        };
        let p = ui.painter();
        p.rect_filled(rect, v.widgets.inactive.corner_radius.at_least(6), fill);
        draw(p, icon, Rect::from_center_size(egui::pos2(rect.center().x, rect.top() + 18.0), vec2(22.0, 22.0)), fg);
        p.text(egui::pos2(rect.center().x, rect.bottom() - 10.0), egui::Align2::CENTER_CENTER, label, font, fg);
    }
    r
}

/// `fg` when it stands out on `bg`, otherwise white or black (whichever reads better): some themes paint the
/// selection with the same accent as its stroke.
fn readable_on(bg: Color32, fg: Color32) -> Color32 {
    let lum = |c: Color32| 0.2126 * c.r() as f32 + 0.7152 * c.g() as f32 + 0.0722 * c.b() as f32;
    if (lum(bg) - lum(fg)).abs() >= 90.0 {
        fg
    } else if lum(bg) < 150.0 {
        Color32::WHITE
    } else {
        Color32::BLACK
    }
}

/// The icon as an egui image of `size` points, for buttons, labels and menu items.
pub fn image(ctx: &egui::Context, icon: Icon, size: f32, c: Color32) -> egui::Image<'static> {
    let px = (size * ctx.pixels_per_point()).round().max(1.0) as u32;
    let t = texture(ctx, icon, px, c);
    egui::Image::from_texture(egui::load::SizedTexture::new(t.id(), vec2(size, size)))
}

/// A button with a small icon before its text.
pub fn button(ui: &mut Ui, icon: Icon, text: impl Into<egui::WidgetText>) -> Response {
    let c = ui.visuals().text_color();
    let img = image(ui.ctx(), icon, 16.0, c);
    ui.add(egui::Button::image_and_text(img, text).min_size(vec2(0.0, 28.0)))
}
