//! C ABI of the editor's objects and painters (snapcore.h): the C++ hands an object as a SnapcoreObj (pointers into its
//! DrawObj, valid during the call), and its GDI+ objects (Graphics, GraphicsPath, Pen) by their native pointers, so
//! the Rust draws into them with the same calls the C++ made.

use std::ffi::{CStr, c_char, c_void};
use std::mem::ManuallyDrop;

use windows::Win32::Graphics::GdiPlus::{PointF, RectF};

use crate::doc::{self, DrawObj, GradStop, Gradient, ObjType, Style};
use crate::ffi::{Decoded, ImageIn, SnapBuf, SnapWStr, guard, image_in, path_arg, wide_out};
use crate::gdip::{Graphics, Path, Pen, rect};
use crate::image::{Image, Rect};
use crate::render;
use crate::serial::{self, DocFile};
use crate::stamps;
use crate::wide;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CPoint {
    pub x: f32,
    pub y: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CRect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CGradStop {
    pub pos: f32,
    pub color: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct CGradient {
    pub kind: i32,
    pub angle: f32,
    pub stops: *const CGradStop,
    pub count: usize,
}

/// Style (Document.h), field by field; the flags as int32 (0 / 1).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct CStyle {
    pub stroke: u32,
    pub fill: u32,
    pub text_color: u32,
    pub width: f32,
    pub opacity: i32,
    pub shadow: i32,
    pub shadow_dir: i32,
    pub shadow_color: u32,
    pub shadow_opacity: i32,
    pub shadow_blur: i32,
    pub shadow_dist: i32,
    pub font: *const u16,
    pub font_size: f32,
    pub bold: i32,
    pub italic: i32,
    pub underline: i32,
    pub align_h: i32,
    pub align_v: i32,
    pub padding: f32,
    pub arrow_start: i32,
    pub arrow_end: i32,
    pub start_size: f32,
    pub end_size: f32,
    pub dash: i32,
    pub bezier: i32,
    pub callout_shape: i32,
    pub shaded: i32,
    pub tail_color: u32,
    pub tail_width: f32,
    pub shape_kind: i32,
    pub step_shape: i32,
    pub step_type: i32,
    pub magnify_shape: i32,
    pub magnify_scale: i32,
    pub magnify_link: i32,
    pub hide_kind: i32,
    pub antialias: i32,
    pub amount: i32,
    pub line_spacing: f32,
    pub step_outline: u32,
    pub step_outline_width: f32,
    pub area_shape: i32,
    pub grad: CGradient,
    pub bg_blur: i32,
    pub bg_blur_amount: i32,
    pub loupe: i32,
    pub loupe_handle: i32,
    pub text_only: i32,
    pub snap_text: i32,
    pub sketch: i32,
    pub sketch_seed: i32,
    pub vertical: i32,
    pub text_on_path: i32,
}

/// DrawObj (Document.h): arrays as pointer + count, strings nul-terminated, children as an array of objects.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct CObj {
    pub kind: i32,
    pub p1: CPoint,
    pub p2: CPoint,
    pub tail: CPoint,
    pub ctrl: CPoint,
    pub pts: *const CPoint,
    pub pts_count: usize,
    pub text: *const u16,
    pub step_value: i32,
    pub style: CStyle,
    pub picture: *const u32,
    pub pic_w: i32,
    pub pic_h: i32,
    pub children: *const CObj,
    pub child_count: usize,
    pub name: *const u16,
    pub hidden: i32,
    pub locked: i32,
    pub floating: i32,
    pub text_boxes: *const CRect,
    pub text_box_count: usize,
    pub pressure: *const f32,
    pub pressure_count: usize,
    pub pic_dpi: f32,
}

unsafe fn slice<'a, T>(p: *const T, n: usize) -> &'a [T] {
    if p.is_null() || n == 0 { &[] } else { unsafe { std::slice::from_raw_parts(p, n) } }
}

