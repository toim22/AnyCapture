//! AnyCapture's user interface in egui (immediate mode: the UI is a function called every frame, which reads the state
//! and draws the widgets, instead of a tree of window objects kept in sync).
//!
//! - `host`: the windows (winit), drawn by egui on the GPU (wgpu), with their Win32 classes and messages
//! - the fonts and the text check of the scripts AnyCapture ships

use std::sync::{Arc, OnceLock};

use egui::{FontData, FontDefinitions, FontFamily};

pub mod droplist;
pub mod form;
pub mod host;
pub mod icons;
pub mod menu;
pub mod caption;
pub mod qr;
pub mod theme;
pub mod widgets;
pub mod upload_ui;

static FIXED_LOOK: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// For the snapshot tests: the same look on every machine (the light theme, Windows' default blue accent). Call it
/// before the first frame.
pub fn fixed_look_for_tests() {
    FIXED_LOOK.store(true, std::sync::atomic::Ordering::Relaxed);
}

pub fn fixed_look() -> bool {
    FIXED_LOOK.load(std::sync::atomic::Ordering::Relaxed)
}

/// The interface font: Inter (SIL Open Font License, rust/resources/fonts), drawn for screens and the same on every
/// machine. Windows' own fonts follow it for the letters Inter lacks, so every language AnyCapture ships has its
/// letters: Segoe UI (Arabic, Hebrew), Nirmala UI (Hindi, Bengali and the other Indic scripts), Segoe UI Symbol and
/// Segoe UI Emoji.
pub const INTER: &[u8] = include_bytes!("../../resources/fonts/Inter-Regular.ttf");
pub const INTER_MEDIUM: &[u8] = include_bytes!("../../resources/fonts/Inter-Medium.ttf");
pub const INTER_SEMIBOLD: &[u8] = include_bytes!("../../resources/fonts/Inter-SemiBold.ttf");

/// The egui family of the semibold face (headings, the wordmark).
pub fn semibold() -> FontFamily {
    FontFamily::Name("semibold".into())
}

pub fn install_fonts(ctx: &egui::Context) {
    static DEFS: OnceLock<FontDefinitions> = OnceLock::new();
    ctx.set_fonts(DEFS.get_or_init(load_fonts).clone());
}

/// The font files are read once per process; every window's Context shares them.
fn load_fonts() -> FontDefinitions {
    let mut defs = FontDefinitions::default();
    for (name, bytes) in [("Inter", INTER), ("Inter Medium", INTER_MEDIUM), ("Inter SemiBold", INTER_SEMIBOLD)] {
        defs.font_data.insert(name.to_owned(), Arc::new(FontData::from_static(bytes)));
    }
    let dir = std::env::var("WINDIR").unwrap_or_else(|_| r"C:\Windows".into()) + r"\Fonts\";
    let mut fallbacks = Vec::new();
    let mut mono = Vec::new();
    for (name, file) in [
        ("Segoe UI", "segoeui.ttf"),
        ("Nirmala UI", "Nirmala.ttc"),
        ("Segoe UI Symbol", "seguisym.ttf"),
        ("Segoe UI Emoji", "seguiemj.ttf"),
        ("Consolas", "consola.ttf"),
    ] {
        let Ok(bytes) = std::fs::read(dir.clone() + file) else { continue };
        defs.font_data.insert(name.to_owned(), Arc::new(FontData::from_owned(bytes)));
        if name == "Consolas" { mono.push(name.to_owned()) } else { fallbacks.push(name.to_owned()) }
    }
    let body = std::iter::once(BODY_FACE.to_owned()).chain(fallbacks.iter().cloned());
    let prop = defs.families.entry(FontFamily::Proportional).or_default();
    prop.splice(0..0, body.clone());
    let monospace = defs.families.entry(FontFamily::Monospace).or_default();
    monospace.splice(0..0, mono.into_iter().chain(body));
    defs.families.insert(semibold(), std::iter::once("Inter SemiBold".to_owned()).chain(fallbacks).collect());
    defs
}

/// The face of the body text: Inter Medium reads better than Regular on screens without hinting (egui draws text
/// with grayscale antialiasing, like most GPU interfaces).
const BODY_FACE: &str = "Inter Medium";

/// The same font for the parts Windows draws (the owner-drawn menus): registered for this process only.
pub fn install_gdi_fonts() {
    static DONE: OnceLock<()> = OnceLock::new();
    DONE.get_or_init(|| {
        for bytes in [INTER, INTER_MEDIUM, INTER_SEMIBOLD] {
            let mut n = 0u32;
            unsafe {
                windows::Win32::Graphics::Gdi::AddFontMemResourceEx(bytes.as_ptr() as _, bytes.len() as u32, None, &mut n);
            }
        }
    });
}

/// The GDI face name of the menus' font.
pub fn gdi_face() -> &'static str {
    "Inter"
}

/// Sample lines in the scripts that need shaping: Arabic and Urdu (right to left, joined letters) and Hindi and
/// Bengali (conjuncts and vowel signs).
pub const SCRIPT_SAMPLES: [(&str, &str); 5] = [
    ("pt", "Capturar a tela e salvar a imagem"),
    ("ar", "التقاط الشاشة وحفظ الصورة"),
    ("ur", "اسکرین کیپچر کریں"),
    ("hi", "स्क्रीन कैप्चर करें"),
    ("bn", "স্ক্রিন ক্যাপচার করুন"),
];

pub fn script_probe(ui: &mut egui::Ui) {
    for (lang, text) in SCRIPT_SAMPLES {
        ui.horizontal(|ui| {
            ui.label(egui::RichText::new(lang).monospace());
            ui.label(egui::RichText::new(text).size(44.0));
        });
    }
}
