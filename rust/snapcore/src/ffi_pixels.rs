//! C ABI of the pixel work (snapcore.h): effects, Beautify, the capture preset effects, computer vision, scaling and
//! the PDF writer. Images come in as SnapcoreImageIn (copied) and go out as a SnapcoreImage (copy it out, then
//! snapcore_image_free); the in-place effects change the caller's pixels directly.

use crate::beautify::{self, BeautifyOptions};
use crate::cv;
use crate::ffi::{Decoded, ImageIn, SnapWStr, guard, image_in, wide_arg, wide_out};
use crate::fx::{self, CaptureEffects, Edge};
use crate::gdip;
use crate::image::{Image, Rect};
use crate::pdf;

fn out(img: Image) -> *mut Decoded {
    Box::into_raw(Box::new(Decoded::new(img)))
}

/// The caller's pixels as an Image, changed by `f`, and written back.
unsafe fn in_place(px: *mut u32, w: i32, h: i32, f: impl FnOnce(&mut Image)) {
    if px.is_null() || w <= 0 || h <= 0 {
        return;
    }
    let n = w as usize * h as usize;
    let buf = unsafe { std::slice::from_raw_parts_mut(px, n) };
    let mut img = Image { w: w as u32, h: h as u32, px: buf.to_vec(), dpi: 0.0, meta: None };
    f(&mut img);
    if img.px.len() == n {
        buf.copy_from_slice(&img.px);
    }
}

/// An effect and its arguments (the meaning of i, d, c, rect and text depends on `op`; see snapcore.h).
#[repr(C)]
pub struct FxArgs {
    pub op: i32,
    pub i: [i32; 4],
    pub d: f64,
    pub c: [u32; 2],
    pub rect: Rect,
    pub text: *const u16,
    pub text_len: usize,
}

/// An effect that makes a new image.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_fx(img: *const ImageIn, a: *const FxArgs) -> *mut Decoded {
    guard(std::ptr::null_mut(), || {
        let (Some(r), Some(a)) = (unsafe { img.as_ref() }, unsafe { a.as_ref() }) else { return std::ptr::null_mut() };
        let Some(s) = (unsafe { image_in(r) }) else { return std::ptr::null_mut() };
        let text = unsafe { wide_arg(a.text, a.text_len) };
        let i = a.i;
        out(match a.op {
            0 => fx::rotate90(&s, i[0] != 0),
            1 => fx::rotate180(&s),
            2 => fx::flip_h(&s),
            3 => fx::flip_v(&s),
            4 => fx::rotate_angle(&s, a.d, a.c[0]),
            5 => fx::canvas_resize(&s, i[0], i[1], i[2], a.c[0]),
            6 => fx::auto_trim(&s, i[0]),
            7 => fx::scale(&s, i[0]),
            8 => fx::border(&s, i[0], a.c[0]),
            9 => fx::shadow(&s, i[0], i[1], i[2]),
            10 => fx::edge_effect(&s, Edge::from_i32(i[0]), i[1], i[2], i[3] != 0),
            11 => fx::perspective(&s, i[0], i[1], i[2]),
            12 => fx::page_curl(&s, i[0], i[1]),
            13 => fx::spotlight(&s, a.rect, i[0], i[1], i[2], i[3] != 0),
            14 => fx::caption(&s, &text, i[0] != 0, i[1], a.c[0], a.c[1]),
            15 => gdip::scale_image(&s, i[0], i[1], i[2] != 0),
            16 => gdip::make_thumbnail(&s, i[0], i[1]),
            _ => return std::ptr::null_mut(),
        })
    })
}

/// An effect that changes the pixels in place.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_fx_in_place(px: *mut u32, w: i32, h: i32, a: *const FxArgs) {
    guard((), || {
        let Some(a) = (unsafe { a.as_ref() }) else { return };
        let text = unsafe { wide_arg(a.text, a.text_len) };
        let i = a.i;
        unsafe {
            in_place(px, w, h, |img| match a.op {
                0 => fx::grayscale(img),
                1 => fx::invert(img),
                2 => fx::sepia(img),
                3 => fx::monochrome(img, i[0]),
                4 => fx::brightness_contrast(img, i[0], i[1]),
                5 => fx::hue_saturation(img, i[0], i[1]),
                6 => fx::gamma(img, a.d),
                7 => fx::replace_color(img, a.c[0], a.c[1], i[0]),
                8 => fx::box_blur(img, a.rect, i[0]),
                9 => fx::pixelate(img, a.rect, i[0]),
                10 => fx::emboss(img, i[0]),
                11 => fx::sharpen(img, i[0]),
                12 => fx::watermark(img, &text, i[0], i[1] != 0, i[2] != 0),
                _ => {}
            })
        }
    })
}

