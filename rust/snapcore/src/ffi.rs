//! The C ABI the C++ side calls (declared in include/snapcore.h). Strings cross as nul-terminated UTF-16 unless a length
//! is given; strings snapcore returns as `*const u16` live as long as the process. No panic crosses the boundary:
//! every entry point catches it and reports a failure.

use std::ffi::{OsStr, OsString, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::PathBuf;

use crate::container::{Reader, Writer, is_snap};
use crate::wide::{from_ptr, to_os, to_string_lossy};
use crate::codec::{self, DecodeError, EncodeError, Format, SaveOptions};
use crate::image::{Image, Meta};
use crate::{gif, i18n, library, log, reg, snap, xml};
use windows::Win32::Foundation::HWND;

fn guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

/// A buffer allocated by snapcore; give it back with snapcore_buf_free.
#[repr(C)]
pub struct SnapBuf {
    pub data: *mut u8,
    pub len: usize,
}

/// A UTF-16 string allocated by snapcore (no nul); give it back with snapcore_wstr_free.
#[repr(C)]
pub struct SnapWStr {
    pub data: *mut u16,
    pub len: usize,
}

unsafe fn path_arg(path: *const u16, len: usize) -> Option<PathBuf> {
    if path.is_null() {
        return None;
    }
    let w = unsafe { std::slice::from_raw_parts(path, len) };
    Some(PathBuf::from(to_os(w)))
}

unsafe fn str_arg<'a>(s: *const u8, len: usize) -> Option<&'a str> {
    if s.is_null() {
        return None;
    }
    std::str::from_utf8(unsafe { std::slice::from_raw_parts(s, len) }).ok()
}

/// A nul-terminated UTF-16 argument; None for null.
unsafe fn os_arg(p: *const u16) -> Option<OsString> {
    unsafe { from_ptr(p) }.map(to_os)
}

// ---------------------------------------------------------------- .snap container

/// True if the file starts with the .snap header.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_is_snap(path: *const u16, len: usize) -> bool {
    guard(false, || unsafe { path_arg(path, len) }.is_some_and(|p| is_snap(&p)))
}

/// Opens a .snap file for reading; null if it cannot be opened or is not a .snap file.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_open(path: *const u16, len: usize) -> *mut Reader {
    guard(std::ptr::null_mut(), || match unsafe { path_arg(path, len) }.map(|p| Reader::open(&p)) {
        Some(Ok(r)) => Box::into_raw(Box::new(r)),
        _ => std::ptr::null_mut(),
    })
}

/// Reads a whole stream into *out (free it with snapcore_buf_free); false if it does not exist or cannot be read.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_read(r: *mut Reader, stream: *const u8, stream_len: usize, out: *mut SnapBuf) -> bool {
    guard(false, || {
        let (Some(r), Some(name), false) = (unsafe { r.as_mut() }, unsafe { str_arg(stream, stream_len) }, out.is_null())
        else {
            return false;
        };
        match r.read(name) {
            Ok(v) => {
                let b = v.into_boxed_slice();
                let len = b.len();
                unsafe { *out = SnapBuf { data: Box::into_raw(b) as *mut u8, len } };
                true
            }
            Err(_) => false,
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_close(r: *mut Reader) {
    if !r.is_null() {
        guard((), || drop(unsafe { Box::from_raw(r) }));
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_buf_free(b: SnapBuf) {
    if !b.data.is_null() {
        guard((), || drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(b.data, b.len)) }));
    }
}

/// Creates (or truncates) a .snap file for writing; null on failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_create(path: *const u16, len: usize) -> *mut Writer {
    guard(std::ptr::null_mut(), || match unsafe { path_arg(path, len) }.map(|p| Writer::create(&p)) {
        Some(Ok(w)) => Box::into_raw(Box::new(w)),
        _ => std::ptr::null_mut(),
    })
}

/// Writes a stream (its storages are created as needed).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_write(w: *mut Writer, stream: *const u8, stream_len: usize, data: *const u8, len: usize) -> bool {
    guard(false, || {
        let (Some(w), Some(name)) = (unsafe { w.as_mut() }, unsafe { str_arg(stream, stream_len) }) else {
            return false;
        };
        let bytes = if len == 0 { &[][..] } else if data.is_null() { return false } else { unsafe { std::slice::from_raw_parts(data, len) } };
        w.write(name, bytes).is_ok()
    })
}

/// Writes everything out, closes the file and frees the writer; false if the file is incomplete.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_finish(w: *mut Writer) -> bool {
    if w.is_null() {
        return false;
    }
    guard(false, || unsafe { Box::from_raw(w) }.finish().is_ok())
}