fn pf(p: CPoint) -> PointF {
    PointF { X: p.x, Y: p.y }
}
fn rf(r: CRect) -> RectF {
    rect(r.x, r.y, r.w, r.h)
}
fn cr(r: RectF) -> CRect {
    CRect { x: r.X, y: r.Y, w: r.Width, h: r.Height }
}

unsafe fn gradient(g: &CGradient) -> Gradient {
    Gradient {
        kind: g.kind,
        angle: g.angle,
        stops: unsafe { slice(g.stops, g.count) }.iter().map(|s| GradStop { pos: s.pos, color: s.color }).collect(),
    }
}

unsafe fn style(c: &CStyle) -> Style {
    Style {
        stroke: c.stroke,
        fill: c.fill,
        text_color: c.text_color,
        width: c.width,
        opacity: c.opacity,
        shadow: c.shadow != 0,
        shadow_dir: c.shadow_dir,
        shadow_color: c.shadow_color,
        shadow_opacity: c.shadow_opacity,
        shadow_blur: c.shadow_blur,
        shadow_dist: c.shadow_dist,
        font: unsafe { wide::from_ptr(c.font) }.map(String::from_utf16_lossy).unwrap_or_default(),
        font_size: c.font_size,
        bold: c.bold != 0,
        italic: c.italic != 0,
        underline: c.underline != 0,
        align_h: c.align_h,
        align_v: c.align_v,
        padding: c.padding,
        arrow_start: c.arrow_start,
        arrow_end: c.arrow_end,
        start_size: c.start_size,
        end_size: c.end_size,
        dash: c.dash,
        bezier: c.bezier != 0,
        callout_shape: c.callout_shape,
        shaded: c.shaded != 0,
        tail_color: c.tail_color,
        tail_width: c.tail_width,
        shape_kind: c.shape_kind,
        step_shape: c.step_shape,
        step_type: c.step_type,
        magnify_shape: c.magnify_shape,
        magnify_scale: c.magnify_scale,
        magnify_link: c.magnify_link != 0,
        hide_kind: c.hide_kind,
        antialias: c.antialias != 0,
        amount: c.amount,
        line_spacing: c.line_spacing,
        step_outline: c.step_outline,
        step_outline_width: c.step_outline_width,
        area_shape: c.area_shape,
        grad: unsafe { gradient(&c.grad) },
        bg_blur: c.bg_blur != 0,
        bg_blur_amount: c.bg_blur_amount,
        loupe: c.loupe != 0,
        loupe_handle: c.loupe_handle != 0,
        text_only: c.text_only != 0,
        snap_text: c.snap_text != 0,
        sketch: c.sketch != 0,
        sketch_seed: c.sketch_seed,
        vertical: c.vertical != 0,
        text_on_path: c.text_on_path != 0,
    }
}

/// The object copied from the C++ DrawObj.
pub(crate) unsafe fn obj(c: &CObj) -> DrawObj {
    unsafe { obj_with(c, true) }
}

/// The object without its picture's pixels, for what only measures it (bounds, hit-testing).
unsafe fn shape(c: &CObj) -> DrawObj {
    unsafe { obj_with(c, false) }
}

unsafe fn obj_with(c: &CObj, pixels: bool) -> DrawObj {
    let picture = if !pixels || c.picture.is_null() || c.pic_w <= 0 || c.pic_h <= 0 {
        None
    } else {
        let px = unsafe { slice(c.picture, c.pic_w as usize * c.pic_h as usize) }.to_vec();
        Some(Image { w: c.pic_w as u32, h: c.pic_h as u32, px, dpi: c.pic_dpi, meta: None })
    };
    DrawObj {
        kind: ObjType::from_i32(c.kind),
        p1: pf(c.p1),
        p2: pf(c.p2),
        pts: unsafe { slice(c.pts, c.pts_count) }.iter().map(|&p| pf(p)).collect(),
        tail: pf(c.tail),
        ctrl: pf(c.ctrl),
        text: unsafe { wide::from_ptr(c.text) }.map(|t| t.to_vec()).unwrap_or_default(),
        step_value: c.step_value,
        style: unsafe { style(&c.style) },
        picture,
        children: unsafe { slice(c.children, c.child_count) }.iter().map(|k| unsafe { obj_with(k, pixels) }).collect(),
        name: unsafe { wide::from_ptr(c.name) }.map(String::from_utf16_lossy).unwrap_or_default(),
        hidden: c.hidden != 0,
        locked: c.locked != 0,
        floating: c.floating != 0,
        text_boxes: unsafe { slice(c.text_boxes, c.text_box_count) }.iter().map(|&r| rf(r)).collect(),
        pressure: unsafe { slice(c.pressure, c.pressure_count) }.to_vec(),
    }
}

