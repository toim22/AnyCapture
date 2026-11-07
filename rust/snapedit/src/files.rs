//! Files of the Editor: images in and out, PDF, the library's names and thumbnails.

use std::path::Path;

use snapcore::codec::{self, Format, SaveOptions};
use snapcore::image::Image;
use snapcore::library;

fn w(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

pub fn library_dir() -> String {
    String::from_utf16_lossy(&library::library_dir())
}

pub fn load_image(path: &str) -> Option<Image> {
    codec::load(Path::new(path)).ok()
}

pub fn save_image(path: &str, img: &Image, jpeg_quality: i32) -> Result<(), String> {
    let ext = crate::doc::ext_of(path);
    let opt = SaveOptions { jpeg_quality, ..SaveOptions::from_settings() };
    if codec::save_file(Path::new(path), img, Format::from_ext(&ext), &opt, crate::VERSION) {
        Ok(())
    } else {
        Err(snapcore::tr!("Não foi possível salvar o arquivo."))
    }
}

pub fn save_pdf(path: &str, img: &Image, links: &[(snapcore::doc::RectF, String)]) -> Result<(), String> {
    let links: Vec<snapcore::pdf::Link> = links
        .iter()
        .map(|(r, u)| snapcore::pdf::Link { x: r.X, y: r.Y, w: r.Width, h: r.Height, url: w(u) })
        .collect();
    snapcore::pdf::save_pdf(&w(path), img, &links).map_err(|e| format!("{e:?}"))
}

/// The name the capture was given in the library (Names.xml), or "".
pub fn capture_name(path: &str) -> String {
    let p = w(path);
    String::from_utf16_lossy(&library::name(&library::dir_of(&p), &p))
}

pub fn set_capture_name(path: &str, name: &str) -> bool {
    let p = w(path);
    library::set_name(&library::dir_of(&p), &p, &w(name))
}

pub fn write_thumbnail(path: &str, img: &Image) -> bool {
    let p = w(path);
    library::write_thumbnail(&library::dir_of(&p), &p, img, crate::VERSION)
}

/// A new .snap path in the library.
pub fn new_library_path() -> String {
    let dir = library_dir();
    format!("{dir}\\{}.snap", String::from_utf16_lossy(&library::new_guid()))
}