/// Closes the file without finishing it and frees the writer (the caller deletes the partial file).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_abort(w: *mut Writer) {
    if !w.is_null() {
        guard((), || drop(unsafe { Box::from_raw(w) }));
    }
}

// ---------------------------------------------------------------- strings handed to the C++ side

fn wstr(s: &OsStr) -> SnapWStr {
    use std::os::windows::ffi::OsStrExt;
    let b: Box<[u16]> = s.encode_wide().collect();
    let len = b.len();
    SnapWStr { data: Box::into_raw(b) as *mut u16, len }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_wstr_free(s: SnapWStr) {
    if !s.data.is_null() {
        guard((), || drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(s.data, s.len)) }));
    }
}

// ---------------------------------------------------------------- log

/// Opens %LOCALAPPDATA%\Snapnab\Logs\<component>.log and writes the start line with `version`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_log_init(component: *const u16, version: *const u16) {
    guard((), || {
        let c = unsafe { from_ptr(component) }.map(to_string_lossy).unwrap_or_default();
        let v = unsafe { from_ptr(version) }.map(to_string_lossy).unwrap_or_default();
        log::init(&c, &v);
    })
}

/// One already formatted message (the time and thread id are added here).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_log(msg: *const u16) {
    guard((), || log::write(&unsafe { from_ptr(msg) }.map(to_string_lossy).unwrap_or_default()))
}

