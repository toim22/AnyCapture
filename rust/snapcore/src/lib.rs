//! snapcore — the .snap container in Rust (pilot of the Rust migration).
//!
//! A .snap file is a 24-byte header followed by an OLE compound file (see src/common/SnapFile.h). This crate reads and
//! writes that container with the `cfb` crate instead of the Windows structured storage API (StgOpenStorageOnILockBytes),
//! so a damaged or hostile file is parsed by memory-safe code. What goes inside the streams (the XML documents and the
//! PNG images) is still handled by the C++ side.
//!
//! C ABI (declared in include/snapcore.h). Stream paths are UTF-8 with '/' between storages, e.g. "Frame0/Document" or
//! "Frame0/ImageStorage/Image_0.png". No panic crosses the boundary: every entry point catches it and reports a failure.

use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;

use cfb::{CompoundFile, Version};

/// 0xCB + "SNAPNAB" + 16-byte format identifier (same bytes as kHeader in SnapFile.cpp)
pub const HEADER: [u8; 24] = [
    0xCB, b'S', b'N', b'A', b'P', b'N', b'A', b'B', 0x5A, 0x3C, 0x91, 0x0E, 0x6B, 0x2D, 0x4F, 0x18, 0xA7, 0x44, 0xC2, 0x9B,
    0x10, 0x7E, 0xD3, 0x55,
];

/// Largest stream read into memory: a bigger one is treated as a damaged file (a capture of 16k × 16k pixels as PNG is
/// far below this).
const MAX_STREAM: u64 = 1 << 30;

/// The file seen from after the header, which is what the compound file reader and writer expect.
struct Offset<F> {
    inner: F,
}

impl<F: Seek> Offset<F> {
    fn new(mut inner: F) -> io::Result<Self> {
        inner.seek(SeekFrom::Start(HEADER.len() as u64))?;
        Ok(Offset { inner })
    }
}

impl<F: Read> Read for Offset<F> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.inner.read(buf)
    }
}

impl<F: Write> Write for Offset<F> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.inner.write(buf)
    }
    fn flush(&mut self) -> io::Result<()> {
        self.inner.flush()
    }
}

impl<F: Seek> Seek for Offset<F> {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        let off = HEADER.len() as u64;
        let pos = match pos {
            SeekFrom::Start(p) => SeekFrom::Start(p.checked_add(off).ok_or(io::ErrorKind::InvalidInput)?),
            other => other,
        };
        let p = self.inner.seek(pos)?;
        p.checked_sub(off).ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "seek before the .snap header"))
    }
}

// ---------------------------------------------------------------- safe API (also used by the Rust tests)

pub struct Reader {
    cf: CompoundFile<Offset<File>>,
}

impl Reader {
    pub fn open(path: &std::path::Path) -> io::Result<Reader> {
        let mut f = File::open(path)?;
        let mut h = [0u8; 24];
        f.read_exact(&mut h)?;
        if h != HEADER {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "not a .snap file"));
        }
        Ok(Reader { cf: CompoundFile::open(Offset::new(f)?)? })
    }

    pub fn read(&mut self, stream: &str) -> io::Result<Vec<u8>> {
        let mut s = self.cf.open_stream(stream_path(stream))?;
        let len = s.len();
        if len > MAX_STREAM {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "stream too large"));
        }
        let mut out = Vec::with_capacity(len as usize);
        s.read_to_end(&mut out)?;
        Ok(out)
    }
}

pub struct Writer {
    cf: CompoundFile<Offset<File>>,
}

impl Writer {
    /// Creates (or truncates) the file. V3 (512-byte sectors), like the Windows API writes them, so older Snapnab
    /// versions open the result too.
    pub fn create(path: &std::path::Path) -> io::Result<Writer> {
        let mut f = File::create(path)?;
        f.write_all(&HEADER)?;
        Ok(Writer { cf: CompoundFile::create_with_version(Version::V3, Offset::new(f)?)? })
    }

    /// Writes one stream, creating the storages on its path as needed.
    pub fn write(&mut self, stream: &str, data: &[u8]) -> io::Result<()> {
        let path = stream_path(stream);
        let mut dir = PathBuf::from("/");
        if let Some(parent) = path.parent() {
            for c in parent.components().skip(1) {
                dir.push(c);
                if !self.cf.exists(&dir) {
                    self.cf.create_storage(&dir)?;
                }
            }
        }
        let mut s = self.cf.create_stream(&path)?;
        s.write_all(data)?;
        s.flush()
    }

    /// Writes everything out and closes the file.
    pub fn finish(mut self) -> io::Result<()> {
        self.cf.flush()?;
        let f = self.cf.into_inner().inner;
        f.sync_data()
    }
}

fn stream_path(stream: &str) -> PathBuf {
    let mut p = PathBuf::from("/");
    for part in stream.split('/').filter(|s| !s.is_empty()) {
        p.push(part);
    }
    p
}

pub fn is_snap(path: &std::path::Path) -> bool {
    let mut h = [0u8; 24];
    File::open(path).and_then(|mut f| f.read_exact(&mut h)).is_ok() && h == HEADER
}

// ---------------------------------------------------------------- C ABI

/// A buffer allocated by snapcore; give it back with snapcore_buf_free.
#[repr(C)]
pub struct SnapBuf {
    pub data: *mut u8,
    pub len: usize,
}

unsafe fn path_arg(path: *const u16, len: usize) -> Option<PathBuf> {
    if path.is_null() {
        return None;
    }
    let w = unsafe { std::slice::from_raw_parts(path, len) };
    Some(PathBuf::from(String::from_utf16(w).ok()?))
}

unsafe fn str_arg<'a>(s: *const u8, len: usize) -> Option<&'a str> {
    if s.is_null() {
        return None;
    }
    std::str::from_utf8(unsafe { std::slice::from_raw_parts(s, len) }).ok()
}

fn guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

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

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!("snapcore-test-{}-{name}", std::process::id()))
    }

    #[test]
    fn round_trip() {
        let p = tmp("rt.snap");
        let mut w = Writer::create(&p).unwrap();
        w.write("Index", b"<Index/>").unwrap();
        w.write("Frame0/Document", b"<Document/>").unwrap();
        w.write("Frame0/ImageStorage/Image_0.png", &vec![7u8; 100_000]).unwrap();
        w.write("Frame0/ImageStorage/Image_1.png", b"").unwrap();
        w.finish().unwrap();
        assert!(is_snap(&p));
        let mut r = Reader::open(&p).unwrap();
        assert_eq!(r.read("Index").unwrap(), b"<Index/>");
        assert_eq!(r.read("/Frame0/Document").unwrap(), b"<Document/>");
        assert_eq!(r.read("Frame0/ImageStorage/Image_0.png").unwrap(), vec![7u8; 100_000]);
        assert!(r.read("Frame0/ImageStorage/Image_1.png").unwrap().is_empty());
        assert!(r.read("Frame1/Document").is_err());
        drop(r);
        std::fs::remove_file(&p).unwrap();
    }

    #[test]
    fn rejects_damaged_files() {
        let p = tmp("bad.snap");
        std::fs::write(&p, b"not a snap file at all").unwrap();
        assert!(!is_snap(&p) && Reader::open(&p).is_err());
        let mut junk = HEADER.to_vec();
        junk.extend((0..4096u32).map(|i| (i.wrapping_mul(2654435761) >> 13) as u8));
        std::fs::write(&p, &junk).unwrap();
        assert!(is_snap(&p) && Reader::open(&p).is_err());       // header right, container garbage: an error, no crash
        std::fs::remove_file(&p).unwrap();
    }
}