/// {computer} {user} {date} {time} replaced.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_fx_expand_tokens(text: *const u16, len: usize, out_text: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out_text, &fx::expand_caption_tokens(&unsafe { wide_arg(text, len) })))
}

/// BeautifyOptions (colors as COLORREF).
#[repr(C)]
pub struct BeautifyIn {
    pub background: i32,
    pub color1: u32,
    pub color2: u32,
    pub angle: i32,
    pub image_path: *const u16,
    pub image_path_len: usize,
    pub padding: i32,
    pub radius: i32,
    pub shadow: i32,
    pub shadow_size: i32,
    pub align: i32,
    pub aspect: i32,
    pub auto_balance: bool,
}

unsafe fn beautify_options(b: &BeautifyIn) -> BeautifyOptions {
    BeautifyOptions {
        background: b.background,
        color1: b.color1,
        color2: b.color2,
        angle: b.angle,
        image_path: unsafe { wide_arg(b.image_path, b.image_path_len) },
        padding: b.padding,
        radius: b.radius,
        shadow: b.shadow,
        shadow_size: b.shadow_size,
        align: b.align,
        aspect: b.aspect,
        auto_balance: b.auto_balance,
    }
}

/// Beautify; *origin gets where the capture ended up.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_beautify(img: *const ImageIn, o: *const BeautifyIn, origin: *mut i32) -> *mut Decoded {
    guard(std::ptr::null_mut(), || {
        let (Some(r), Some(o)) = (unsafe { img.as_ref() }, unsafe { o.as_ref() }) else { return std::ptr::null_mut() };
        let Some(s) = (unsafe { image_in(r) }) else { return std::ptr::null_mut() };
        let (img, (x, y)) = beautify::beautify(&s, &unsafe { beautify_options(o) });
        if !origin.is_null() {
            unsafe {
                *origin = x;
                *origin.add(1) = y;
            }
        }
        out(img)
    })
}

/// The desktop wallpaper file, or empty.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_wallpaper_path(out_path: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out_path, &beautify::wallpaper_path()))
}

