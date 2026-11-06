//! C ABI (snapcore.h) of the color texts, HDR capture, OCR, face detection and barcode reading.

use std::ffi::c_void;
use std::sync::OnceLock;

use windows::Win32::Foundation::RECT;

use crate::ffi::{ImageIn, SnapWStr, guard, image_in, wide_arg, wide_out};
use crate::ffi_pixels::in_place;
use crate::image::Rect;
use crate::ocr::OcrError;
use crate::{barcode, color, hdr, ocr};

// ---------------------------------------------------------------- color texts (Color.cpp, ColorToHex / HexToColor)

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_color_text(c: u32, fmt: i32, out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &color::to_text(c, fmt).encode_utf16().collect::<Vec<_>>()))
}

/// The name of a format, nul-terminated, for the life of the process ("" past the last).
#[unsafe(no_mangle)]
pub extern "C" fn snapcore_color_format_name(f: i32) -> *const u16 {
    static NAMES: OnceLock<Vec<Vec<u16>>> = OnceLock::new();
    let names = NAMES.get_or_init(|| {
        let mut v: Vec<Vec<u16>> = color::FORMATS.iter().map(|s| s.encode_utf16().chain([0]).collect()).collect();
        v.push(vec![0]);
        v
    });
    names[if (0..color::FORMATS.len() as i32).contains(&f) { f as usize } else { color::FORMATS.len() }].as_ptr()
}

