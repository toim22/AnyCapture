//! snapcore — Snapnab's shared Rust layer, linked into both programs while the C++ code moves to Rust
//! (docs/ARCHITECTURE.md). The C++ side reaches it through the C ABI in `ffi` (declared in include/snapcore.h).
//!
//! - `container`: the compound file inside every .snap
//! - `log`: %LOCALAPPDATA%\Snapnab\Logs\<program>.log
//! - `reg`: the settings in HKCU\Software\Snapnab\1
//! - `i18n`: the interface language and the lang\<code>.po catalogs
//! - `xml`: the XML reader and writer (presets, .snap documents, Names.xml)
//! - `image`, `codec`: images in memory and image files through the Windows codecs (WIC)
//! - `snap`: the .snap document (index, frame documents, base and embedded images)
//! - `library`: the capture library (its folder, thumbnails, capture names, cleanup)
//! - `gif`: the animated GIF writer
//! - `crt`: printf-style number formatting through the C runtime (the same rounding the C++ had)
//! - `gdip`: the GDI+ flat API (scaling, text, polygons) and image scaling
//! - `fx`: the image effects and the capture preset effects
//! - `beautify`: Beautify (background, padding, rounded corners, shadow)
//! - `cv`: computer vision (magic wand, masks, inpainting, translation detection)
//! - `pdf`: the PDF writer and the page breaks of long captures

pub mod beautify;
pub mod codec;
pub mod container;
pub mod crt;
pub mod cv;
pub mod fx;
pub mod gdip;
pub mod gif;
pub mod i18n;
pub mod image;
pub mod library;
pub mod log;
pub mod paths;
pub mod pdf;
pub mod reg;
pub mod snap;
pub mod wide;
pub mod xml;

mod ffi;
mod ffi_pixels;
