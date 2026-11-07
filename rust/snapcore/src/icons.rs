//! The vector icons (Icons.cpp), drawn with GDI+ exactly as the C++ drew them: each icon is laid out on a 24×24 grid
//! and scaled to the rectangle it is drawn in. `render` turns one into pixels, which the egui windows show as a texture
//! and the native menus as a bitmap.


use windows::Win32::Graphics::GdiPlus::{
    DashStyleDash, FontStyleBold, LineCapFlat, LineCapRound, LineJoinMiter, LineJoinRound, SmoothingModeAntiAlias,
    StringAlignmentCenter, TextRenderingHintAntiAliasGridFit,
};

use crate::gdip::{Bitmap, Brush, Font, Graphics, Path, Pen as GPen, StringFormat, pt, rect};
use crate::image::Image;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Icon {
    Capture, Image, Video, Record, Pause, Stop, Restart, Cancel, Mic, MicOff, Speaker, SpeakerOff, Camera,
    Select, Arrow, Line, Rect, Ellipse, Pen, Highlighter, Text, Callout, Step, Stamp, Blur, Spotlight,
    Magnify, Fill, Eraser, Crop, Selection, Wand, Cutout, Eyedropper, Undo, Redo, Editor, Folder, Gear,
    Copy, Save, Share, Ocr, Library, CameraFill, VideoFill, Webcam, WebcamOff,
    Mail, Print, NewDoc, Trash, Layers, Rotate, Effects,
    Star, Clipboard, Program, Duplicate, SaveAs, NewClipboard, AutoTrim, DeleteObject,
    FxBorder, FxEdges, FxCaption, FxPerspective, FxPageCurl, FxShadow, FxFilters, FxColorAdjust, FxColorReplace,
    FxSpotlight, FxWatermark,
    Lock, Unlock, Eye, EyeOff, Pin, AlignLeft, AlignCenter, AlignRight, AlignTop, AlignMiddle, AlignBottom,
    DistributeH, DistributeV,
    Ruler, Link, Group, Sliders,
    Check, Qr, Upload, Sparkle, Compare, Combine, Clock, Cursor, Contrast, Split, Focus, Beautify,
}

impl Icon {
    pub const ALL: [Icon; 103] = [
        Icon::Capture, Icon::Image, Icon::Video, Icon::Record, Icon::Pause, Icon::Stop, Icon::Restart, Icon::Cancel,
        Icon::Mic, Icon::MicOff, Icon::Speaker, Icon::SpeakerOff, Icon::Camera, Icon::Select, Icon::Arrow,
        Icon::Line, Icon::Rect, Icon::Ellipse, Icon::Pen, Icon::Highlighter, Icon::Text, Icon::Callout, Icon::Step,
        Icon::Stamp, Icon::Blur, Icon::Spotlight, Icon::Magnify, Icon::Fill, Icon::Eraser, Icon::Crop,
        Icon::Selection, Icon::Wand, Icon::Cutout, Icon::Eyedropper, Icon::Undo, Icon::Redo, Icon::Editor,
        Icon::Folder, Icon::Gear, Icon::Copy, Icon::Save, Icon::Share, Icon::Ocr, Icon::Library, Icon::CameraFill,
        Icon::VideoFill, Icon::Webcam, Icon::WebcamOff, Icon::Mail, Icon::Print, Icon::NewDoc, Icon::Trash,
        Icon::Layers, Icon::Rotate, Icon::Effects, Icon::Star, Icon::Clipboard, Icon::Program, Icon::Duplicate,
        Icon::SaveAs, Icon::NewClipboard, Icon::AutoTrim, Icon::DeleteObject, Icon::FxBorder, Icon::FxEdges,
        Icon::FxCaption, Icon::FxPerspective, Icon::FxPageCurl, Icon::FxShadow, Icon::FxFilters,
        Icon::FxColorAdjust, Icon::FxColorReplace, Icon::FxSpotlight, Icon::FxWatermark, Icon::Lock, Icon::Unlock,
        Icon::Eye, Icon::EyeOff, Icon::Pin, Icon::AlignLeft, Icon::AlignCenter, Icon::AlignRight, Icon::AlignTop,
        Icon::AlignMiddle, Icon::AlignBottom, Icon::DistributeH, Icon::DistributeV, Icon::Ruler, Icon::Link,
        Icon::Group, Icon::Sliders, Icon::Check, Icon::Qr, Icon::Upload, Icon::Sparkle, Icon::Compare,
        Icon::Combine, Icon::Clock, Icon::Cursor, Icon::Contrast, Icon::Split, Icon::Focus, Icon::Beautify,
    ];
}

const WHITE: u32 = 0xFFFF_FFFF;

