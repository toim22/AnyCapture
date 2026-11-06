//! In-memory image (BGRA 32 bpp, straight alpha), as `Image` in Image.h: 0xAARRGGBB per pixel, rows from top to bottom.

/// Metadata carried with an image (C29): what an imported photo had (EXIF) and what Snapnab writes when saving.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Meta {
    pub date_taken: String, // "YYYY:MM:DD HH:MM:SS" (EXIF form)
    pub make: String,
    pub model: String,
    pub software: String,
    pub gps: bool,
    pub lat: f64, // degrees, south negative
    pub lon: f64, // degrees, west negative
}

impl Meta {
    pub fn is_empty(&self) -> bool {
        self.date_taken.is_empty() && self.make.is_empty() && self.model.is_empty() && self.software.is_empty() && !self.gps
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Image {
    pub w: u32,
    pub h: u32,
    pub px: Vec<u32>,
    pub dpi: f32,           // resolution written to the file (0 = the encoder's default)
    pub meta: Option<Meta>, // metadata of the source photo
}

impl Image {
    pub fn new(w: u32, h: u32, fill: u32) -> Self {
        Image { w, h, px: vec![fill; w as usize * h as usize], dpi: 0.0, meta: None }
    }

    pub fn is_empty(&self) -> bool {
        self.w == 0 || self.h == 0
    }

    pub fn has_transparency(&self) -> bool {
        self.px.iter().any(|&p| p >> 24 != 255)
    }

    /// The image composed over an opaque color (JPEG and BMP have no alpha).
    pub fn flattened(&self, background: u32) -> Image {
        Image { px: self.px.iter().map(|&s| blend_over(background, s)).collect(), meta: self.meta.clone(), ..*self }
    }

    /// The pixels as bytes (B, G, R, A per pixel).
    pub fn bytes(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.px.as_ptr() as *const u8, self.px.len() * 4) }
    }

    pub fn bytes_mut(&mut self) -> &mut [u8] {
        unsafe { std::slice::from_raw_parts_mut(self.px.as_mut_ptr() as *mut u8, self.px.len() * 4) }
    }
}

/// `src` over `dst` with straight alpha, with the integer arithmetic of BlendOver in Image.cpp.
pub fn blend_over(d: u32, s: u32) -> u32 {
    let sa = (s >> 24) as i32;
    if sa == 255 {
        return s;
    }
    if sa == 0 {
        return d;
    }
    let da = (d >> 24) as i32;
    let oa = sa + da * (255 - sa) / 255;
    if oa == 0 {
        return 0;
    }
    let ch = |shift: u32| {
        let (sc, dc) = (((s >> shift) & 255) as i32, ((d >> shift) & 255) as i32);
        ((sc * sa + dc * da * (255 - sa) / 255) / oa) as u32
    };
    (oa as u32) << 24 | ch(16) << 16 | ch(8) << 8 | ch(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blending() {
        assert_eq!(blend_over(0xFFFFFFFF, 0xFF102030), 0xFF102030);
        assert_eq!(blend_over(0xFFFFFFFF, 0x00102030), 0xFFFFFFFF);
        assert_eq!(blend_over(0x00000000, 0x00102030), 0x00000000);
        // half-transparent black over white: (0*128 + 255*255*127/255) / 255 = 127
        assert_eq!(blend_over(0xFFFFFFFF, 0x80000000), 0xFF7F7F7F);
        let img = Image::new(2, 1, 0x80FF0000);
        assert!(img.has_transparency());
        let flat = img.flattened(0xFFFFFFFF);
        assert!(!flat.has_transparency());
        assert_eq!(flat.px[0], blend_over(0xFFFFFFFF, 0x80FF0000));
    }
}
