//! Renders each program's icon from its SVG artwork into a Windows .ico with PNG frames. The small frames (up to 32
//! pixels: the tray, title bars, small Explorer views) come from the `-small` artwork, which leaves out the glow and
//! has the frame corners thicker, so they stay sharp; the larger ones come from the full artwork.

use std::path::{Path, PathBuf};

use resvg::{tiny_skia, usvg};

const SMALL: [u32; 4] = [16, 20, 24, 32];
const LARGE: [u32; 6] = [40, 48, 64, 96, 128, 256];

fn render(svg: &Path, px: u32) -> Vec<u8> {
    let data = std::fs::read(svg).unwrap_or_else(|e| panic!("{}: {e}", svg.display()));
    let tree = usvg::Tree::from_data(&data, &usvg::Options::default()).expect("valid SVG");
    let mut pm = tiny_skia::Pixmap::new(px, px).unwrap();
    let k = px as f32 / tree.size().width().max(tree.size().height());
    resvg::render(&tree, tiny_skia::Transform::from_scale(k, k), &mut pm.as_mut());
    pm.encode_png().unwrap()
}

fn ico(out: &Path, frames: &[(u32, Vec<u8>)]) {
    let mut f = Vec::new();
    f.extend_from_slice(&[0, 0, 1, 0]);
    f.extend_from_slice(&(frames.len() as u16).to_le_bytes());
    let mut offset = 6 + 16 * frames.len() as u32;
    for (px, png) in frames {
        let side = (*px % 256) as u8; // 0 means 256
        f.extend_from_slice(&[side, side, 0, 0]);
        f.extend_from_slice(&1u16.to_le_bytes()); // planes
        f.extend_from_slice(&32u16.to_le_bytes()); // bits per pixel
        f.extend_from_slice(&(png.len() as u32).to_le_bytes());
        f.extend_from_slice(&offset.to_le_bytes());
        offset += png.len() as u32;
    }
    for (_, png) in frames {
        f.extend_from_slice(png);
    }
    std::fs::write(out, f).unwrap();
    println!("{}", out.display());
}

fn main() {
    let res = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../rust/resources");
    for (art, name) in [("capture", "AnyCapture.ico"), ("editor", "AnyCaptureEditor.ico")] {
        let small = res.join(format!("icons/{art}-small.svg"));
        let large = res.join(format!("icons/{art}.svg"));
        let mut frames: Vec<(u32, Vec<u8>)> = SMALL.iter().map(|&px| (px, render(&small, px))).collect();
        frames.extend(LARGE.iter().map(|&px| (px, render(&large, px))));
        ico(&res.join(name), &frames);
    }
}