// ---------------------------------------------------------------- registry (sub may be null)

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_reg_get_dword(sub: *const u16, name: *const u16, def: u32) -> u32 {
    guard(def, || {
        let (s, n) = unsafe { (os_arg(sub), os_arg(name)) };
        n.map_or(def, |n| reg::get_dword(&n, def, s.as_deref()))
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_reg_set_dword(sub: *const u16, name: *const u16, v: u32) {
    guard((), || {
        let (s, n) = unsafe { (os_arg(sub), os_arg(name)) };
        if let Some(n) = n {
            reg::set_dword(&n, v, s.as_deref());
        }
    })
}

/// The string in *out (free it with snapcore_wstr_free); false if the value is missing or not a string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_reg_get_string(sub: *const u16, name: *const u16, out: *mut SnapWStr) -> bool {
    guard(false, || {
        let (s, n) = unsafe { (os_arg(sub), os_arg(name)) };
        match (n.and_then(|n| reg::get_string(&n, s.as_deref())), out.is_null()) {
            (Some(v), false) => {
                unsafe { *out = wstr(&v) };
                true
            }
            _ => false,
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_reg_set_string(sub: *const u16, name: *const u16, value: *const u16) {
    guard((), || {
        let (s, n, v) = unsafe { (os_arg(sub), os_arg(name), os_arg(value)) };
        if let (Some(n), Some(v)) = (n, v) {
            reg::set_string(&n, &v, s.as_deref());
        }
    })
}

// ---------------------------------------------------------------- interface language

fn code_ptr(code: &str) -> *const u16 {
    static EMPTY: [u16; 1] = [0];
    match i18n::LANGUAGES.iter().position(|(c, _)| *c == code) {
        Some(i) => i18n::LANGUAGES_WIDE[i].0.as_ptr(),
        None => EMPTY.as_ptr(),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_i18n_init() {
    guard((), i18n::init)
}

/// The translation of `pt`, or `pt` itself when there is none.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_tr(pt: *const u16) -> *const u16 {
    guard(pt, || match unsafe { from_ptr(pt) }.and_then(i18n::tr) {
        Some(t) => t.as_ptr(),
        None => pt,
    })
}

/// Language `index` of the 12 (code and own name); false past the end.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_i18n_language(index: usize, code: *mut *const u16, name: *mut *const u16) -> bool {
    guard(false, || match (i18n::LANGUAGES_WIDE.get(index), code.is_null() || name.is_null()) {
        (Some((c, n)), false) => {
            unsafe {
                *code = c.as_ptr();
                *name = n.as_ptr();
            }
            true
        }
        _ => false,
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_i18n_available(code: *const u16) -> bool {
    guard(false, || unsafe { from_ptr(code) }.is_some_and(|c| i18n::available(&to_string_lossy(c))))
}

/// The language chosen in Options, "" for the Windows language.
#[unsafe(no_mangle)]
pub extern "C" fn snapcore_i18n_configured() -> *const u16 {
    guard(code_ptr(""), || code_ptr(i18n::configured()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_i18n_set_configured(code: *const u16) {
    guard((), || i18n::set_configured(&unsafe { from_ptr(code) }.map(to_string_lossy).unwrap_or_default()))
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_i18n_resolve() -> *const u16 {
    guard(code_ptr("pt"), || code_ptr(i18n::resolve()))
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_i18n_current() -> *const u16 {
    guard(code_ptr("pt"), || i18n::current_wide().as_ptr())
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_i18n_rtl() -> bool {
    guard(false, i18n::rtl)
}

/// Interface font with the language's characters; null for the default font.
#[unsafe(no_mangle)]
pub extern "C" fn snapcore_i18n_ui_font_face() -> *const u16 {
    use windows::core::w;
    guard(std::ptr::null(), || match i18n::ui_font_face() {
        Some("Microsoft YaHei UI") => w!("Microsoft YaHei UI").0,
        Some(_) => w!("Nirmala UI").0,
        None => std::ptr::null(),
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_i18n_alt_key(name: *const u16, fallback: u16) -> u16 {
    guard(fallback, || unsafe { from_ptr(name) }.map_or(fallback, |n| i18n::alt_key(n, fallback)))
}

/// Reads a .po catalog and calls `entry(ctx, msgid, msgstr)` for each translated entry (nul-terminated UTF-16, valid
/// during the call); false if the file does not open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_po_load(path: *const u16, entry: extern "C" fn(*mut std::ffi::c_void, *const u16, *const u16),
                                          ctx: *mut std::ffi::c_void) -> bool {
    guard(false, || {
        let Some(p) = (unsafe { os_arg(path) }) else { return false };
        let mut v = Vec::new();
        if !i18n::load_po_file(std::path::Path::new(&p), &mut v) {
            return false;
        }
        for (id, msg) in v {
            let (id, msg) = (crate::wide::to_wide(&id), crate::wide::to_wide(&msg));
            entry(ctx, id.as_ptr(), msg.as_ptr());
        }
        true
    })
}

// ---------------------------------------------------------------- XML (Xml.cpp)
// Text crosses as UTF-8 with a length. A document read by snapcore comes back as calls to an XmlSink, in document
// order; one written by snapcore is fed to an XmlBuilder the same way.

/// Calls that rebuild a document on the C++ side: open(name), attr(k, v)…, text(t) if any, the children, close().
/// The pointers are valid during the call only.
#[repr(C)]
pub struct XmlSink {
    pub open: extern "C" fn(ctx: *mut c_void, name: *const u8, len: usize),
    pub attr: extern "C" fn(ctx: *mut c_void, k: *const u8, klen: usize, v: *const u8, vlen: usize),
    pub text: extern "C" fn(ctx: *mut c_void, t: *const u8, len: usize),
    pub close: extern "C" fn(ctx: *mut c_void),
}

fn emit(n: &xml::Node, sink: &XmlSink, ctx: *mut c_void) {
    (sink.open)(ctx, n.name.as_ptr(), n.name.len());
    for (k, v) in &n.attrs {
        (sink.attr)(ctx, k.as_ptr(), k.len(), v.as_ptr(), v.len());
    }
    if !n.text.is_empty() {
        (sink.text)(ctx, n.text.as_ptr(), n.text.len());
    }
    for c in &n.children {
        emit(c, sink, ctx);
    }
    (sink.close)(ctx);
}

unsafe fn bytes_arg<'a>(p: *const u8, len: usize) -> &'a [u8] {
    if p.is_null() || len == 0 { &[] } else { unsafe { std::slice::from_raw_parts(p, len) } }
}

unsafe fn text_arg(p: *const u8, len: usize) -> String {
    String::from_utf8_lossy(unsafe { bytes_arg(p, len) }).into_owned()
}

/// Reads `data` and sends the root element to the sink, also when the text is malformed (the part read up to the
/// error; false is returned then).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_xml_parse(data: *const u8, len: usize, sink: *const XmlSink, ctx: *mut c_void) -> bool {
    guard(false, || {
        let Some(sink) = (unsafe { sink.as_ref() }) else { return false };
        let (root, ok) = xml::parse_partial(unsafe { bytes_arg(data, len) });
        emit(&root, sink, ctx);
        ok
    })
}

/// As snapcore_xml_parse, from a file: -1 if the file cannot be read (nothing is sent), 0 if it is malformed, 1 if read.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_xml_load(path: *const u16, len: usize, sink: *const XmlSink, ctx: *mut c_void) -> i32 {
    guard(-1, || {
        let (Some(p), Some(sink)) = (unsafe { path_arg(path, len) }, unsafe { sink.as_ref() }) else { return -1 };
        let Ok(data) = std::fs::read(p) else { return -1 };
        let (root, ok) = xml::parse_partial(&data);
        emit(&root, sink, ctx);
        ok as i32
    })
}

/// A document being handed to snapcore element by element (open, attr, text, close), then written out.
#[derive(Default)]
pub struct XmlBuilder {
    stack: Vec<xml::Node>,
    root: Option<xml::Node>,
}

impl XmlBuilder {
    fn finish(mut self) -> xml::Node {
        while !self.stack.is_empty() {
            self.close();
        }
        self.root.unwrap_or_default()
    }

    fn close(&mut self) {
        if let Some(n) = self.stack.pop() {
            match self.stack.last_mut() {
                Some(parent) => parent.children.push(n),
                None => self.root = Some(n),
            }
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_xml_builder_new() -> *mut XmlBuilder {
    guard(std::ptr::null_mut(), || Box::into_raw(Box::default()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_xml_open(b: *mut XmlBuilder, name: *const u8, len: usize) {
    guard((), || {
        if let Some(b) = unsafe { b.as_mut() } {
            b.stack.push(xml::Node::new(unsafe { text_arg(name, len) }));
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_xml_attr(b: *mut XmlBuilder, k: *const u8, klen: usize, v: *const u8, vlen: usize) {
    guard((), || {
        if let Some(n) = unsafe { b.as_mut() }.and_then(|b| b.stack.last_mut()) {
            n.attrs.push(unsafe { (text_arg(k, klen), text_arg(v, vlen)) });
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_xml_text(b: *mut XmlBuilder, t: *const u8, len: usize) {
    guard((), || {
        if let Some(n) = unsafe { b.as_mut() }.and_then(|b| b.stack.last_mut()) {
            n.text.push_str(&unsafe { text_arg(t, len) });
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_xml_close(b: *mut XmlBuilder) {
    guard((), || {
        if let Some(b) = unsafe { b.as_mut() } {
            b.close();
        }
    })
}

/// Writes the document as text into *out (free it with snapcore_buf_free) and frees the builder.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_xml_serialize(b: *mut XmlBuilder, declaration: bool, out: *mut SnapBuf) -> bool {
    guard(false, || {
        if b.is_null() {
            return false;
        }
        let root = unsafe { Box::from_raw(b) }.finish();
        if out.is_null() {
            return false;
        }
        let bytes = xml::serialize(&root, declaration).into_bytes().into_boxed_slice();
        let len = bytes.len();
        unsafe { *out = SnapBuf { data: Box::into_raw(bytes) as *mut u8, len } };
        true
    })
}

/// Writes the document to a file (with the XML declaration) and frees the builder; false if it cannot be written.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_xml_save(b: *mut XmlBuilder, path: *const u16, len: usize) -> bool {
    guard(false, || {
        if b.is_null() {
            return false;
        }
        let root = unsafe { Box::from_raw(b) }.finish();
        unsafe { path_arg(path, len) }.is_some_and(|p| xml::save_file(&p, &root))
    })
}

// ---------------------------------------------------------------- image files (Image.cpp; WebP stays with libwebp)
// Errors: decoding 1 = failed, 2 = Windows has no decoder for the file; encoding 1 = failed, 2 = the AVIF codec hint
// applies.

/// A decoded image held by snapcore until the C++ side has copied it.
pub struct Decoded {
    img: Image,
    texts: [Vec<u16>; 4], // date taken, make, model, software (nul-terminated)
}

#[repr(C)]
pub struct ImageInfo {
    pub w: u32,
    pub h: u32,
    pub has_meta: bool,
    pub gps: bool,
    pub lat: f64,
    pub lon: f64,
}

fn decoded(r: Result<Image, DecodeError>, err: *mut i32) -> *mut Decoded {
    let code = match &r {
        Ok(_) => 0,
        Err(DecodeError::Failed) => 1,
        Err(DecodeError::NoDecoder) => 2,
    };
    if !err.is_null() {
        unsafe { *err = code };
    }
    let Ok(img) = r else { return std::ptr::null_mut() };
    Box::into_raw(Box::new(Decoded::new(img)))
}

impl Decoded {
    fn new(img: Image) -> Self {
        let text = |s: &str| crate::wide::to_wide(s);
        let texts = match &img.meta {
            Some(m) => [text(&m.date_taken), text(&m.make), text(&m.model), text(&m.software)],
            None => [vec![0], vec![0], vec![0], vec![0]],
        };
        Decoded { img, texts }
    }
}

/// Decodes an image in memory (not WebP); null on failure, with the reason in *err.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_image_decode(data: *const u8, len: usize, err: *mut i32) -> *mut Decoded {
    guard(std::ptr::null_mut(), || decoded(codec::decode(unsafe { bytes_arg(data, len) }), err))
}

/// Decodes an image file (not WebP); null on failure, with the reason in *err.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_image_load(path: *const u16, len: usize, err: *mut i32) -> *mut Decoded {
    guard(std::ptr::null_mut(), || match unsafe { path_arg(path, len) } {
        Some(p) => decoded(codec::load(&p), err),
        None => decoded(Err(DecodeError::Failed), err),
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_image_info(d: *const Decoded, out: *mut ImageInfo) {
    guard((), || {
        let (Some(d), false) = (unsafe { d.as_ref() }, out.is_null()) else { return };
        let m = d.img.meta.as_ref();
        unsafe {
            *out = ImageInfo {
                w: d.img.w,
                h: d.img.h,
                has_meta: m.is_some(),
                gps: m.is_some_and(|m| m.gps),
                lat: m.map_or(0.0, |m| m.lat),
                lon: m.map_or(0.0, |m| m.lon),
            }
        };
    })
}

/// The w × h pixels (0xAARRGGBB), valid until snapcore_image_free.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_image_pixels(d: *const Decoded) -> *const u32 {
    guard(std::ptr::null(), || unsafe { d.as_ref() }.map_or(std::ptr::null(), |d| d.img.px.as_ptr()))
}

/// A metadata text: 0 date taken, 1 make, 2 model, 3 software ("" when there is none); valid until snapcore_image_free.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_image_meta_text(d: *const Decoded, field: u32) -> *const u16 {
    guard(std::ptr::null(), || {
        unsafe { d.as_ref() }.and_then(|d| d.texts.get(field as usize)).map_or(std::ptr::null(), |t| t.as_ptr())
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_image_free(d: *mut Decoded) {
    if !d.is_null() {
        guard((), || drop(unsafe { Box::from_raw(d) }));
    }
}

/// An image handed over by the C++ side (Image and ImageMeta of Image.h); the strings are nul-terminated UTF-16 or
/// null. It is copied: the pointers need to live during the call only.
#[repr(C)]
pub struct ImageIn {
    pub w: u32,
    pub h: u32,
    pub px: *const u32,
    pub dpi: f32,
    pub has_meta: bool,
    pub date_taken: *const u16,
    pub make: *const u16,
    pub model: *const u16,
    pub software: *const u16,
    pub gps: bool,
    pub lat: f64,
    pub lon: f64,
}

/// A copy of the image; None if the pixels are missing.
unsafe fn image_in(r: &ImageIn) -> Option<Image> {
    let n = r.w as usize * r.h as usize;
    if n > 0 && r.px.is_null() {
        return None;
    }
    let text = |p: *const u16| unsafe { from_ptr(p) }.map(to_string_lossy).unwrap_or_default();
    Some(Image {
        w: r.w,
        h: r.h,
        px: if n > 0 { unsafe { std::slice::from_raw_parts(r.px, n) }.to_vec() } else { Vec::new() },
        dpi: r.dpi,
        meta: r.has_meta.then(|| Meta {
            date_taken: text(r.date_taken),
            make: text(r.make),
            model: text(r.model),
            software: text(r.software),
            gps: r.gps,
            lat: r.lat,
            lon: r.lon,
        }),
    })
}

/// An image to encode and how (SaveOptions of Image.h).
#[repr(C)]
pub struct EncodeRequest {
    pub image: ImageIn,
    pub format: i32,
    pub jpeg_quality: i32,
    pub avif_quality: i32,
    pub strip_metadata: bool,
    pub version: *const u16,
}

/// Encodes the image (not WebP) into *out (free it with snapcore_buf_free); false on failure, with the reason in *err.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_image_encode(req: *const EncodeRequest, out: *mut SnapBuf, err: *mut i32) -> bool {
    let set_err = |code: i32| {
        if !err.is_null() {
            unsafe { *err = code };
        }
    };
    let r = guard(Err(EncodeError::Failed), || {
        let Some(r) = (unsafe { req.as_ref() }) else { return Err(EncodeError::Failed) };
        let (Some(img), false) = (unsafe { image_in(&r.image) }, out.is_null()) else { return Err(EncodeError::Failed) };
        let text = |p: *const u16| unsafe { from_ptr(p) }.map(to_string_lossy).unwrap_or_default();
        let opt = SaveOptions { jpeg_quality: r.jpeg_quality, avif_quality: r.avif_quality, strip_metadata: r.strip_metadata };
        codec::encode(&img, Format::from_i32(r.format), &opt, &text(r.version))
    });
    match r {
        Ok(v) => {
            let b = v.into_boxed_slice();
            let len = b.len();
            unsafe { *out = SnapBuf { data: Box::into_raw(b) as *mut u8, len } };
            set_err(0);
            true
        }
        Err(EncodeError::Failed) => {
            set_err(1);
            false
        }
        Err(EncodeError::Codec) => {
            set_err(2);
            false
        }
    }
}

// ---------------------------------------------------------------- .snap documents (SnapFile.cpp)

/// A .snap being handed over frame by frame, then saved.
#[derive(Default)]
pub struct SnapSave {
    frames: Vec<snap::Frame>,
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_snap_new() -> *mut SnapSave {
    guard(std::ptr::null_mut(), || Box::into_raw(Box::default()))
}

/// Adds a frame: its base image and its document (the builder is consumed). False if the image has no pixels.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_add_frame(s: *mut SnapSave, base: *const ImageIn, doc: *mut XmlBuilder) -> bool {
    guard(false, || {
        let doc = if doc.is_null() { xml::Node::default() } else { unsafe { Box::from_raw(doc) }.finish() };
        let (Some(s), Some(base)) = (unsafe { s.as_mut() }, unsafe { base.as_ref() }) else { return false };
        let Some(base) = (unsafe { image_in(base) }) else { return false };
        s.frames.push(snap::Frame { base, doc, embedded: Default::default() });
        true
    })
}

/// Adds an image the last frame's objects embed (by StreamName).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_add_embedded(s: *mut SnapSave, name: *const u8, len: usize, img: *const ImageIn) -> bool {
    guard(false, || {
        let (Some(f), Some(img)) = (unsafe { s.as_mut() }.and_then(|s| s.frames.last_mut()), unsafe { img.as_ref() })
        else {
            return false;
        };
        let Some(img) = (unsafe { image_in(img) }) else { return false };
        f.embedded.insert(unsafe { text_arg(name, len) }, img);
        true
    })
}

/// Writes the .snap file and frees `s`; `version` is the program's (the Index's SoftwareVersion).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_save(s: *mut SnapSave, path: *const u16, len: usize, version: *const u16) -> bool {
    guard(false, || {
        if s.is_null() {
            return false;
        }
        let s = unsafe { Box::from_raw(s) };
        let version = unsafe { from_ptr(version) }.map(to_string_lossy).unwrap_or_default();
        unsafe { path_arg(path, len) }.is_some_and(|p| snap::save(&p, &s.frames, &version))
    })
}

/// A .snap read by snapcore, held until the C++ side has copied it.
pub struct SnapLoaded {
    frames: Vec<(Decoded, xml::Node, Vec<(String, Decoded)>)>,
}

/// Reads every frame of a .snap; null if it is not one, is damaged or has no base image.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_load(path: *const u16, len: usize) -> *mut SnapLoaded {
    guard(std::ptr::null_mut(), || {
        let Some(frames) = (unsafe { path_arg(path, len) }).and_then(|p| snap::load(&p)) else { return std::ptr::null_mut() };
        let frames = frames
            .into_iter()
            .map(|f| (Decoded::new(f.base), f.doc, f.embedded.into_iter().map(|(k, v)| (k, Decoded::new(v))).collect()))
            .collect();
        Box::into_raw(Box::new(SnapLoaded { frames }))
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_frames(h: *const SnapLoaded) -> usize {
    guard(0, || unsafe { h.as_ref() }.map_or(0, |h| h.frames.len()))
}

/// Sends frame i's document to the sink (as snapcore_xml_parse does).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_frame_doc(h: *const SnapLoaded, i: usize, sink: *const XmlSink, ctx: *mut c_void) {
    guard((), || {
        if let (Some(f), Some(sink)) = (unsafe { h.as_ref() }.and_then(|h| h.frames.get(i)), unsafe { sink.as_ref() }) {
            emit(&f.1, sink, ctx);
        }
    })
}

/// Frame i's base image (read it with snapcore_image_info / _pixels / _meta_text; do not free it).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_frame_base(h: *const SnapLoaded, i: usize) -> *const Decoded {
    guard(std::ptr::null(), || unsafe { h.as_ref() }.and_then(|h| h.frames.get(i)).map_or(std::ptr::null(), |f| &f.0))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_embedded_count(h: *const SnapLoaded, i: usize) -> usize {
    guard(0, || unsafe { h.as_ref() }.and_then(|h| h.frames.get(i)).map_or(0, |f| f.2.len()))
}

/// Frame i's k-th embedded image and its StreamName (UTF-8, not nul-terminated); do not free it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_embedded(h: *const SnapLoaded, i: usize, k: usize, name: *mut *const u8, len: *mut usize) -> *const Decoded {
    guard(std::ptr::null(), || {
        let Some((n, d)) = unsafe { h.as_ref() }.and_then(|h| h.frames.get(i)).and_then(|f| f.2.get(k)) else {
            return std::ptr::null();
        };
        if !name.is_null() && !len.is_null() {
            unsafe {
                *name = n.as_ptr();
                *len = n.len();
            }
        }
        d
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_free(h: *mut SnapLoaded) {
    if !h.is_null() {
        guard((), || drop(unsafe { Box::from_raw(h) }));
    }
}

/// Only the first frame's document: -1 if it cannot be read (nothing is sent), 0 if malformed (the partial tree is
/// sent), 1 if read.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_snap_load_document(path: *const u16, len: usize, sink: *const XmlSink, ctx: *mut c_void) -> i32 {
    guard(-1, || {
        let (Some(p), Some(sink)) = (unsafe { path_arg(path, len) }, unsafe { sink.as_ref() }) else { return -1 };
        match snap::load_document(&p) {
            Some((doc, ok)) => {
                emit(&doc, sink, ctx);
                ok as i32
            }
            None => -1,
        }
    })
}

// ---------------------------------------------------------------- the capture library (Library.cpp)

/// A UTF-16 string of `len` units (no nul needed); empty for null.
unsafe fn wide_arg(p: *const u16, len: usize) -> Vec<u16> {
    if p.is_null() { Vec::new() } else { unsafe { std::slice::from_raw_parts(p, len) }.to_vec() }
}

/// The library folder: `dir` if given, otherwise the program's (LibraryDir).
unsafe fn dir_arg(dir: *const u16, len: usize) -> Vec<u16> {
    if dir.is_null() { library::library_dir() } else { unsafe { wide_arg(dir, len) } }
}

fn wide_out(out: *mut SnapWStr, s: &[u16]) -> bool {
    if out.is_null() {
        return false;
    }
    unsafe { *out = wstr(&to_os(s)) };
    true
}

/// LibraryDir (created if missing).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_dir(out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &library::library_dir()))
}

/// Called once per item, the most recent first.
pub type LibraryItemFn = extern "C" fn(ctx: *mut c_void, path: *const u16, len: usize, modified: u64, size: u64, video: bool);

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_list(dir: *const u16, dir_len: usize, max: usize, item: Option<LibraryItemFn>, ctx: *mut c_void) {
    guard((), || {
        let Some(item) = item else { return };
        for it in library::list(&unsafe { dir_arg(dir, dir_len) }, max) {
            item(ctx, it.path.as_ptr(), it.path.len(), it.modified, it.size, it.video);
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_thumb_path(dir: *const u16, dir_len: usize, item: *const u16, item_len: usize, out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &library::thumb_path_for(&unsafe { dir_arg(dir, dir_len) }, &unsafe { wide_arg(item, item_len) })))
}

/// Writes the thumbnail of an item, already scaled; false if it is empty or cannot be written.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_write_thumbnail(dir: *const u16, dir_len: usize, item: *const u16, item_len: usize,
                                                          thumb: *const ImageIn, version: *const u16) -> bool {
    guard(false, || {
        let Some(thumb) = unsafe { thumb.as_ref() }.and_then(|t| unsafe { image_in(t) }) else { return false };
        let version = unsafe { from_ptr(version) }.map(to_string_lossy).unwrap_or_default();
        library::write_thumbnail(&unsafe { dir_arg(dir, dir_len) }, &unsafe { wide_arg(item, item_len) }, &thumb, &version)
    })
}

/// An image with its StreamName (UTF-8).
#[repr(C)]
pub struct NamedImage {
    pub name: *const u8,
    pub len: usize,
    pub image: ImageIn,
}

/// A capture to add (AddImageToLibrary / AddLayeredToLibrary); the strings are nul-terminated or null.
#[repr(C)]
pub struct LibraryAdd {
    pub dir: *const u16,
    pub dir_len: usize,
    pub base: ImageIn,
    pub thumb: ImageIn,
    pub mode: *const u16,
    pub window_title: *const u16,
    pub application: *const u16,
    pub objects: *mut XmlBuilder,
    pub embedded: *const NamedImage,
    pub embedded_count: usize,
    pub version: *const u16,
}

/// Adds the capture as a new .snap and writes its thumbnail; the path in *out. `objects` is consumed (null for a
/// plain image).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_add(req: *const LibraryAdd, out: *mut SnapWStr) -> bool {
    guard(false, || {
        let Some(r) = (unsafe { req.as_ref() }) else { return false };
        let objects = (!r.objects.is_null()).then(|| unsafe { Box::from_raw(r.objects) }.finish());
        let (Some(base), Some(thumb)) = (unsafe { image_in(&r.base) }, unsafe { image_in(&r.thumb) }) else { return false };
        let mut embedded = std::collections::BTreeMap::new();
        if r.embedded_count > 0 && !r.embedded.is_null() {
            for e in unsafe { std::slice::from_raw_parts(r.embedded, r.embedded_count) } {
                let Some(img) = (unsafe { image_in(&e.image) }) else { return false };
                embedded.insert(unsafe { text_arg(e.name, e.len) }, img);
            }
        }
        let text = |p: *const u16| unsafe { from_ptr(p) }.map(|s| s.to_vec()).unwrap_or_default();
        let info = library::CaptureInfo { mode: text(r.mode), window_title: text(r.window_title), application: text(r.application) };
        let version = unsafe { from_ptr(r.version) }.map(to_string_lossy).unwrap_or_default();
        match library::add(&unsafe { dir_arg(r.dir, r.dir_len) }, base, &thumb, &info, objects, embedded, &version) {
            Some(p) => wide_out(out, &p),
            None => false,
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_new_video_path(dir: *const u16, dir_len: usize, out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &library::new_video_path(&unsafe { dir_arg(dir, dir_len) })))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_audio_dir(out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &library::audio_dir()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_new_audio_path(mp3: bool, out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &library::new_audio_path(mp3)))
}

/// Moves the item to the Recycle Bin (owner: the window of the progress, may be null).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_delete(dir: *const u16, dir_len: usize, item: *const u16, item_len: usize, owner: *mut c_void) -> bool {
    guard(false, || {
        library::delete_item(&unsafe { dir_arg(dir, dir_len) }, &unsafe { wide_arg(item, item_len) }, HWND(owner))
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_cleanup(dir: *const u16, dir_len: usize, days: i32, keep_named: bool, keep_tagged: bool, recycle: bool) -> i32 {
    guard(0, || library::cleanup(&unsafe { dir_arg(dir, dir_len) }, days, keep_named, keep_tagged, recycle))
}

#[unsafe(no_mangle)]
pub extern "C" fn snapcore_library_cleanup_now() -> i32 {
    guard(0, library::cleanup_now)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_contains(dir: *const u16, dir_len: usize, path: *const u16, len: usize) -> bool {
    guard(false, || library::contains(&unsafe { dir_arg(dir, dir_len) }, &unsafe { wide_arg(path, len) }))
}

/// The capture's name in *out ("" if none).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_name(dir: *const u16, dir_len: usize, item: *const u16, item_len: usize, out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &library::name(&unsafe { dir_arg(dir, dir_len) }, &unsafe { wide_arg(item, item_len) })))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_library_set_name(dir: *const u16, dir_len: usize, item: *const u16, item_len: usize,
                                                   name: *const u16, name_len: usize) -> bool {
    guard(false, || {
        library::set_name(&unsafe { dir_arg(dir, dir_len) }, &unsafe { wide_arg(item, item_len) }, &unsafe { wide_arg(name, name_len) })
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_next_auto_name(folder: *const u16, folder_len: usize, prefix: *const u16, prefix_len: usize,
                                                 ext: *const u16, ext_len: usize, digits: i32, out: *mut SnapWStr) -> bool {
    guard(false, || {
        let (f, p, e) = unsafe { (wide_arg(folder, folder_len), wide_arg(prefix, prefix_len), wide_arg(ext, ext_len)) };
        wide_out(out, &library::next_auto_name(&f, &p, &e, digits))
    })
}

// ---------------------------------------------------------------- animated GIF (Gif.cpp)

/// Creates the GIF and writes its header; null if the file cannot be created (logged).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_gif_open(path: *const u16, len: usize, w: i32, h: i32, fps: f64, bpp: i32, dither: bool,
                                           delta: bool, comment: *const u8, comment_len: usize) -> *mut gif::GifWriter {
    guard(std::ptr::null_mut(), || {
        let Some(p) = (unsafe { path_arg(path, len) }) else { return std::ptr::null_mut() };
        match gif::GifWriter::open(&p, w, h, fps, bpp, dither, delta, unsafe { bytes_arg(comment, comment_len) }) {
            Some(g) => Box::into_raw(Box::new(g)),
            None => std::ptr::null_mut(),
        }
    })
}

/// Adds a frame of the GIF's size; false if it has another size or the file could not be written.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_gif_add_frame(g: *mut gif::GifWriter, frame: *const ImageIn, delay_cs: i32) -> bool {
    guard(false, || {
        let (Some(g), Some(f)) = (unsafe { g.as_mut() }, unsafe { frame.as_ref() }) else { return false };
        unsafe { image_in(f) }.is_some_and(|img| g.add_frame(&img, delay_cs))
    })
}

/// Writes the trailer, closes the file and frees `g`; false if anything could not be written.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_gif_close(g: *mut gif::GifWriter) -> bool {
    if g.is_null() {
        return false;
    }
    guard(false, || unsafe { Box::from_raw(g) }.close())
}

/// For the tests: sorts `v` by the byte at `shift` with the MSVC std::sort the GIF palette uses, or only with its heap
/// sort (std::make_heap then std::sort_heap).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_sort_by_byte(v: *mut u32, n: usize, shift: u32, heap_only: bool) {
    if v.is_null() || shift > 24 {
        return;
    }
    guard((), || {
        let v = unsafe { std::slice::from_raw_parts_mut(v, n) };
        let lt = |a: &u32, b: &u32| (a >> shift) & 255 < (b >> shift) & 255;
        if heap_only { gif::heap_sort(v, &lt) } else { gif::msvc_sort(v, lt) }
    })
}