/// The caller's pixels seen as an Image, changed in place (no copy: the painters run once per object per redraw).
/// The Vec is never grown nor freed: it only borrows the C++ pixels for the call.
struct Canvas(ManuallyDrop<Image>);

impl Canvas {
    unsafe fn new(px: *mut u32, w: i32, h: i32) -> Option<Canvas> {
        if px.is_null() || w <= 0 || h <= 0 {
            return None;
        }
        let n = w as usize * h as usize;
        let v = unsafe { Vec::from_raw_parts(px, n, n) };
        Some(Canvas(ManuallyDrop::new(Image { w: w as u32, h: h as u32, px: v, dpi: 0.0, meta: None })))
    }
}

fn image_out(img: Image) -> *mut Decoded {
    if img.is_empty() { std::ptr::null_mut() } else { Box::into_raw(Box::new(Decoded::new(img))) }
}

/// RenderObject: `graphics` is a GpGraphics over the same pixels (the C++ flushed nothing yet; the painter flushes).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_render_object(graphics: *mut c_void, px: *mut u32, w: i32, h: i32, o: *const CObj, hide_text: bool) {
    guard((), || {
        let (Some(o), Some(mut canvas)) = (unsafe { o.as_ref() }, unsafe { Canvas::new(px, w, h) }) else { return };
        if graphics.is_null() {
            return;
        }
        let g = unsafe { Graphics::borrow(graphics) };
        render::render_object(&g, &mut canvas.0, &unsafe { obj(o) }, hide_text);
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_shape_path(path: *mut c_void, kind: i32, r: CRect) {
    guard((), || {
        if !path.is_null() {
            render::shape_path(&unsafe { Path::borrow(path) }, kind, rf(r));
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_callout_body_path(path: *mut c_void, shape: i32, r: CRect) {
    guard((), || {
        if !path.is_null() {
            render::callout_body_path(&unsafe { Path::borrow(path) }, shape, rf(r));
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_area_path(path: *mut c_void, o: *const CObj) {
    guard((), || {
        if let (false, Some(o)) = (path.is_null(), unsafe { o.as_ref() }) {
            render::area_path(&unsafe { Path::borrow(path) }, &unsafe { shape(o) });
        }
    })
}

/// 0 the object's bounds, 1 the callout's text area, 2 the arrow label's box, 3 the magnifier with its loupe handle.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_obj_rect(o: *const CObj, which: i32) -> CRect {
    let zero = CRect { x: 0.0, y: 0.0, w: 0.0, h: 0.0 };
    guard(zero, || {
        let Some(o) = (unsafe { o.as_ref() }) else { return zero };
        let o = unsafe { shape(o) };
        cr(match which {
            1 => render::callout_text_rect(&o),
            2 => render::arrow_label_rect(&o),
            3 => render::magnifier_extra(&o),
            _ => o.bounds(),
        })
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_text_block_height(o: *const CObj, width: f32) -> f32 {
    guard(0.0, || unsafe { o.as_ref() }.map_or(0.0, |o| render::text_block_height(&unsafe { shape(o) }, width)))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_stamp_aspect(id: *const u16) -> f32 {
    guard(1.0, || render::stamp_aspect(&unsafe { wide::from_ptr(id) }.map(String::from_utf16_lossy).unwrap_or_default()))
}

/// 0 a stamp's image (emoji, file, label or cursor, cached by size), 1 an emoji, 2 a cursor stamp; null when empty.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_stamp_image(id: *const u16, px: i32, which: i32) -> *mut Decoded {
    guard(std::ptr::null_mut(), || {
        let id = unsafe { wide::from_ptr(id) }.map(String::from_utf16_lossy).unwrap_or_default();
        image_out(match which {
            1 => stamps::emoji(&id, px),
            2 => stamps::cursor(&id, px),
            _ => render::stamp_image(&id, px),
        })
    })
}

/// FillBrush: a new GpBrush (the C++ wraps it and deletes it).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_fill_brush(color: u32, g: *const CGradient, r: CRect, opacity: i32) -> *mut c_void {
    guard(std::ptr::null_mut(), || {
        let grad = unsafe { g.as_ref() }.map(|g| unsafe { gradient(g) }).unwrap_or_default();
        render::fill_brush(color, &grad, rf(r), opacity).into_raw() as *mut c_void
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_fill_gradient_mask(px: *mut u32, w: i32, h: i32, mask: *const u8, mask_len: usize, b: Rect,
                                                     g: *const CGradient, opacity: i32) {
    guard((), || {
        let (Some(mut canvas), Some(g)) = (unsafe { Canvas::new(px, w, h) }, unsafe { g.as_ref() }) else { return };
        let mask = unsafe { slice(mask, mask_len) };
        if !mask.is_empty() && mask.len() < w as usize * h as usize {
            return;
        }
        render::fill_gradient_mask(&mut canvas.0, mask, b, &unsafe { gradient(g) }, opacity);
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_draw_cap_glyph(graphics: *mut c_void, cap: i32, r: CRect, color: u32, at_start: bool) {
    guard((), || {
        if !graphics.is_null() {
            render::draw_cap_glyph(&unsafe { Graphics::borrow(graphics) }, cap, rf(r), color, at_start);
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_apply_dash(pen: *mut c_void, dash: i32) {
    guard((), || {
        if !pen.is_null() {
            render::apply_dash(&unsafe { Pen::borrow(pen) }, dash);
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_step_label(value: i32, kind: i32, out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &doc::step_label(value, kind).encode_utf16().collect::<Vec<u16>>()))
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_cap_length(w: f32, size: f32) -> f32 {
    doc::cap_length(w, size)
}

// ---------------------------------------------------------------- objects handed back to the C++ (files, clipboard)
fn wz(s: impl IntoIterator<Item = u16>) -> Vec<u16> {
    s.into_iter().chain(std::iter::once(0)).collect()
}

/// An object as the C++ reads it: a CObj pointing into the strings and arrays kept here and into the DrawObj's own
/// pixels and pressure, valid while the list that holds both lives.
struct View {
    c: CObj,
    _strings: [Vec<u16>; 3],
    _pts: Vec<CPoint>,
    _boxes: Vec<CRect>,
    _stops: Vec<CGradStop>,
    _kids: Vec<View>,
    _kids_c: Vec<CObj>,
}

fn view(o: &DrawObj) -> View {
    let text = wz(o.text.iter().copied());
    let font = wz(o.style.font.encode_utf16());
    let name = wz(o.name.encode_utf16());
    let pts: Vec<CPoint> = o.pts.iter().map(|p| CPoint { x: p.X, y: p.Y }).collect();
    let boxes: Vec<CRect> = o.text_boxes.iter().map(|&r| cr(r)).collect();
    let stops: Vec<CGradStop> = o.style.grad.stops.iter().map(|s| CGradStop { pos: s.pos, color: s.color }).collect();
    let kids: Vec<View> = o.children.iter().map(view).collect();
    let kids_c: Vec<CObj> = kids.iter().map(|k| k.c).collect();
    let s = &o.style;
    let cp = |p: PointF| CPoint { x: p.X, y: p.Y };
    let (picture, pic_w, pic_h, pic_dpi) = match &o.picture {
        Some(p) if !p.is_empty() => (p.px.as_ptr(), p.w as i32, p.h as i32, p.dpi),
        _ => (std::ptr::null(), 0, 0, 0.0),
    };
    let c = CObj {
        kind: o.kind as i32,
        p1: cp(o.p1),
        p2: cp(o.p2),
        tail: cp(o.tail),
        ctrl: cp(o.ctrl),
        pts: pts.as_ptr(),
        pts_count: pts.len(),
        text: text.as_ptr(),
        step_value: o.step_value,
        style: CStyle {
            stroke: s.stroke,
            fill: s.fill,
            text_color: s.text_color,
            width: s.width,
            opacity: s.opacity,
            shadow: s.shadow as i32,
            shadow_dir: s.shadow_dir,
            shadow_color: s.shadow_color,
            shadow_opacity: s.shadow_opacity,
            shadow_blur: s.shadow_blur,
            shadow_dist: s.shadow_dist,
            font: font.as_ptr(),
            font_size: s.font_size,
            bold: s.bold as i32,
            italic: s.italic as i32,
            underline: s.underline as i32,
            align_h: s.align_h,
            align_v: s.align_v,
            padding: s.padding,
            arrow_start: s.arrow_start,
            arrow_end: s.arrow_end,
            start_size: s.start_size,
            end_size: s.end_size,
            dash: s.dash,
            bezier: s.bezier as i32,
            callout_shape: s.callout_shape,
            shaded: s.shaded as i32,
            tail_color: s.tail_color,
            tail_width: s.tail_width,
            shape_kind: s.shape_kind,
            step_shape: s.step_shape,
            step_type: s.step_type,
            magnify_shape: s.magnify_shape,
            magnify_scale: s.magnify_scale,
            magnify_link: s.magnify_link as i32,
            hide_kind: s.hide_kind,
            antialias: s.antialias as i32,
            amount: s.amount,
            line_spacing: s.line_spacing,
            step_outline: s.step_outline,
            step_outline_width: s.step_outline_width,
            area_shape: s.area_shape,
            grad: CGradient { kind: s.grad.kind, angle: s.grad.angle, stops: stops.as_ptr(), count: stops.len() },
            bg_blur: s.bg_blur as i32,
            bg_blur_amount: s.bg_blur_amount,
            loupe: s.loupe as i32,
            loupe_handle: s.loupe_handle as i32,
            text_only: s.text_only as i32,
            snap_text: s.snap_text as i32,
            sketch: s.sketch as i32,
            sketch_seed: s.sketch_seed,
            vertical: s.vertical as i32,
            text_on_path: s.text_on_path as i32,
        },
        picture,
        pic_w,
        pic_h,
        children: kids_c.as_ptr(),
        child_count: kids_c.len(),
        name: name.as_ptr(),
        hidden: o.hidden as i32,
        locked: o.locked as i32,
        floating: o.floating as i32,
        text_boxes: boxes.as_ptr(),
        text_box_count: boxes.len(),
        pressure: o.pressure.as_ptr(),
        pressure_count: o.pressure.len(),
        pic_dpi,
    };
    View { c, _strings: [text, font, name], _pts: pts, _boxes: boxes, _stops: stops, _kids: kids, _kids_c: kids_c }
}

/// A document's details as the C++ reads them (pointers into the list).
#[repr(C)]
pub struct CDocInfo {
    pub capture_mode: *const u16,
    pub window_title: *const u16,
    pub application: *const u16,
    pub created: *const u16,
    pub tags: *const u16,
    pub next_step: i32,
    pub guides_x: *const f32,
    pub guides_x_count: usize,
    pub guides_y: *const f32,
    pub guides_y_count: usize,
}

struct DocKeep {
    strings: [Vec<u16>; 5],
    guides_x: Vec<f32>,
    guides_y: Vec<f32>,
    next_step: i32,
    base: Option<Image>,
}

/// Objects snapcore read (a .snap or the clipboard), with what the C++ needs to copy them out.
pub struct ObjList {
    objs: Vec<DrawObj>,
    views: Vec<View>,
    source: Option<Vec<u16>>,
    doc: Option<DocKeep>,
}

impl ObjList {
    fn new(objs: Vec<DrawObj>) -> Box<ObjList> {
        let mut l = Box::new(ObjList { objs, views: Vec::new(), source: None, doc: None });
        l.views = l.objs.iter().map(view).collect();
        l
    }
}

unsafe fn objs_in(objs: *const CObj, n: usize) -> Vec<DrawObj> {
    unsafe { slice(objs, n) }.iter().map(|o| unsafe { obj(o) }).collect()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_objs_count(l: *const ObjList) -> usize {
    unsafe { l.as_ref() }.map_or(0, |l| l.views.len())
}

/// The i-th object, valid until snapcore_objs_free; null past the end.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_objs_get(l: *const ObjList, i: usize) -> *const CObj {
    unsafe { l.as_ref() }.and_then(|l| l.views.get(i)).map_or(std::ptr::null(), |v| &v.c)
}

/// The clipboard text's source document (nul-terminated); null when the text was not Snapnab objects.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_objs_source(l: *const ObjList) -> *const u16 {
    unsafe { l.as_ref() }.and_then(|l| l.source.as_ref()).map_or(std::ptr::null(), |s| s.as_ptr())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_objs_free(l: *mut ObjList) {
    if !l.is_null() {
        drop(unsafe { Box::from_raw(l) });
    }
}

/// The objects as the Editor's clipboard text (UTF-8, into *out: free it with snapcore_buf_free).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_objects_to_clip(objs: *const CObj, n: usize, source: *const u16, out: *mut SnapBuf) -> bool {
    guard(false, || {
        if out.is_null() {
            return false;
        }
        let source = unsafe { wide::from_ptr(source) }.map(String::from_utf16_lossy).unwrap_or_default();
        let bytes = serial::objects_to_clip(&unsafe { objs_in(objs, n) }, &source).into_bytes().into_boxed_slice();
        let len = bytes.len();
        unsafe { *out = SnapBuf { data: Box::into_raw(bytes) as *mut u8, len } };
        true
    })
}

/// The objects of a clipboard text (UTF-8); free the list with snapcore_objs_free.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_objects_from_clip(text: *const u8, len: usize) -> *mut ObjList {
    guard(std::ptr::null_mut(), || {
        let (objs, source) = serial::objects_from_clip(unsafe { slice(text, len) });
        let mut l = ObjList::new(objs);
        l.source = source.map(|s| wz(s.encode_utf16()));
        Box::into_raw(l)
    })
}

/// Document::Save: the base, the objects and the details into a .snap (`version` in its index).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_doc_save(path: *const u16, len: usize, info: *const CDocInfo, base: *const ImageIn,
                                           objs: *const CObj, n: usize, version: *const u16) -> bool {
    guard(false, || {
        let (Some(path), Some(i), Some(base)) = (unsafe { path_arg(path, len) }, unsafe { info.as_ref() }, unsafe { base.as_ref() })
        else {
            return false;
        };
        let Some(base) = (unsafe { image_in(base) }) else { return false };
        let text = |p: *const u16| unsafe { wide::from_ptr(p) }.map(String::from_utf16_lossy).unwrap_or_default();
        let doc = DocFile {
            base,
            objs: unsafe { objs_in(objs, n) },
            capture_mode: text(i.capture_mode),
            window_title: text(i.window_title),
            application: text(i.application),
            created: text(i.created),
            tags: text(i.tags),
            next_step: i.next_step,
            guides_x: unsafe { slice(i.guides_x, i.guides_x_count) }.to_vec(),
            guides_y: unsafe { slice(i.guides_y, i.guides_y_count) }.to_vec(),
        };
        doc.save(&path, &text(version))
    })
}

/// Document::Load of a .snap; null when it cannot be read. Free the list with snapcore_objs_free.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_doc_load(path: *const u16, len: usize) -> *mut ObjList {
    guard(std::ptr::null_mut(), || {
        let Some(doc) = unsafe { path_arg(path, len) }.and_then(|p| DocFile::load(&p)) else { return std::ptr::null_mut() };
        let mut l = ObjList::new(doc.objs);
        l.doc = Some(DocKeep {
            strings: [&doc.capture_mode, &doc.window_title, &doc.application, &doc.created, &doc.tags].map(|s| wz(s.encode_utf16())),
            guides_x: doc.guides_x,
            guides_y: doc.guides_y,
            next_step: doc.next_step,
            base: Some(doc.base),
        });
        Box::into_raw(l)
    })
}

/// The details of a document snapcore_doc_load read (pointers valid until snapcore_objs_free).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_doc_info(l: *const ObjList, out: *mut CDocInfo) -> bool {
    let (Some(d), false) = (unsafe { l.as_ref() }.and_then(|l| l.doc.as_ref()), out.is_null()) else { return false };
    let s = &d.strings;
    unsafe {
        *out = CDocInfo {
            capture_mode: s[0].as_ptr(),
            window_title: s[1].as_ptr(),
            application: s[2].as_ptr(),
            created: s[3].as_ptr(),
            tags: s[4].as_ptr(),
            next_step: d.next_step,
            guides_x: d.guides_x.as_ptr(),
            guides_x_count: d.guides_x.len(),
            guides_y: d.guides_y.as_ptr(),
            guides_y_count: d.guides_y.len(),
        }
    };
    true
}

/// The base image of a loaded document, handed over once (null after that).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_doc_take_base(l: *mut ObjList) -> *mut Decoded {
    guard(std::ptr::null_mut(), || {
        unsafe { l.as_mut() }.and_then(|l| l.doc.as_mut()).and_then(|d| d.base.take()).map_or(std::ptr::null_mut(), image_out)
    })
}

/// DrawObj::HitTest.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_hit_test(o: *const CObj, x: f32, y: f32, tol: f32) -> bool {
    guard(false, || unsafe { o.as_ref() }.is_some_and(|o| unsafe { shape(o) }.hit_test(PointF { X: x, Y: y }, tol)))
}

/// GradStopsToString (UTF-8, into *out: free it with snapcore_buf_free).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_grad_stops_string(g: *const CGradient, out: *mut SnapBuf) -> bool {
    guard(false, || {
        let (Some(g), false) = (unsafe { g.as_ref() }, out.is_null()) else { return false };
        let bytes = serial::grad_stops_to_string(&unsafe { gradient(g) }).into_bytes().into_boxed_slice();
        let len = bytes.len();
        unsafe { *out = SnapBuf { data: Box::into_raw(bytes) as *mut u8, len } };
        true
    })
}

/// GradFromXml from the values of FillGradient, GradientAngle and GradientStops (nul-terminated UTF-8, null when the
/// attribute is missing): the kind and angle into *kind / *angle, each stop through `stop`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_grad_from_attrs(kind: *const c_char, angle: *const c_char, stops: *const c_char, out_kind: *mut i32,
                                                  out_angle: *mut f32, stop: Option<unsafe extern "C" fn(*mut c_void, f32, u32)>,
                                                  ctx: *mut c_void) -> bool {
    guard(false, || {
        let attr = |p: *const c_char| (!p.is_null()).then(|| unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned());
        let (k, a, s) = (attr(kind), attr(angle), attr(stops));
        let g = serial::grad_from_attrs(k.as_deref(), a.as_deref(), s.as_deref());
        if out_kind.is_null() || out_angle.is_null() {
            return false;
        }
        unsafe {
            *out_kind = g.kind;
            *out_angle = g.angle;
        }
        if let Some(stop) = stop {
            for st in &g.stops {
                unsafe { stop(ctx, st.pos, st.color) };
            }
        }
        true
    })
}
