//! GDI+ through its flat API, for the drawing the C++ did with the Gdiplus classes: each wrapper makes the same flat call
//! the class did (Bitmap(w, h, stride, PixelFormat32bppARGB, scan0) → GdipCreateBitmapFromScan0, Graphics(image) →
//! GdipGetImageGraphicsContext…), so the pixels come out the same. Statuses are ignored, as the C++ did.

use std::marker::PhantomData;
use std::sync::OnceLock;

use windows::Win32::Graphics::GdiPlus::*;
use windows::core::PCWSTR;

use crate::image::Image;

/// PixelFormat32bppARGB (10 | 32 bpp | alpha | GDI | canonical).
const PIXEL_FORMAT_32BPP_ARGB: i32 = 10 | (32 << 8) | 0x40000 | 0x20000 | 0x200000;

/// GDI+ for the process: the programs start it themselves; this covers the Rust tests (never shut down).
pub fn startup() {
    static STARTED: OnceLock<()> = OnceLock::new();
    STARTED.get_or_init(|| {
        let input = GdiplusStartupInput { GdiplusVersion: 1, ..Default::default() };
        let mut token = 0usize;
        unsafe { GdiplusStartup(&mut token, &input, std::ptr::null_mut()) };
    });
}

/// A GDI+ bitmap over the pixels of an Image (Image::WrapGdip: no copy, drawn into in place).
pub struct Bitmap<'a> {
    raw: *mut GpBitmap,
    _px: PhantomData<&'a mut [u32]>,
}

impl<'a> Bitmap<'a> {
    pub fn wrap(img: &'a mut Image) -> Bitmap<'a> {
        Self::over(img.w, img.h, img.px.as_mut_ptr())
    }

    /// A source bitmap: GDI+ only reads it (the C++ cast the const away).
    pub fn wrap_read(img: &'a Image) -> Bitmap<'a> {
        Self::over(img.w, img.h, img.px.as_ptr() as *mut u32)
    }

    fn over(w: u32, h: u32, px: *mut u32) -> Bitmap<'a> {
        startup();
        let mut raw = std::ptr::null_mut();
        unsafe {
            GdipCreateBitmapFromScan0(w as i32, h as i32, w as i32 * 4, PIXEL_FORMAT_32BPP_ARGB, Some(px as *const u8), &mut raw)
        };
        Bitmap { raw, _px: PhantomData }
    }

    fn image(&self) -> *mut GpImage {
        self.raw as *mut GpImage
    }
}

impl Drop for Bitmap<'_> {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { GdipDisposeImage(self.raw as *mut GpImage) };
        }
    }
}

pub struct Graphics<'b> {
    raw: *mut GpGraphics,
    _target: PhantomData<&'b ()>,
}