fn alpha(c: u32, a: u8) -> u32 {
    (c & 0x00FF_FFFF) | (a as u32) << 24
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

fn centered() -> StringFormat {
    let f = StringFormat::new();
    f.align(StringAlignmentCenter);
    f.line_align(StringAlignmentCenter);
    f
}

/// The icon in the rectangle (x, y, w, h), in the color `c` (ARGB).
pub fn draw(g: &Graphics, id: Icon, x: f32, y: f32, w: f32, h: f32, c: u32) {
    use Icon::*;
    let st = g.save();
    g.smoothing(SmoothingModeAntiAlias);
    g.translate(x, y);
    g.scale(w / 24.0, h / 24.0);
    let p = GPen::new(c, 1.8);
    p.line_join(LineJoinRound);
    p.start_cap(LineCapRound);
    p.end_cap(LineCapRound);
    let thin = GPen::new(c, 1.2);
    let (p3, p26, p24) = (GPen::new(c, 3.0), GPen::new(c, 2.6), GPen::new(c, 2.4));
    p3.end_cap(LineCapRound);
    p26.start_cap(LineCapRound);
    p26.end_cap(LineCapRound);
    p24.end_cap(LineCapRound);
    let b = Brush::solid(c);
    let faint = Brush::solid(alpha(c, 70));
    let arrow_head = |tx: f32, ty: f32, ang: f32, sz: f32| {
        let pts = [pt(tx, ty), pt(tx - sz * (ang - 0.45).cos(), ty - sz * (ang - 0.45).sin()),
                   pt(tx - sz * (ang + 0.45).cos(), ty - sz * (ang + 0.45).sin())];
        g.fill_polygon(&b, &pts);
    };
    let lines = |pen: &GPen, pts: &[(f32, f32)]| g.draw_lines(pen, &pts.iter().map(|&(x, y)| pt(x, y)).collect::<Vec<_>>());
    let poly = |pen: &GPen, pts: &[(f32, f32)]| g.draw_polygon(pen, &pts.iter().map(|&(x, y)| pt(x, y)).collect::<Vec<_>>());
    let fill_poly = |br: &Brush, pts: &[(f32, f32)]| g.fill_polygon(br, &pts.iter().map(|&(x, y)| pt(x, y)).collect::<Vec<_>>());
    let rc = |pen: &GPen, x: f32, y: f32, w: f32, h: f32| g.draw_rect(pen, rect(x, y, w, h));
    let fill_rc = |br: &Brush, x: f32, y: f32, w: f32, h: f32| g.fill_rect(br, rect(x, y, w, h));
    let arc = |pen: &GPen, x: f32, y: f32, w: f32, h: f32, a: f32, s: f32| g.draw_arc(pen, rect(x, y, w, h), a, s);
    let text = |s: &str, family: &str, px: f32, r: (f32, f32, f32, f32), br: &Brush| {
        let f = Font::new(family, px, FontStyleBold);
        g.draw_string(&wide(s), &f, rect(r.0, r.1, r.2, r.3), &centered(), br);
    };
    match id {
        Capture => {
            g.draw_line(&p, 12.0, 2.0, 12.0, 8.0);
            g.draw_line(&p, 12.0, 16.0, 12.0, 22.0);
            g.draw_line(&p, 2.0, 12.0, 8.0, 12.0);
            g.draw_line(&p, 16.0, 12.0, 22.0, 12.0);
            g.draw_ellipse(&p, 7.0, 7.0, 10.0, 10.0);
        }
        Image => {
            rc(&p, 3.0, 5.0, 18.0, 14.0);
            lines(&p, &[(5.0, 17.0), (10.0, 11.0), (14.0, 15.0)]);
            lines(&p, &[(12.0, 14.0), (15.0, 11.0), (19.0, 17.0)]);
            g.fill_ellipse(&b, 15.0, 7.0, 3.0, 3.0);
        }
        Video => {
            rc(&p, 2.5, 6.0, 13.0, 12.0);
            poly(&p, &[(16.0, 10.0), (21.5, 7.0), (21.5, 17.0)]);
        }
        Record => g.fill_ellipse(&b, 5.0, 5.0, 14.0, 14.0),
        Pause => {
            fill_rc(&b, 6.0, 5.0, 4.0, 14.0);
            fill_rc(&b, 14.0, 5.0, 4.0, 14.0);
        }
        Stop => fill_rc(&b, 6.0, 6.0, 12.0, 12.0),
        Restart => {
            arc(&p, 5.0, 5.0, 14.0, 14.0, -60.0, 300.0);
            arrow_head(18.5, 6.5, -0.2, 5.0);
        }
        Cancel => {
            g.draw_line(&p, 6.0, 6.0, 18.0, 18.0);
            g.draw_line(&p, 18.0, 6.0, 6.0, 18.0);
        }
        Mic | MicOff => {
            rc(&p, 9.0, 3.0, 6.0, 11.0);
            arc(&p, 6.0, 6.0, 12.0, 11.0, 0.0, 180.0);
            g.draw_line(&p, 12.0, 17.0, 12.0, 21.0);
            g.draw_line(&p, 8.0, 21.0, 16.0, 21.0);
            if id == MicOff {
                g.draw_line(&p, 4.0, 4.0, 20.0, 20.0);
            }
        }
        Speaker | SpeakerOff => {
            poly(&p, &[(3.0, 9.0), (7.0, 9.0), (12.0, 4.0), (12.0, 20.0), (7.0, 15.0), (3.0, 15.0)]);
            if id == Speaker {
                arc(&p, 11.0, 8.0, 6.0, 8.0, -60.0, 120.0);
                arc(&p, 11.0, 5.0, 10.0, 14.0, -60.0, 120.0);
            } else {
                g.draw_line(&p, 15.0, 9.0, 21.0, 15.0);
                g.draw_line(&p, 21.0, 9.0, 15.0, 15.0);
            }
        }
        Camera => {
            rc(&p, 3.0, 7.0, 18.0, 12.0);
            rc(&p, 8.0, 4.0, 8.0, 3.0);
            g.draw_ellipse(&p, 8.5, 9.5, 7.0, 7.0);
        }
        Select => {
            let s = [(6.0, 3.0), (6.0, 19.0), (10.0, 15.0), (13.0, 21.0), (15.5, 20.0), (12.5, 14.0), (18.0, 14.0)];
            fill_poly(&faint, &s);
            poly(&thin, &s);
        }
        Arrow => {
            g.draw_line(&p, 4.0, 20.0, 17.0, 7.0);
            arrow_head(20.0, 4.0, -0.785, 8.0);
        }
        Line => g.draw_line(&p, 4.0, 20.0, 20.0, 4.0),
        Rect => rc(&p, 4.0, 6.0, 16.0, 12.0),
        Ellipse => g.draw_ellipse(&p, 3.0, 6.0, 18.0, 12.0),
        Pen => {
            let s = [pt(3.0, 17.0), pt(7.0, 12.0), pt(11.0, 16.0), pt(16.0, 8.0), pt(21.0, 12.0)];
            g.draw_curve(&p, &s, 0.5);
        }
        Highlighter => {
            fill_rc(&Brush::solid(0x96FF_D600), 3.0, 15.0, 18.0, 5.0);
            poly(&p, &[(9.0, 14.0), (15.0, 4.0), (19.0, 6.0), (13.0, 16.0)]);
        }
        Text => text("T", "Georgia", 19.0, (0.0, 0.0, 24.0, 25.0), &b),
        Callout => {
            let gp = Path::new();
            gp.arc(3.0, 4.0, 6.0, 6.0, 180.0, 90.0);
            gp.arc(15.0, 4.0, 6.0, 6.0, 270.0, 90.0);
            gp.arc(15.0, 10.0, 6.0, 6.0, 0.0, 90.0);
            gp.line(pt(12.0, 16.0), pt(8.0, 21.0));
            gp.line(pt(8.0, 21.0), pt(8.0, 16.0));
            gp.arc(3.0, 10.0, 6.0, 6.0, 90.0, 90.0);
            gp.close_figure();
            g.draw_path(&p, &gp);
        }
        Step => {
            // "1" and "2" behind, "3" in front
            let white = Brush::solid(WHITE);
            let ring1 = GPen::new(alpha(c, 150), 1.0);
            g.fill_ellipse(&white, 1.5, 1.0, 9.0, 9.0);
            g.draw_ellipse(&ring1, 1.5, 1.0, 9.0, 9.0);
            text("1", "Segoe UI", 7.0, (1.5, 1.4, 9.0, 9.0), &b);
            g.fill_ellipse(&faint, 10.5, 2.0, 11.5, 11.5);
            text("2", "Segoe UI", 8.5, (10.5, 2.5, 11.5, 11.5), &b);
            g.fill_ellipse(&white, 1.0, 8.5, 14.0, 14.0);
            g.fill_ellipse(&b, 2.0, 9.5, 12.0, 12.0);
            text("3", "Segoe UI", 8.5, (2.0, 10.0, 12.0, 12.0), &white);
        }
        Stamp => {
            g.fill_ellipse(&b, 8.6, 1.6, 6.8, 6.8);
            fill_rc(&b, 10.6, 7.0, 2.8, 4.5);
            fill_poly(&b, &[(7.0, 11.0), (17.0, 11.0), (18.5, 14.5), (5.5, 14.5)]);
            fill_rc(&b, 3.5, 14.0, 17.0, 4.2);
            g.draw_line(&GPen::new(c, 1.6), 4.0, 21.0, 20.0, 21.0);
        }
        Blur => {
            for yy in 0..3 {
                for xx in 0..3 {
                    let cb = Brush::solid(alpha(c, if (xx + yy) % 2 == 1 { 90 } else { 220 }));
                    fill_rc(&cb, 4.0 + xx as f32 * 5.5, 4.0 + yy as f32 * 5.5, 5.0, 5.0);
                }
            }
        }
        Spotlight => {
            fill_rc(&faint, 2.0, 3.0, 20.0, 18.0);
            g.fill_ellipse(&Brush::solid(WHITE), 7.0, 7.0, 10.0, 10.0);
            g.draw_ellipse(&p, 7.0, 7.0, 10.0, 10.0);
        }
        Magnify | Ocr => {
            g.draw_ellipse(&p, 3.0, 3.0, 13.0, 13.0);
            g.draw_line(&p3, 14.5, 14.5, 20.5, 20.5);
            if id == Ocr {
                text("Aa", "Segoe UI", 7.0, (3.0, 3.0, 13.0, 13.0), &b);
            }
        }
        Fill => {
            // tilted bucket with its mouth up to the left, upright handle and a falling paint drop
            let light = alpha(c, 150);
            let handle = GPen::new(light, 2.0);
            handle.start_cap(LineCapRound);
            handle.end_cap(LineCapFlat);
            g.draw_line(&handle, 10.0, 1.2, 10.0, 8.6);
            let bucket = [(12.0, 5.0), (13.5, 3.5), (22.5, 12.5), (13.5, 21.5), (4.5, 12.5), (7.0, 10.0), (12.0, 10.0)];
            let edge = GPen::new(c, 1.4);
            edge.line_join(LineJoinRound);
            fill_poly(&b, &bucket);
            poly(&edge, &bucket);
            let drop = Path::new();
            drop.bezier(pt(4.5, 15.0), pt(5.5, 16.7), pt(7.0, 18.3), pt(7.0, 20.4));
            drop.arc(2.0, 17.9, 5.0, 5.0, 0.0, 180.0);
            drop.bezier(pt(2.0, 20.4), pt(2.0, 18.3), pt(3.5, 16.7), pt(4.5, 15.0));
            drop.close_figure();
            g.fill_path(&Brush::solid(light), &drop);
        }
        Eraser => {
            poly(&p, &[(3.0, 15.0), (12.0, 6.0), (19.0, 13.0), (10.0, 22.0)]);
            g.draw_line(&p, 7.5, 10.5, 14.5, 17.5);
            g.draw_line(&p, 10.0, 21.0, 21.0, 21.0);
        }
        Crop => {
            g.draw_line(&p, 6.0, 2.0, 6.0, 18.0);
            g.draw_line(&p, 6.0, 18.0, 22.0, 18.0);
            g.draw_line(&p, 2.0, 6.0, 18.0, 6.0);
            g.draw_line(&p, 18.0, 6.0, 18.0, 22.0);
        }
        Selection => {
            let d = GPen::new(c, 1.6);
            d.dash(DashStyleDash);
            rc(&d, 4.0, 5.0, 16.0, 14.0);
        }
        Wand => {
            g.draw_line(&p26, 4.0, 20.0, 15.0, 9.0);
            for i in 0..4 {
                let a = i as f32 * 1.5708;
                g.draw_line(&thin, 18.0 + 2.0 * a.cos(), 6.0 + 2.0 * a.sin(), 18.0 + 4.5 * a.cos(), 6.0 + 4.5 * a.sin());
            }
        }
        Cutout => {
            // image with a zigzag tear
            let gp = Path::new();
            let d = 5.0;
            gp.arc(2.0, 4.0, d, d, 180.0, 90.0);
            gp.arc(22.0 - d, 4.0, d, d, 270.0, 90.0);
            gp.arc(22.0 - d, 20.0 - d, d, d, 0.0, 90.0);
            gp.arc(2.0, 20.0 - d, d, d, 90.0, 90.0);
            gp.close_figure();
            g.fill_path(&b, &gp);
            let zig = GPen::new(WHITE, 2.2);
            zig.line_join(LineJoinMiter);
            lines(&zig, &[(1.0, 10.5), (5.0, 7.5), (8.5, 11.0), (12.0, 7.5), (15.5, 11.0), (19.0, 7.5), (23.0, 10.5)]);
        }
        Eyedropper => {
            g.draw_line(&p24, 5.0, 19.0, 14.0, 10.0);
            g.fill_ellipse(&b, 13.0, 4.0, 7.0, 7.0);
            g.draw_line(&p, 11.0, 8.0, 16.0, 13.0);
        }
        Undo | Redo => {
            if id == Redo {
                g.translate(24.0, 0.0);
                g.scale(-1.0, 1.0);
            }
            arc(&p, 6.0, 7.0, 14.0, 12.0, 270.0, 180.0);
            g.draw_line(&p, 13.0, 19.0, 9.0, 19.0);
            g.draw_line(&p, 13.0, 7.0, 5.0, 7.0);
            arrow_head(3.5, 7.0, 3.14159, 5.0);
        }
        Editor => {
            rc(&p, 3.0, 4.0, 14.0, 14.0);
            g.draw_line(&p26, 10.0, 18.0, 20.0, 8.0);
        }
        Folder => poly(&p, &[(3.0, 6.0), (9.0, 6.0), (11.0, 8.0), (21.0, 8.0), (21.0, 19.0), (3.0, 19.0)]),
        Gear => {
            g.draw_ellipse(&p, 8.0, 8.0, 8.0, 8.0);
            for i in 0..8 {
                let a = i as f32 * 0.785;
                g.draw_line(&p26, 12.0 + 6.0 * a.cos(), 12.0 + 6.0 * a.sin(), 12.0 + 9.0 * a.cos(), 12.0 + 9.0 * a.sin());
            }
        }
        Copy => {
            rc(&p, 8.0, 8.0, 12.0, 13.0);
            lines(&p, &[(5.0, 16.0), (5.0, 4.0), (15.0, 4.0), (15.0, 6.0)]);
        }
        Save => {
            rc(&p, 4.0, 4.0, 16.0, 16.0);
            rc(&p, 8.0, 4.0, 8.0, 5.0);
            rc(&p, 7.0, 13.0, 10.0, 7.0);
        }
        Share => {
            g.draw_ellipse(&p, 3.0, 9.0, 5.0, 5.0);
            g.draw_ellipse(&p, 16.0, 3.0, 5.0, 5.0);
            g.draw_ellipse(&p, 16.0, 16.0, 5.0, 5.0);
            g.draw_line(&p, 7.5, 10.5, 16.5, 6.5);
            g.draw_line(&p, 7.5, 12.5, 16.5, 17.5);
        }
        Library => {
            rc(&p, 3.0, 4.0, 7.0, 7.0);
            rc(&p, 14.0, 4.0, 7.0, 7.0);
            rc(&p, 3.0, 14.0, 7.0, 7.0);
            rc(&p, 14.0, 14.0, 7.0, 7.0);
        }
        CameraFill => {
            // filled camera (tabs and lists of the capture window)
            let gp = Path::new();
            gp.line(pt(4.5, 6.0), pt(8.0, 6.0));
            gp.line(pt(8.0, 6.0), pt(9.6, 3.6));
            gp.line(pt(9.6, 3.6), pt(14.4, 3.6));
            gp.line(pt(14.4, 3.6), pt(16.0, 6.0));
            gp.line(pt(16.0, 6.0), pt(19.5, 6.0));
            gp.arc(17.0, 6.0, 5.0, 5.0, 270.0, 90.0);
            gp.arc(17.0, 15.0, 5.0, 5.0, 0.0, 90.0);
            gp.arc(2.0, 15.0, 5.0, 5.0, 90.0, 90.0);
            gp.arc(2.0, 6.0, 5.0, 5.0, 180.0, 90.0);
            gp.close_figure();
            gp.ellipse(7.4, 8.4, 9.2, 9.2);
            g.fill_path(&b, &gp);
            g.fill_ellipse(&b, 9.3, 10.3, 5.4, 5.4);
        }
        VideoFill => {
            let gp = Path::new();
            gp.arc(2.0, 6.0, 5.0, 5.0, 180.0, 90.0);
            gp.arc(11.5, 6.0, 5.0, 5.0, 270.0, 90.0);
            gp.arc(11.5, 13.0, 5.0, 5.0, 0.0, 90.0);
            gp.arc(2.0, 13.0, 5.0, 5.0, 90.0, 90.0);
            gp.close_figure();
            g.fill_path(&b, &gp);
            fill_poly(&b, &[(17.5, 10.4), (22.0, 7.4), (22.0, 16.6), (17.5, 13.6)]);
        }
        Mail => {
            rc(&p, 3.0, 6.0, 18.0, 13.0);
            lines(&p, &[(3.5, 6.5), (12.0, 13.0), (20.5, 6.5)]);
        }
        Print => {
            rc(&p, 3.0, 9.0, 18.0, 8.0);
            lines(&p, &[(7.0, 9.0), (7.0, 3.5), (17.0, 3.5), (17.0, 9.0)]);
            rc(&p, 7.0, 14.0, 10.0, 6.5);
        }
        NewDoc => {
            lines(&p, &[(5.0, 3.0), (14.0, 3.0), (19.0, 8.0), (19.0, 21.0), (5.0, 21.0), (5.0, 3.0)]);
            lines(&p, &[(14.0, 3.0), (14.0, 8.0), (19.0, 8.0)]);
            g.draw_line(&p, 12.0, 11.0, 12.0, 18.0);
            g.draw_line(&p, 8.5, 14.5, 15.5, 14.5);
        }
        Trash => {
            g.draw_line(&p, 4.0, 6.5, 20.0, 6.5);
            lines(&p, &[(9.0, 6.0), (9.5, 3.5), (14.5, 3.5), (15.0, 6.0)]);
            lines(&p, &[(6.0, 7.0), (7.0, 21.0), (17.0, 21.0), (18.0, 7.0)]);
            g.draw_line(&thin, 10.0, 10.0, 10.0, 18.0);
            g.draw_line(&thin, 14.0, 10.0, 14.0, 18.0);
        }
        Layers => {
            let top = [(12.0, 3.0), (21.0, 8.0), (12.0, 13.0), (3.0, 8.0)];
            fill_poly(&faint, &top);
            poly(&p, &top);
            lines(&p, &[(3.0, 12.0), (12.0, 17.0), (21.0, 12.0)]);
            lines(&p, &[(3.0, 16.0), (12.0, 21.0), (21.0, 16.0)]);
        }
        Rotate => {
            arc(&p, 4.0, 5.0, 15.0, 15.0, 200.0, 250.0);
            arrow_head(19.0, 9.0, 1.35, 5.5);
            rc(&thin, 9.0, 10.0, 6.0, 6.0);
        }
        Effects => {
            // wand with sparkles
            g.draw_line(&p26, 4.0, 20.0, 14.0, 10.0);
            let spark = |x: f32, y: f32, r: f32| {
                let k = r * 0.28;
                fill_poly(&b, &[(x, y - r), (x + k, y - k), (x + r, y), (x + k, y + k), (x, y + r), (x - k, y + k), (x - r, y),
                                (x - k, y - k)]);
            };
            spark(17.0, 7.0, 4.5);
            spark(9.0, 5.0, 2.6);
            spark(20.0, 15.0, 2.6);
        }
        Star => {
            let s: Vec<(f32, f32)> = (0..10)
                .map(|i| {
                    let a = -1.5708 + i as f32 * 3.14159 / 5.0;
                    let rr = if i % 2 == 1 { 4.4 } else { 10.0 };
                    (12.0 + rr * a.cos(), 12.8 + rr * a.sin())
                })
                .collect();
            fill_poly(&b, &s);
        }
        Clipboard => {
            rc(&p, 5.0, 5.0, 14.0, 16.0);
            fill_rc(&b, 9.0, 3.0, 6.0, 4.0);
            g.draw_line(&thin, 8.0, 11.0, 16.0, 11.0);
            g.draw_line(&thin, 8.0, 15.0, 14.0, 15.0);
        }
        Program => {
            rc(&p, 3.0, 4.0, 18.0, 15.0);
            fill_rc(&b, 3.0, 4.0, 18.0, 4.0);
            g.draw_line(&thin, 7.0, 12.0, 17.0, 12.0);
            g.draw_line(&thin, 7.0, 15.0, 13.0, 15.0);
        }
        Duplicate => {
            rc(&p, 4.0, 4.0, 11.0, 11.0);
            fill_rc(&faint, 9.0, 9.0, 11.0, 11.0);
            rc(&p, 9.0, 9.0, 11.0, 11.0);
        }
        SaveAs => {
            rc(&p, 3.0, 3.0, 14.0, 14.0);
            rc(&p, 6.0, 3.0, 7.0, 4.0);
            g.draw_line(&p26, 13.0, 21.0, 21.0, 13.0);
        }
        NewClipboard => {
            rc(&p, 4.0, 5.0, 12.0, 15.0);
            fill_rc(&b, 7.0, 3.0, 6.0, 4.0);
            g.draw_line(&p, 18.0, 14.0, 18.0, 22.0);
            g.draw_line(&p, 14.0, 18.0, 22.0, 18.0);
        }
        AutoTrim => {
            let d = GPen::new(c, 1.4);
            d.dash(DashStyleDash);
            rc(&d, 3.0, 3.0, 18.0, 18.0);
            fill_rc(&b, 8.0, 8.0, 8.0, 8.0);
        }
        DeleteObject => {
            g.draw_ellipse(&p, 3.0, 6.0, 13.0, 12.0);
            g.fill_ellipse(&b, 13.0, 2.0, 9.0, 9.0);
            let w = GPen::new(WHITE, 1.4);
            g.draw_line(&w, 15.5, 4.5, 19.5, 8.5);
            g.draw_line(&w, 19.5, 4.5, 15.5, 8.5);
        }
        FxBorder => rc(&p3, 4.5, 4.5, 15.0, 15.0),
        FxEdges => fill_poly(&b, &[(4.0, 4.0), (20.0, 4.0), (20.0, 16.0), (17.0, 19.0), (14.0, 16.0), (11.0, 20.0), (8.0, 17.0),
                                    (4.0, 20.0)]),
        FxCaption => {
            rc(&p, 3.0, 4.0, 18.0, 16.0);
            fill_rc(&b, 3.0, 15.0, 18.0, 5.0);
        }
        FxPerspective => fill_poly(&b, &[(4.0, 6.0), (20.0, 3.0), (20.0, 21.0), (4.0, 18.0)]),
        FxPageCurl => {
            fill_poly(&b, &[(4.0, 3.0), (20.0, 3.0), (20.0, 14.0), (13.0, 21.0), (4.0, 21.0)]);
            let f = [(20.0, 14.0), (13.0, 14.0), (13.0, 21.0)];
            fill_poly(&Brush::solid(WHITE), &f);
            poly(&GPen::new(c, 1.2), &f);
        }
        FxShadow => {
            fill_rc(&faint, 8.0, 8.0, 13.0, 13.0);
            fill_rc(&b, 4.0, 4.0, 13.0, 13.0);
        }
        FxFilters => {
            g.fill_ellipse(&Brush::solid(alpha(c, 200)), 3.0, 3.0, 11.0, 11.0);
            g.fill_ellipse(&faint, 10.0, 3.0, 11.0, 11.0);
            g.fill_ellipse(&faint, 6.5, 10.0, 11.0, 11.0);
        }
        FxColorAdjust => {
            for i in 0..4 {
                fill_rc(&Brush::solid(alpha(c, (70 + i * 60) as u8)), 4.0, 4.0 + i as f32 * 4.0, 16.0, 4.0);
            }
        }
        FxColorReplace => {
            g.draw_line(&p24, 5.0, 19.0, 14.0, 10.0);
            g.fill_ellipse(&b, 13.0, 4.0, 7.0, 7.0);
            g.draw_line(&p, 11.0, 8.0, 16.0, 13.0);
            g.fill_ellipse(&faint, 3.0, 15.0, 6.0, 6.0);
        }
        FxSpotlight => {
            g.draw_ellipse(&p, 3.0, 3.0, 13.0, 13.0);
            g.draw_line(&p3, 14.5, 14.5, 20.5, 20.5);
            g.fill_ellipse(&faint, 6.0, 6.0, 7.0, 7.0);
        }
        FxWatermark => {
            let m = [(3.0, 19.0), (9.0, 9.0), (13.0, 15.0), (16.0, 11.0), (21.0, 19.0)];
            fill_poly(&faint, &m);
            poly(&p, &m);
            g.fill_ellipse(&b, 15.0, 4.0, 4.0, 4.0);
        }
        Webcam | WebcamOff => {
            g.draw_ellipse(&p, 5.5, 3.0, 13.0, 13.0);
            g.fill_ellipse(&b, 9.6, 7.1, 4.8, 4.8);
            g.draw_line(&p, 12.0, 16.0, 12.0, 20.5);
            g.draw_line(&p, 7.5, 20.8, 16.5, 20.8);
            if id == WebcamOff {
                g.draw_line(&p, 3.5, 3.5, 20.5, 20.5);
            }
        }
        Lock | Unlock => {
            fill_rc(&b, 5.0, 11.0, 14.0, 10.0);
            if id == Lock {
                arc(&p, 8.0, 4.0, 8.0, 12.0, 180.0, 180.0);
                g.draw_line(&p, 8.0, 10.0, 8.0, 11.5);
                g.draw_line(&p, 16.0, 10.0, 16.0, 11.5);
            } else {
                arc(&p, 12.0, 3.0, 8.0, 11.0, 180.0, 180.0);
                g.draw_line(&p, 20.0, 8.5, 20.0, 10.0);
            }
        }
        Eye | EyeOff => {
            let e = Path::new();
            e.bezier(pt(2.5, 12.0), pt(7.0, 4.5), pt(17.0, 4.5), pt(21.5, 12.0));
            e.bezier(pt(21.5, 12.0), pt(17.0, 19.5), pt(7.0, 19.5), pt(2.5, 12.0));
            g.draw_path(&p, &e);
            g.fill_ellipse(&b, 9.0, 9.0, 6.0, 6.0);
            if id == EyeOff {
                g.draw_line(&p, 4.0, 20.0, 20.0, 4.0);
            }
        }
        Pin => {
            fill_poly(&b, &[(14.0, 2.5), (21.5, 10.0), (18.5, 11.0), (13.5, 16.0), (13.0, 19.0), (5.0, 11.0), (8.0, 10.5),
                            (13.0, 5.5)]);
            g.draw_line(&p, 8.5, 15.5, 3.0, 21.0);
        }
        AlignLeft | AlignCenter | AlignRight => {
            let x = match id { AlignLeft => 4.0, AlignCenter => 12.0, _ => 20.0 };
            g.draw_line(&p, x, 3.0, x, 21.0);
            let (w1, w2) = (13.0, 8.0);
            let at = |w: f32| match id { AlignLeft => x + 2.0, AlignCenter => x - w / 2.0, _ => x - 2.0 - w };
            fill_rc(&b, at(w1), 6.0, w1, 4.5);
            fill_rc(&b, at(w2), 13.5, w2, 4.5);
        }
        AlignTop | AlignMiddle | AlignBottom => {
            let y = match id { AlignTop => 4.0, AlignMiddle => 12.0, _ => 20.0 };
            g.draw_line(&p, 3.0, y, 21.0, y);
            let (h1, h2) = (13.0, 8.0);
            let at = |h: f32| match id { AlignTop => y + 2.0, AlignMiddle => y - h / 2.0, _ => y - 2.0 - h };
            fill_rc(&b, 6.0, at(h1), 4.5, h1);
            fill_rc(&b, 13.5, at(h2), 4.5, h2);
        }
        DistributeH => {
            g.draw_line(&thin, 2.5, 3.0, 2.5, 21.0);
            g.draw_line(&thin, 21.5, 3.0, 21.5, 21.0);
            fill_rc(&b, 5.0, 8.0, 3.5, 8.0);
            fill_rc(&b, 10.25, 5.0, 3.5, 14.0);
            fill_rc(&b, 15.5, 9.0, 3.5, 6.0);
        }
        DistributeV => {
            g.draw_line(&thin, 3.0, 2.5, 21.0, 2.5);
            g.draw_line(&thin, 3.0, 21.5, 21.0, 21.5);
            fill_rc(&b, 8.0, 5.0, 8.0, 3.5);
            fill_rc(&b, 5.0, 10.25, 14.0, 3.5);
            fill_rc(&b, 9.0, 15.5, 6.0, 3.5);
        }
        Ruler => {
            let rp = Path::new();
            rp.rectangle(rect(2.5, 8.0, 19.0, 8.0));
            g.draw_path(&p, &rp);
            for i in 0..5 {
                let x = 5.5 + i as f32 * 3.3;
                g.draw_line(&thin, x, 8.0, x, if i % 2 == 1 { 11.0 } else { 12.5 });
            }
        }
        Link => {
            arc(&p, 3.0, 9.0, 10.0, 6.0, 90.0, 180.0);
            g.draw_line(&p, 8.0, 9.0, 11.0, 9.0);
            g.draw_line(&p, 8.0, 15.0, 11.0, 15.0);
            arc(&p, 11.0, 9.0, 10.0, 6.0, -90.0, 180.0);
            g.draw_line(&p, 13.0, 9.0, 16.0, 9.0);
            g.draw_line(&p, 13.0, 15.0, 16.0, 15.0);
            g.draw_line(&p, 8.5, 12.0, 15.5, 12.0);
        }
        Sliders => {
            for i in 0..3 {
                let y = 6.0 + i as f32 * 6.0;
                let k = match i { 0 => 15.0, 1 => 8.0, _ => 13.0 };
                g.draw_line(&p, 3.0, y, 21.0, y);
                g.fill_ellipse(&b, k - 2.6, y - 2.6, 5.2, 5.2);
            }
        }
        Group => {
            let d = GPen::new(c, 1.2);
            d.dash(DashStyleDash);
            rc(&d, 2.5, 2.5, 19.0, 19.0);
            fill_rc(&b, 6.0, 6.0, 7.0, 7.0);
            g.draw_ellipse(&p, 11.0, 11.0, 7.0, 7.0);
        }
        Check => lines(&p, &[(4.5, 12.5), (9.5, 17.5), (19.5, 6.5)]),
        Qr => {
            for (x, y) in [(3.0, 3.0), (14.0, 3.0), (3.0, 14.0)] {
                rc(&p, x, y, 7.0, 7.0);
                fill_rc(&b, x + 2.5, y + 2.5, 2.0, 2.0);
            }
            fill_rc(&b, 14.0, 14.0, 2.5, 2.5);
            fill_rc(&b, 18.0, 18.0, 2.5, 2.5);
            fill_rc(&b, 18.0, 14.0, 2.5, 2.5);
        }
        Upload => {
            arc(&p, 3.0, 9.0, 8.0, 8.0, 90.0, 180.0);
            arc(&p, 7.0, 5.0, 10.0, 10.0, 180.0, 180.0);
            arc(&p, 13.0, 9.0, 8.0, 8.0, -90.0, 180.0);
            g.draw_line(&p, 7.0, 17.0, 17.0, 17.0);
            g.draw_line(&p, 12.0, 21.0, 12.0, 11.0);
            g.draw_line(&p, 9.0, 14.0, 12.0, 11.0);
            g.draw_line(&p, 15.0, 14.0, 12.0, 11.0);
        }
        Sparkle => {
            fill_poly(&b, &[(10.0, 2.0), (12.0, 8.0), (18.0, 10.0), (12.0, 12.0), (10.0, 18.0), (8.0, 12.0), (2.0, 10.0), (8.0, 8.0)]);
            fill_poly(&b, &[(18.0, 14.0), (19.0, 17.0), (22.0, 18.0), (19.0, 19.0), (18.0, 22.0), (17.0, 19.0), (14.0, 18.0),
                            (17.0, 17.0)]);
        }
        Compare => {
            rc(&p, 3.0, 5.0, 18.0, 14.0);
            g.draw_line(&p, 12.0, 2.0, 12.0, 22.0);
            fill_rc(&faint, 3.0, 5.0, 9.0, 14.0);
        }
        Combine => {
            rc(&p, 3.0, 4.0, 8.0, 16.0);
            rc(&p, 13.0, 4.0, 8.0, 16.0);
            fill_rc(&faint, 13.0, 4.0, 8.0, 16.0);
        }
        Clock => {
            g.draw_ellipse(&p, 3.0, 3.0, 18.0, 18.0);
            g.draw_line(&p, 12.0, 12.0, 12.0, 6.5);
            g.draw_line(&p, 12.0, 12.0, 16.0, 14.0);
        }
        Cursor => {
            let s = [(5.0, 2.0), (5.0, 19.0), (9.5, 14.5), (12.5, 21.0), (15.0, 20.0), (12.0, 13.5), (18.0, 13.5)];
            fill_poly(&faint, &s);
            poly(&p, &s);
        }
        Contrast => {
            g.draw_ellipse(&p, 3.0, 3.0, 18.0, 18.0);
            g.fill_pie(&b, rect(3.0, 3.0, 18.0, 18.0), 90.0, 180.0);
        }
        Split => {
            rc(&p, 5.0, 3.0, 14.0, 18.0);
            let d = GPen::new(c, 1.6);
            d.dash(DashStyleDash);
            g.draw_line(&d, 2.0, 12.0, 22.0, 12.0);
        }
        Focus => {
            fill_rc(&faint, 2.0, 4.0, 20.0, 16.0);
            rc(&p, 2.0, 4.0, 20.0, 16.0);
            g.draw_ellipse(&p, 8.0, 7.0, 10.0, 10.0);
        }
        Beautify => {
            fill_rc(&faint, 2.0, 3.0, 20.0, 18.0);
            rc(&p, 6.0, 7.0, 12.0, 10.0);
        }
    }
    g.restore(st);
}

/// The icon as `px`×`px` pixels (ARGB, transparent around it).
pub fn render(id: Icon, px: i32, c: u32) -> Image {
    let mut img = Image::sized(px.max(1), px.max(1), 0);
    {
        let bmp = Bitmap::wrap(&mut img);
        let g = Graphics::new(&bmp);
        g.text_hint(TextRenderingHintAntiAliasGridFit);
        draw(&g, id, 0.0, 0.0, px as f32, px as f32, c);
        g.flush();
    }
    img
}
