//! snapcore — Snapnab's shared Rust layer, linked into both programs while the C++ code moves to Rust
//! (docs/ARCHITECTURE.md). The C++ side reaches it through the C ABI in `ffi` (declared in include/snapcore.h).
//!
//! - `container`: the compound file inside every .snap
//! - `log`: %LOCALAPPDATA%\Snapnab\Logs\<program>.log
//! - `reg`: the settings in HKCU\Software\Snapnab\1
//! - `i18n`: the interface language and the lang\<code>.po catalogs
//! - `xml`: the XML reader and writer (presets, .snap documents, Names.xml)

pub mod container;
pub mod i18n;
pub mod log;
pub mod paths;
pub mod reg;
pub mod wide;
pub mod xml;

mod ffi;