/// CaptureEffects (colors as COLORREF), field by field as Effects.h.
#[repr(C)]
pub struct CaptureEffectsIn {
    pub border: bool,
    pub border_width: i32,
    pub border_color: u32,
    pub filter: bool,
    pub filter_kind: i32,
    pub shadow: bool,
    pub shadow_offset: i32,
    pub shadow_blur: i32,
    pub shadow_opacity: i32,
    pub caption: bool,
    pub caption_text: *const u16,
    pub caption_text_len: usize,
    pub caption_top: bool,
    pub replace_color: bool,
    pub replace_from: u32,
    pub replace_to: u32,
    pub replace_tolerance: i32,
    pub scale: bool,
    pub scale_percent: i32,
    pub edge: bool,
    pub edge_style: i32,
    pub edge_size: i32,
    pub edge_sides: i32,
    pub trim: bool,
    pub trim_tolerance: i32,
    pub adjust: bool,
    pub brightness: i32,
    pub contrast: i32,
    pub hue: i32,
    pub saturation: i32,
    pub rotate: bool,
    pub rotate_kind: i32,
    pub watermark: bool,
    pub watermark_text: *const u16,
    pub watermark_text_len: usize,
    pub watermark_opacity: i32,
    pub watermark_tiled: bool,
    pub resolution: bool,
    pub dpi: i32,
    pub beautify: bool,
    pub beautify_opt: BeautifyIn,
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_capture_effects(img: *const ImageIn, e: *const CaptureEffectsIn) -> *mut Decoded {
    guard(std::ptr::null_mut(), || {
        let (Some(r), Some(e)) = (unsafe { img.as_ref() }, unsafe { e.as_ref() }) else { return std::ptr::null_mut() };
        let Some(s) = (unsafe { image_in(r) }) else { return std::ptr::null_mut() };
        let ce = CaptureEffects {
            border: e.border,
            border_width: e.border_width,
            border_color: e.border_color,
            filter: e.filter,
            filter_kind: e.filter_kind,
            shadow: e.shadow,
            shadow_offset: e.shadow_offset,
            shadow_blur: e.shadow_blur,
            shadow_opacity: e.shadow_opacity,
            caption: e.caption,
            caption_text: unsafe { wide_arg(e.caption_text, e.caption_text_len) },
            caption_top: e.caption_top,
            replace_color: e.replace_color,
            replace_from: e.replace_from,
            replace_to: e.replace_to,
            replace_tolerance: e.replace_tolerance,
            scale: e.scale,
            scale_percent: e.scale_percent,
            edge: e.edge,
            edge_style: e.edge_style,
            edge_size: e.edge_size,
            edge_sides: e.edge_sides,
            trim: e.trim,
            trim_tolerance: e.trim_tolerance,
            adjust: e.adjust,
            brightness: e.brightness,
            contrast: e.contrast,
            hue: e.hue,
            saturation: e.saturation,
            rotate: e.rotate,
            rotate_kind: e.rotate_kind,
            watermark: e.watermark,
            watermark_text: unsafe { wide_arg(e.watermark_text, e.watermark_text_len) },
            watermark_opacity: e.watermark_opacity,
            watermark_tiled: e.watermark_tiled,
            resolution: e.resolution,
            dpi: e.dpi,
            beautify: e.beautify,
            beautify_opt: unsafe { beautify_options(&e.beautify_opt) },
        };
        out(fx::apply_capture_effects(&s, &ce))
    })
}

// ---------------------------------------------------------------- computer vision (masks are w × h bytes)

unsafe fn mask_out(m: &[u8], dst: *mut u8, n: usize) -> bool {
    if dst.is_null() || m.len() != n {
        return false;
    }
    unsafe { std::slice::from_raw_parts_mut(dst, n) }.copy_from_slice(m);
    true
}

unsafe fn mask_in<'a>(m: *const u8, n: usize) -> &'a [u8] {
    if m.is_null() || n == 0 { &[] } else { unsafe { std::slice::from_raw_parts(m, n) } }
}

/// WandSelect, or DoWand when `do_wand`; false when the result is empty (DoWand outside the image).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_cv_wand(img: *const ImageIn, x: i32, y: i32, tol: f32, global: bool, do_wand: bool,
                                          mask: *mut u8) -> bool {
    guard(false, || {
        let Some(s) = unsafe { img.as_ref() }.and_then(|r| unsafe { image_in(r) }) else { return false };
        let m = if do_wand { cv::do_wand(&s, (x, y), tol, global) } else { cv::wand_select(&s, (x, y), tol, global) };
        unsafe { mask_out(&m, mask, s.px.len()) }
    })
}

