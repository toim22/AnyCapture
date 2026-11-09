//! Every icon draws something; ANYCAPTURE_ICON_SHEET=<file.png> also writes them all on one sheet to look at.

use snapcore::codec::{self, Format, SaveOptions};
use snapcore::icons::{Icon, render};
use snapcore::image::Image;

#[test]
fn every_icon_draws() {
    let (px, cols) = (48, 12);
    let rows = Icon::ALL.len().div_ceil(cols) as i32;
    let mut sheet = Image::sized(cols as i32 * (px + 8), rows * (px + 8), 0xFFFF_FFFF);
    let sw = sheet.w as i32;
    for (i, &id) in Icon::ALL.iter().enumerate() {
        let img = render(id, px, 0xFF34_3840);
        let inked = img.px.iter().filter(|&&p| p >> 24 > 40).count();
        assert!(inked > 20, "{id:?} draws nothing");
        let (ox, oy) = ((i % cols) as i32 * (px + 8) + 4, (i / cols) as i32 * (px + 8) + 4);
        for y in 0..px {
            for x in 0..px {
                let s = img.px[(y * px + x) as usize];
                let a = s >> 24;
                let d = &mut sheet.px[((oy + y) * sw + ox + x) as usize];
                let dv = *d;
                let mix = |sh: u32| (((s >> sh & 255) * a + (dv >> sh & 255) * (255 - a)) / 255) << sh;
                *d = 0xFF00_0000 | mix(16) | mix(8) | mix(0);
            }
        }
    }
    if let Ok(out) = std::env::var("ANYCAPTURE_ICON_SHEET") {
        assert!(codec::save_file(std::path::Path::new(&out), &sheet, Format::Png, &SaveOptions::default(), "test"));
    }
}