/// h, s, l into out[0..3].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_color_hsl(c: u32, out: *mut f64) {
    let (h, s, l) = color::rgb_to_hsl(c);
    if !out.is_null() {
        unsafe { std::slice::from_raw_parts_mut(out, 3) }.copy_from_slice(&[h, s, l]);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_color_luminance(c: u32) -> f64 {
    color::luminance(c)
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_color_contrast(a: u32, b: u32) -> f64 {
    color::contrast_ratio(a, b)
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_color_default_format() -> i32 {
    guard(0, color::default_format)
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_color_set_default_format(f: i32) {
    guard((), || color::set_default_format(f))
}

/// The recent colors into out (capacity cap); returns how many there are.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_color_recent(out: *mut u32, cap: usize) -> usize {
    guard(0, || {
        let v = color::recent();
        if !out.is_null() {
            let k = v.len().min(cap);
            unsafe { std::slice::from_raw_parts_mut(out, k) }.copy_from_slice(&v[..k]);
        }
        v.len()
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_color_add_recent(c: u32) {
    guard((), || color::add_recent(c))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_color_from_hex(s: *const u16, len: usize, def: u32) -> u32 {
    guard(def, || color::hex_to_color(&unsafe { wide_arg(s, len) }, def))
}

// ---------------------------------------------------------------- HDR (Hdr.cpp)

fn win_rect(r: Rect) -> RECT {
    RECT { left: r.left, top: r.top, right: r.right, bottom: r.bottom }
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_hdr_any_monitor() -> bool {
    guard(false, hdr::any_hdr_monitor)
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_hdr_is_area(screen: Rect) -> bool {
    guard(false, || hdr::is_hdr_area(win_rect(screen)))
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_hdr_enabled() -> bool {
    guard(true, hdr::enabled)
}

/// Replaces in place the parts of a GDI copy of `screen` (w × h pixels) that lie on HDR monitors.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_hdr_fix_capture(screen: Rect, px: *mut u32, w: i32, h: i32) -> bool {
    guard(false, || {
        let mut any = false;
        unsafe { in_place(px, w, h, |img| any = hdr::fix_capture(win_rect(screen), img)) };
        any
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_hdr_tone_map(v: f32, sdr_white_nits: f32) -> u8 {
    hdr::tone_map(v, sdr_white_nits)
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_hdr_half_to_float(h: u16) -> f32 {
    hdr::half_to_float(h)
}

/// A recording session; null when the area is not on (only) an HDR monitor or there is no duplication.
#[unsafe(no_mangle)]
pub extern "C" fn snapcore_hdr_begin(screen: Rect, out_w: i32, out_h: i32) -> *mut hdr::Session {
    guard(std::ptr::null_mut(), || match hdr::Session::begin(win_rect(screen), out_w, out_h) {
        Some(s) => Box::into_raw(Box::new(s)),
        None => std::ptr::null_mut(),
    })
}

/// The latest frame into `bottom_up` (out_w × out_h pixels, rows bottom-up).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_hdr_grab(s: *mut hdr::Session, bottom_up: *mut u32, n: usize) -> bool {
    guard(false, || {
        let Some(s) = (unsafe { s.as_mut() }) else { return false };
        if bottom_up.is_null() {
            return false;
        }
        s.grab(unsafe { std::slice::from_raw_parts_mut(bottom_up, n) })
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_hdr_end(s: *mut hdr::Session) {
    if !s.is_null() {
        guard((), || drop(unsafe { Box::from_raw(s) }));
    }
}

// ---------------------------------------------------------------- OCR and faces (Ocr.cpp)
// Results: 0 done, 1 empty image, 2 no OCR language installed, 3 OCR failed, 4 no face detection on this Windows,
// 5 face detection failed, 6 a Windows error whose text is in *msg.

unsafe fn ocr_code(e: OcrError, msg: *mut SnapWStr) -> i32 {
    match e {
        OcrError::Empty => 1,
        OcrError::NoLanguage => 2,
        OcrError::Failed => 3,
        OcrError::FacesUnsupported => 4,
        OcrError::FacesFailed => 5,
        OcrError::Message(m) => {
            wide_out(msg, &m.encode_utf16().collect::<Vec<_>>());
            6
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_ocr_text(img: *const ImageIn, text: *mut SnapWStr, msg: *mut SnapWStr) -> i32 {
    guard(3, || {
        let Some(s) = unsafe { img.as_ref() }.and_then(|r| unsafe { image_in(r) }) else { return 1 };
        match ocr::run_ocr(&s) {
            Ok(t) => {
                wide_out(text, &t);
                0
            }
            Err(e) => unsafe { ocr_code(e, msg) },
        }
    })
}

/// Each line, then each of its words (`word` true), with their boxes in image pixels.
pub type OcrSink = unsafe extern "C" fn(ctx: *mut c_void, text: *const u16, len: usize, r: Rect, word: bool);

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_ocr_lines(img: *const ImageIn, sink: OcrSink, ctx: *mut c_void, msg: *mut SnapWStr) -> i32 {
    guard(3, || {
        let Some(s) = unsafe { img.as_ref() }.and_then(|r| unsafe { image_in(r) }) else { return 1 };
        match ocr::run_ocr_lines(&s) {
            Ok(lines) => {
                for l in &lines {
                    unsafe { sink(ctx, l.text.as_ptr(), l.text.len(), l.rect, false) };
                    for w in &l.words {
                        unsafe { sink(ctx, w.text.as_ptr(), w.text.len(), w.rect, true) };
                    }
                }
                0
            }
            Err(e) => unsafe { ocr_code(e, msg) },
        }
    })
}

pub type FaceSink = unsafe extern "C" fn(ctx: *mut c_void, r: Rect);

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_detect_faces(img: *const ImageIn, sink: FaceSink, ctx: *mut c_void, msg: *mut SnapWStr) -> i32 {
    guard(5, || {
        let Some(s) = unsafe { img.as_ref() }.and_then(|r| unsafe { image_in(r) }) else { return 1 };
        match ocr::detect_faces(&s) {
            Ok(faces) => {
                for f in faces {
                    unsafe { sink(ctx, f) };
                }
                0
            }
            Err(e) => unsafe { ocr_code(e, msg) },
        }
    })
}

// ---------------------------------------------------------------- QR codes and barcodes (Barcode.cpp)

/// One code: its text (UTF-8), its symbology (nul-terminated), where it is and whether it is a link.
pub type CodeSink = unsafe extern "C" fn(ctx: *mut c_void, text: *const u8, len: usize, format: *const u8, r: Rect, url: bool);

/// Calls `sink` for each code found; returns how many.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_barcode_read(img: *const ImageIn, sink: CodeSink, ctx: *mut c_void) -> usize {
    guard(0, || {
        let Some(s) = unsafe { img.as_ref() }.and_then(|r| unsafe { image_in(r) }) else { return 0 };
        let codes = barcode::read(&s);
        for c in &codes {
            let f: Vec<u8> = c.format.bytes().chain([0]).collect();
            unsafe { sink(ctx, c.text.as_ptr(), c.text.len(), f.as_ptr(), c.rect, c.url) };
        }
        codes.len()
    })
}