impl<'b> Graphics<'b> {
    pub fn new(target: &'b Bitmap<'_>) -> Graphics<'b> {
        let mut raw = std::ptr::null_mut();
        unsafe { GdipGetImageGraphicsContext(target.image(), &mut raw) };
        Graphics { raw, _target: PhantomData }
    }

    pub fn interpolation(&self, m: InterpolationMode) {
        unsafe { GdipSetInterpolationMode(self.raw, m) };
    }
    pub fn smoothing(&self, m: SmoothingMode) {
        unsafe { GdipSetSmoothingMode(self.raw, m) };
    }
    pub fn pixel_offset(&self, m: PixelOffsetMode) {
        unsafe { GdipSetPixelOffsetMode(self.raw, m) };
    }
    pub fn compositing(&self, m: CompositingMode) {
        unsafe { GdipSetCompositingMode(self.raw, m) };
    }
    pub fn text_hint(&self, h: TextRenderingHint) {
        unsafe { GdipSetTextRenderingHint(self.raw, h) };
    }
    pub fn translate(&self, dx: f32, dy: f32) {
        unsafe { GdipTranslateWorldTransform(self.raw, dx, dy, MatrixOrderPrepend) };
    }
    pub fn rotate(&self, degrees: f32) {
        unsafe { GdipRotateWorldTransform(self.raw, degrees, MatrixOrderPrepend) };
    }
    /// DrawImage(image, x, y, w, h)
    pub fn draw_image(&self, img: &Bitmap, x: f32, y: f32, w: f32, h: f32) {
        unsafe { GdipDrawImageRect(self.raw, img.image(), x, y, w, h) };
    }
    /// DrawImage(image, Rect dst, srcx, srcy, srcw, srch, UnitPixel, attributes)
    pub fn draw_image_rect(&self, img: &Bitmap, dst: [i32; 4], src: [i32; 4], attr: &ImageAttributes) {
        unsafe {
            GdipDrawImageRectRectI(self.raw, img.image(), dst[0], dst[1], dst[2], dst[3], src[0], src[1], src[2], src[3],
                                   UnitPixel, attr.0, 0, std::ptr::null_mut())
        };
    }
    pub fn fill_polygon(&self, brush: &Brush, pts: &[PointF]) {
        unsafe { GdipFillPolygon(self.raw, brush.0, pts.as_ptr(), pts.len() as i32, FillModeAlternate) };
    }
    pub fn draw_polygon(&self, pen: &Pen, pts: &[PointF]) {
        unsafe { GdipDrawPolygon(self.raw, pen.0, pts.as_ptr(), pts.len() as i32) };
    }
    pub fn draw_line(&self, pen: &Pen, x1: f32, y1: f32, x2: f32, y2: f32) {
        unsafe { GdipDrawLine(self.raw, pen.0, x1, y1, x2, y2) };
    }
    /// DrawString(text, -1, font, rect, format, brush); `text` without the terminating zero.
    pub fn draw_string(&self, text: &[u16], font: &Font, rect: RectF, format: &StringFormat, brush: &Brush) {
        let t: Vec<u16> = text.iter().copied().chain(std::iter::once(0)).collect();
        unsafe { GdipDrawString(self.raw, PCWSTR(t.as_ptr()), -1, font.font, &rect, format.0, brush.0) };
    }
}

impl Drop for Graphics<'_> {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { GdipDeleteGraphics(self.raw) };
        }
    }
}

pub struct Brush(*mut GpBrush);

impl Brush {
    /// SolidBrush(Color)
    pub fn solid(argb: u32) -> Brush {
        startup();
        let mut b = std::ptr::null_mut();
        unsafe { GdipCreateSolidFill(argb, &mut b) };
        Brush(b as *mut GpBrush)
    }
    /// LinearGradientBrush(point1, point2, color1, color2): WrapModeTile.
    pub fn linear(p1: PointF, p2: PointF, c1: u32, c2: u32) -> Brush {
        startup();
        let mut b = std::ptr::null_mut();
        unsafe { GdipCreateLineBrush(&p1, &p2, c1, c2, WrapModeTile, &mut b) };
        Brush(b as *mut GpBrush)
    }
}

impl Drop for Brush {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { GdipDeleteBrush(self.0) };
        }
    }
}

pub struct Pen(*mut GpPen);

impl Pen {
    /// Pen(Color, width): UnitWorld.
    pub fn new(argb: u32, width: f32) -> Pen {
        startup();
        let mut p = std::ptr::null_mut();
        unsafe { GdipCreatePen1(argb, width, UnitWorld, &mut p) };
        Pen(p)
    }
    pub fn dash(&self, style: DashStyle) {
        unsafe { GdipSetPenDashStyle(self.0, style) };
    }
}

impl Drop for Pen {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { GdipDeletePen(self.0) };
        }
    }
}

/// FontFamily(name) and Font(&family, size, style, UnitPixel).
pub struct Font {
    family: *mut GpFontFamily,
    font: *mut GpFont,
}

