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
use crate::{i18n, log, reg, xml};

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
    let text = |s: &str| crate::wide::to_wide(s);
    let texts = match &img.meta {
        Some(m) => [text(&m.date_taken), text(&m.make), text(&m.model), text(&m.software)],
        None => [vec![0], vec![0], vec![0], vec![0]],
    };
    Box::into_raw(Box::new(Decoded { img, texts }))
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

/// An image to encode and how (SaveOptions and ImageMeta of Image.h); the strings are nul-terminated UTF-16 or null.
#[repr(C)]
pub struct EncodeRequest {
    pub w: u32,
    pub h: u32,
    pub px: *const u32,
    pub dpi: f32,
    pub format: i32,
    pub jpeg_quality: i32,
    pub avif_quality: i32,
    pub strip_metadata: bool,
    pub has_meta: bool,
    pub date_taken: *const u16,
    pub make: *const u16,
    pub model: *const u16,
    pub software: *const u16,
    pub gps: bool,
    pub lat: f64,
    pub lon: f64,
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
        let n = r.w as usize * r.h as usize;
        if out.is_null() || (n > 0 && r.px.is_null()) {
            return Err(EncodeError::Failed);
        }
        let text = |p: *const u16| unsafe { from_ptr(p) }.map(to_string_lossy).unwrap_or_default();
        let img = Image {
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
        };
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