/// 0 rectangle, 1 ellipse (in `r`), 2 polygon (`pts`: x, y pairs).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_cv_shape_mask(kind: i32, w: i32, h: i32, r: Rect, pts: *const i32, n: usize, mask: *mut u8) {
    guard((), || {
        if w <= 0 || h <= 0 {
            return;
        }
        let m = match kind {
            0 => cv::rect_mask(w, h, r),
            1 => cv::ellipse_mask(w, h, r),
            _ => {
                let p = if pts.is_null() { &[][..] } else { unsafe { std::slice::from_raw_parts(pts, n * 2) } };
                cv::polygon_mask(w, h, &p.chunks(2).map(|c| (c[0], c[1])).collect::<Vec<_>>())
            }
        };
        unsafe { mask_out(&m, mask, (w * h) as usize) };
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_cv_mask_bounds(mask: *const u8, w: i32, h: i32) -> Rect {
    guard(Rect::default(), || {
        if w <= 0 || h <= 0 {
            return Rect::default();
        }
        cv::mask_bounds(unsafe { mask_in(mask, (w * h) as usize) }, w, h)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_cv_mask_empty(mask: *const u8, n: usize) -> bool {
    guard(true, || cv::mask_empty(unsafe { mask_in(mask, n) }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_cv_dilate(mask: *const u8, w: i32, h: i32, k: i32, dst: *mut u8) {
    guard((), || {
        if w <= 0 || h <= 0 {
            return;
        }
        let n = (w * h) as usize;
        let m = cv::dilate(unsafe { mask_in(mask, n) }, w, h, k);
        unsafe { mask_out(&m, dst, n) };
    })
}

/// 0 Dingo7 (kernel; *dominant and *share when it worked, false when there is no ring), 1 diffusion (iterations),
/// 2 smart fill. The pixels change in place.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_cv_inpaint(kind: i32, px: *mut u32, w: i32, h: i32, mask: *const u8, arg: i32,
                                             dominant: *mut u32, share: *mut f64) -> bool {
    guard(false, || {
        if w <= 0 || h <= 0 {
            return false;
        }
        let m = unsafe { mask_in(mask, (w * h) as usize) };
        if m.len() != (w * h) as usize {
            return false;
        }
        let mut ok = true;
        unsafe {
            in_place(px, w, h, |img| match kind {
                0 => match cv::dingo7_inpaint(img, m, arg) {
                    Some((d, s)) => {
                        if !dominant.is_null() {
                            *dominant = d;
                        }
                        if !share.is_null() {
                            *share = s;
                        }
                    }
                    None => ok = false,
                },
                1 => cv::diffusion_inpaint(img, m, arg),
                _ => cv::smart_fill(img, m),
            })
        };
        ok
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_cv_auto_tolerance(img: *const ImageIn) -> f32 {
    guard(10.0, || {
        let Some(s) = unsafe { img.as_ref() }.and_then(|r| unsafe { image_in(r) }) else { return 10.0 };
        cv::smart_move_auto_tolerance(&s)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_cv_tolerance_scaling(t: f32, in_min: f32, in_max: f32, out_min: f32, out_max: f32) -> f32 {
    cv::smart_move_tolerance_scaling(t, in_min, in_max, out_min, out_max)
}

/// The offset of `b` relative to `a` in *offset (x, y); *confidence (may be null) is left alone when the sizes differ.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_cv_detect_translation(a: *const ImageIn, b: *const ImageIn, offset: *mut i32,
                                                        confidence: *mut f64) {
    guard((), || {
        let ia = unsafe { a.as_ref() }.and_then(|r| unsafe { image_in(r) }).unwrap_or_default();
        let ib = unsafe { b.as_ref() }.and_then(|r| unsafe { image_in(r) }).unwrap_or_default();
        let same = !ia.is_empty() && !ib.is_empty() && ia.w == ib.w && ia.h == ib.h;
        let ((x, y), c) = cv::detect_translation(&ia, &ib);
        unsafe {
            if !offset.is_null() {
                *offset = x;
                *offset.add(1) = y;
            }
            if same && !confidence.is_null() {
                *confidence = c;
            }
        }
    })
}

// ---------------------------------------------------------------- PDF and page breaks

#[repr(C)]
pub struct PdfLinkIn {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub url: *const u16,
    pub url_len: usize,
}

/// 0 saved, 1 empty image, 2 the image could not be encoded, 3 the file could not be created, 4 nor written.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_pdf_save(path: *const u16, len: usize, img: *const ImageIn, links: *const PdfLinkIn,
                                           n: usize) -> i32 {
    guard(2, || {
        let Some(s) = unsafe { img.as_ref() }.and_then(|r| unsafe { image_in(r) }) else { return 1 };
        let ls = if links.is_null() { &[][..] } else { unsafe { std::slice::from_raw_parts(links, n) } };
        let ls: Vec<pdf::Link> = ls
            .iter()
            .map(|l| pdf::Link { x: l.x, y: l.y, w: l.w, h: l.h, url: unsafe { wide_arg(l.url, l.url_len) } })
            .collect();
        match pdf::save_pdf(&unsafe { wide_arg(path, len) }, &s, &ls) {
            Ok(()) => 0,
            Err(pdf::PdfError::Empty) => 1,
            Err(pdf::PdfError::Encode) => 2,
            Err(pdf::PdfError::Create) => 3,
            Err(pdf::PdfError::Write) => 4,
        }
    })
}

/// SmartBreaks (`parts` < 0: page height `page_h`, `search`) or SmartCuts (`parts` ≥ 0) into `out` (capacity `cap`);
/// returns how many there are.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_page_breaks(img: *const ImageIn, page_h: i32, search: i32, parts: i32, out_cuts: *mut i32,
                                              cap: usize) -> usize {
    guard(0, || {
        let Some(s) = unsafe { img.as_ref() }.and_then(|r| unsafe { image_in(r) }) else { return 0 };
        let v = if parts >= 0 { pdf::smart_cuts(&s, parts) } else { pdf::smart_breaks(&s, page_h, search) };
        if !out_cuts.is_null() {
            let k = v.len().min(cap);
            unsafe { std::slice::from_raw_parts_mut(out_cuts, k) }.copy_from_slice(&v[..k]);
        }
        v.len()
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_split_long_pages() -> bool {
    guard(true, pdf::split_long_pages)
}