impl Font {
    pub fn new(family: &str, px: f32, style: FontStyle) -> Font {
        startup();
        let name: Vec<u16> = family.encode_utf16().chain(std::iter::once(0)).collect();
        let (mut fam, mut font) = (std::ptr::null_mut(), std::ptr::null_mut());
        unsafe {
            GdipCreateFontFamilyFromName(PCWSTR(name.as_ptr()), std::ptr::null_mut(), &mut fam);
            GdipCreateFont(fam, px, style.0, UnitPixel, &mut font);
        }
        Font { family: fam, font }
    }
}

impl Drop for Font {
    fn drop(&mut self) {
        unsafe {
            if !self.font.is_null() {
                GdipDeleteFont(self.font);
            }
            if !self.family.is_null() {
                GdipDeleteFontFamily(self.family);
            }
        }
    }
}

/// StringFormat() (no flags, LANG_NEUTRAL).
pub struct StringFormat(*mut GpStringFormat);

impl StringFormat {
    pub fn new() -> StringFormat {
        startup();
        let mut f = std::ptr::null_mut();
        unsafe { GdipCreateStringFormat(0, 0, &mut f) };
        StringFormat(f)
    }
    pub fn align(&self, a: StringAlignment) {
        unsafe { GdipSetStringFormatAlign(self.0, a) };
    }
    pub fn line_align(&self, a: StringAlignment) {
        unsafe { GdipSetStringFormatLineAlign(self.0, a) };
    }
    pub fn trimming(&self, t: StringTrimming) {
        unsafe { GdipSetStringFormatTrimming(self.0, t) };
    }
}

impl Drop for StringFormat {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { GdipDeleteStringFormat(self.0) };
        }
    }
}

pub struct ImageAttributes(*mut GpImageAttributes);

impl ImageAttributes {
    pub fn new() -> ImageAttributes {
        startup();
        let mut a = std::ptr::null_mut();
        unsafe { GdipCreateImageAttributes(&mut a) };
        ImageAttributes(a)
    }
    /// SetWrapMode(wrap): the border color is Color() (opaque black), no clamp.
    pub fn wrap_mode(&self, w: WrapMode) {
        unsafe { GdipSetImageAttributesWrapMode(self.0, w, 0xFF000000, false) };
    }
}

impl Drop for ImageAttributes {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { GdipDisposeImageAttributes(self.0) };
        }
    }
}

pub fn pt(x: f32, y: f32) -> PointF {
    PointF { X: x, Y: y }
}

pub fn rect(x: f32, y: f32, w: f32, h: f32) -> RectF {
    RectF { X: x, Y: y, Width: w, Height: h }
}

/// ScaleImage: GDI+ high quality bicubic (or nearest neighbor), half pixel offset, source copy, tile flip edges.
pub fn scale_image(src: &Image, nw: i32, nh: i32, high_quality: bool) -> Image {
    if src.is_empty() || nw <= 0 || nh <= 0 {
        return Image::default();
    }
    let mut dst = Image::new(nw as u32, nh as u32, 0);
    {
        let sb = Bitmap::wrap_read(src);
        let db = Bitmap::wrap(&mut dst);
        let g = Graphics::new(&db);
        g.interpolation(if high_quality { InterpolationModeHighQualityBicubic } else { InterpolationModeNearestNeighbor });
        g.pixel_offset(PixelOffsetModeHalf);
        g.compositing(CompositingModeSourceCopy);
        let ia = ImageAttributes::new();
        ia.wrap_mode(WrapModeTileFlipXY);
        g.draw_image_rect(&sb, [0, 0, nw, nh], [0, 0, src.w as i32, src.h as i32], &ia);
    }
    dst
}

/// MakeThumbnail: fitted inside max_w × max_h, never enlarged.
pub fn make_thumbnail(img: &Image, max_w: i32, max_h: i32) -> Image {
    if img.is_empty() {
        return Image::default();
    }
    let s = 1.0f64.min((max_w as f64 / img.w as f64).min(max_h as f64 / img.h as f64));
    scale_image(img, 1.max((img.w as f64 * s) as i32), 1.max((img.h as f64 * s) as i32), true)
}
