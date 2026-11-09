//! The capture window (comp_1 §2, src/capture/main.cpp): the menus on the title bar; the Image / Video tabs on the left; the
//! selection, effects and share (or quality and webcam) fields; the switches; the round capture button; the bottom
//! bar (⚙ Presets, +, Open Editor) and the preset list below it. Drawing returns what the clicks ask for; the app
//! does it. Each control carries the C++ control's name as its UI Automation id (AutomationId "IDC_SELECTION"…), so
//! the end-to-end tests find them as they found the Win32 controls.

use snapui::icons::{self, Icon};
use snapui::droplist::ListItem;
use snapui::widgets;
use egui::Color32;
use snapcore::fx::CaptureEffects;
use snapcore::i18n;
use snapcore::presets::{CaptureKind, Preset, PresetStore, Selection};

use crate::id;
use crate::tr;

/// The outputs of the "Share" box (one per preset; main.cpp Share).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum Share {
    None = 0,
    File,
    Program,
    Text,
    Pin,
    Qr,
    Word,
    PowerPoint,
    Excel,
    OneNote,
    Upload,
}

impl Share {
    pub const COUNT: i32 = 11;

    pub fn from_i32(i: i32) -> Share {
        use Share::*;
        [None, File, Program, Text, Pin, Qr, Word, PowerPoint, Excel, OneNote, Upload][i.clamp(0, Self::COUNT - 1) as usize]
    }

    pub fn names() -> Vec<String> {
        vec![tr("Nenhum"), tr("Arquivo"), tr("Programa externo"), tr("Texto (OCR)"), tr("Fixar na tela"), tr("Ler código QR"), "Word".into(),
             "PowerPoint".into(), "Excel".into(), "OneNote".into(), tr("Upload (copiar o link)")]
    }

    /// The outputs with settings behind the gear.
    pub fn has_gear(self) -> bool {
        matches!(self, Share::File | Share::Program | Share::Upload | Share::PowerPoint)
    }
}

/// ShareOf
pub fn share_of(p: &Preset) -> Share {
    if p.grab_text {
        Share::Text
    } else if p.read_qr {
        Share::Qr
    } else if p.pin_to_screen {
        Share::Pin
    } else if p.upload {
        Share::Upload
    } else if (0..4).contains(&p.office) {
        Share::from_i32(Share::Word as i32 + p.office)
    } else if p.save_to_file {
        Share::File
    } else if p.open_in_program {
        Share::Program
    } else {
        Share::None
    }
}

/// SetShare: one output at a time.
pub fn set_share(p: &mut Preset, s: Share) {
    p.save_to_file = s == Share::File;
    p.open_in_program = s == Share::Program;
    p.grab_text = s == Share::Text;
    p.pin_to_screen = s == Share::Pin;
    p.read_qr = s == Share::Qr;
    p.upload = s == Share::Upload;
    p.office = if (Share::Word as i32..=Share::OneNote as i32).contains(&(s as i32)) { s as i32 - Share::Word as i32 } else { -1 };
}

/// The effects of the "Effects" box, in the order of the list (main.cpp Fx).
pub const FX_NAMES: [&str; 13] = ["Borda", "Filtros", "Sombra", "Informações da captura", "Substituição de cor", "Redimensionar imagem",
                                  "Bordas especiais", "Cortar bordas", "Ajuste de cor", "Girar imagem", "Marca d'água", "Resolução da imagem",
                                  "Embelezar (fundo, margem e sombra)"];

pub fn fx_on(e: &mut CaptureEffects, k: usize) -> &mut bool {
    match k {
        0 => &mut e.border,
        1 => &mut e.filter,
        2 => &mut e.shadow,
        3 => &mut e.caption,
        4 => &mut e.replace_color,
        5 => &mut e.scale,
        6 => &mut e.edge,
        7 => &mut e.trim,
        8 => &mut e.adjust,
        9 => &mut e.rotate,
        10 => &mut e.watermark,
        11 => &mut e.resolution,
        _ => &mut e.beautify,
    }
}

fn fx_count_on(e: &CaptureEffects) -> usize {
    let mut e = e.clone();
    (0..FX_NAMES.len()).filter(|&k| *fx_on(&mut e, k)).count()
}

fn fx_first_on(e: &CaptureEffects) -> Option<usize> {
    let mut e = e.clone();
    (0..FX_NAMES.len()).find(|&k| *fx_on(&mut e, k))
}

/// What the window shows that is not in the presets.
#[derive(Clone, Debug, Default)]
pub struct View {
    /// the preset list is open below the bottom bar (CapturePresetsOpen)
    pub presets_open: bool,
    /// One Click is on (the View menu's check)
    pub one_click: bool,
    /// the image and video hotkeys, for the capture button's tooltip
    pub image_hotkey: String,
    pub video_hotkey: String,
    /// the webcams, listed when the webcam menu opens (name, link)
    pub cams: Vec<(String, String)>,
    /// the effect the "Effects" box shows (target of "Remove this effect")
    pub fx_current: Option<usize>,
    /// open the list of the effects that are off ("Add another effect")
    pub fx_add_mode: bool,
    /// open the webcam menu (the frame after the click, once the cameras are listed)
    pub webcam_menu: bool,
    /// the window (its title bar opens the menus); 0 in the tests
    pub hwnd: isize,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    /// the presets changed (to be saved)
    Changed,
    /// a command of the menus (main.cpp IDM_*)
    Command(u32),
    Capture,
    /// the settings of effect k (its form; OK turns it on)
    EditFx(usize),
    /// a new output chosen in the "Share" box (Office not installed / upload not configured are said)
    ShareChosen(Share),
    /// the gear of the "Share" box (file, program, upload or PowerPoint settings)
    ShareGear,
    /// Region › Fixed region…
    FixedRegion,
    /// the gear of the "Quality" box (video options)
    VideoOptions,
    /// Webcam › Remove the background…
    ChromaKey,
    /// the webcam menu opens: the cameras are listed (into `View::cams`)
    ListCameras,
    /// the hotkey label of a preset row (its form)
    PresetHotkey(usize),
    /// the preset list opened or closed (the window grows or shrinks)
    PresetsToggled,
}

/// Tags a widget with the C++ control's name: its UI Automation id.
fn tag(ui: &egui::Ui, r: &egui::Response, name: &str) {
    ui.ctx().accesskit_node_builder(r.id, |n| n.set_author_id(name.to_owned()));
}

fn tag_value(ui: &egui::Ui, r: &egui::Response, name: &str, value: &str) {
    ui.ctx().accesskit_node_builder(r.id, |n| {
        n.set_author_id(name.to_owned());
        n.set_value(value.to_owned());
    });
}

/// The window's height: the content and the bottom bar, plus the preset list when it is open.
pub fn height(store: &PresetStore, view: &View) -> f32 {
    let base = 262.0;
    if view.presets_open { base + 8.0 + 30.0 * store.presets.len().clamp(2, 8) as f32 } else { base }
}

pub const WIDTH: f32 = 840.0;

/// BuildMenu: File, Capture, View and Help, on the title bar (snapui's caption.rs). A chosen command arrives as
/// WM_COMMAND, as the C++ menus sent it.
pub fn menus() -> Vec<(String, snapui::menu::Menu)> {
    use snapui::menu::{Item, Menu};
    let c = |id: u32, t: String| Item::Cmd(id, t);
    let file = Menu::popup(vec![
        c(id::OPEN_EDITOR, tr("Abrir o Editor")),
        c(id::LIBRARY_FOLDER, tr("Abrir a pasta da biblioteca")),
        c(id::LOGS, tr("Abrir a pasta de logs")),
        Item::Separator,
        c(id::PRESET_IMPORT, tr("Importar presets…")),
        c(id::PRESET_EXPORT, tr("Exportar presets…")),
        Item::Separator,
        c(id::SETTINGS_EXPORT, tr("Exportar todas as configurações…")),
        c(id::SETTINGS_IMPORT, tr("Importar todas as configurações…")),
        c(id::SETTINGS_BACKUPS, tr("Abrir a pasta de backups")),
        Item::Separator,
        c(id::UPLOAD_SETTINGS, tr("Configurar o upload…")),
        c(id::CLEANUP_NOW, tr("Limpar a biblioteca agora")),
        c(id::OPTIONS, tr("Opções…")),
        Item::Separator,
        c(id::EXIT, tr("Sair do AnyCapture")),
    ]);
    let pin = Menu::popup(vec![
        c(id::CAPTURE_PIN, tr("Capturar e fixar")),
        c(id::PIN_CLIPBOARD, tr("Fixar da área de transferência")),
        c(id::PIN_FILE, tr("Fixar um arquivo…")),
        c(id::PINS_CLOSE, tr("Fechar todas")),
    ]);
    let capture = Menu::popup(vec![
        c(id::CAPTURE_IMAGE, tr("Capturar imagem")),
        c(id::CAPTURE_VIDEO, tr("Gravar vídeo")),
        c(id::REPEAT, tr("Repetir a última captura")),
        Item::Separator,
        c(id::CAPTURE_CLIPBOARD, tr("Da área de transferência")),
        c(id::CAPTURE_FILE, tr("De um arquivo gráfico…")),
        Item::Separator,
        c(id::COLOR_PICKER, tr("Conta-gotas de tela")),
        c(id::QR_REGION, tr("Ler código QR")),
        Item::SubIcon(tr("Fixar na tela"), pin, Icon::Pin),
        c(id::INTERVAL, tr("Captura por intervalo…")),
        c(id::FOCUS, tr("Foco de tela (apresentações)")),
        c(id::RECORD_AUDIO, tr("Gravar só o áudio")),
        c(id::AUDIO_FOLDER, tr("Abrir a pasta dos áudios")),
    ]);
    let view = Menu::popup(vec![
        Item::Checked(id::ONECLICK, tr("Mostrar o One Click"), false),
        Item::Checked(id::SHOW_PRESETS, tr("Lista de presets"), false),
    ]);
    let help = Menu::popup(vec![
        c(id::MANUAL, tr("Manual do AnyCapture")),
        c(id::PRESET_HELP, tr("Como usar os presets")),
        Item::Separator,
        c(id::ABOUT, tr("Sobre o AnyCapture")),
    ]);
    vec![(tr("&Arquivo"), file), (tr("&Captura"), capture), (tr("E&xibir"), view), (tr("A&juda"), help)]
}

/// The icons of the menu commands.
pub fn menu_icon(cmd: u32) -> Option<Icon> {
    Some(match cmd {
        id::OPEN_EDITOR => Icon::Editor,
        id::LIBRARY_FOLDER => Icon::Library,
        id::LOGS | id::SETTINGS_BACKUPS | id::AUDIO_FOLDER => Icon::Folder,
        id::UPLOAD_SETTINGS => Icon::Upload,
        id::CLEANUP_NOW => Icon::Trash,
        id::OPTIONS => Icon::Gear,
        id::CAPTURE_IMAGE => Icon::Capture,
        id::CAPTURE_VIDEO => Icon::VideoFill,
        id::REPEAT => Icon::Restart,
        id::CAPTURE_CLIPBOARD | id::PIN_CLIPBOARD => Icon::Clipboard,
        id::CAPTURE_FILE | id::PIN_FILE => Icon::Image,
        id::COLOR_PICKER => Icon::Eyedropper,
        id::QR_REGION => Icon::Qr,
        id::CAPTURE_PIN => Icon::Pin,
        id::PINS_CLOSE => Icon::Cancel,
        id::INTERVAL => Icon::Clock,
        id::FOCUS => Icon::Focus,
        id::RECORD_AUDIO => Icon::Mic,
        _ => return None,
    })
}

/// The View menu's checks, applied when a menu opens.
fn menu_states(view: &View) {
    snapui::menu::set_states(snapui::menu::MenuStates {
        enabled: Vec::new(),
        checked: vec![(id::ONECLICK, view.one_click), (id::SHOW_PRESETS, view.presets_open)],
    });
}

/// The Image / Video tabs (vertical, as in the Windows 11 navigation: the active one has an accent bar on its left).
fn tabs(ui: &mut egui::Ui, p: &mut Preset, out: &mut Vec<Action>) {
    for (k, name, cid, icon) in [(CaptureKind::Image, tr("Imagem"), "IDC_TAB_IMAGE", Icon::CameraFill),
                                 (CaptureKind::Video, tr("Vídeo"), "IDC_TAB_VIDEO", Icon::VideoFill)] {
        let on = p.kind == k;
        let r = nav_tab(ui, icon, &name, on);
        tag(ui, &r, cid);
        if r.clicked() && !on {
            set_kind(p, k);
            out.push(Action::Changed);
        }
        ui.add_space(2.0);
    }
}

fn nav_tab(ui: &mut egui::Ui, icon: Icon, label: &str, on: bool) -> egui::Response {
    let t = snapui::theme::tokens();
    let (rect, r) = ui.allocate_exact_size(egui::vec2(ui.available_width(), 60.0), egui::Sense::click());
    r.widget_info(|| egui::WidgetInfo::selected(egui::Role::Tab, true, on, label));
    let p = ui.painter();
    if r.hovered() || r.is_pointer_button_down_on() {
        p.rect_filled(rect.shrink2(egui::vec2(4.0, 0.0)), egui::CornerRadius::same(t.radius_lg / 2 + 2),
                      if r.is_pointer_button_down_on() { t.pressed } else { t.hover });
    }
    if on {
        let bar = egui::Rect::from_center_size(egui::pos2(rect.left() + 5.5, rect.center().y), egui::vec2(3.0, 22.0));
        p.rect_filled(bar, egui::CornerRadius::same(2), t.accent);
    }
    let fg = if on { t.text } else { t.text_dim };
    icons::draw(p, icon, egui::Rect::from_center_size(egui::pos2(rect.center().x, rect.top() + 21.0), egui::vec2(22.0, 22.0)), fg);
    // the active label in semibold: drawn twice, a fraction of a pixel apart (egui has no bold face of its own)
    let at = egui::pos2(rect.center().x, rect.bottom() - 13.0);
    for dx in if on { &[0.0, 0.45][..] } else { &[0.0][..] } {
        p.text(at + egui::vec2(*dx, 0.0), egui::Align2::CENTER_CENTER, label, egui::FontId::proportional(12.5), fg);
    }
    r
}

/// The tab changes the preset's kind; a selection the new kind lacks becomes Region.
pub fn set_kind(p: &mut Preset, k: CaptureKind) {
    p.kind = k;
    if !Selection::for_kind(k).contains(&p.selection) {
        p.selection = Selection::Region;
    }
}

/// The region gear: Region (chosen during the capture or fixed) and, for images, each window as an object and the
/// page's links.
fn selection_has_gear(p: &Preset) -> bool {
    p.selection == Selection::Region
        || (p.kind == CaptureKind::Image
            && matches!(p.selection, Selection::Window | Selection::FullScreen | Selection::Monitor | Selection::Object | Selection::Freehand
                                     | Selection::MultiArea))
}

/// Width of the combo boxes (the switch column takes the room left before the capture button).
const BOX_W: f32 = 196.0;

/// What a row of a gear menu does.
#[derive(Clone, Copy)]
enum GearItem {
    RegionAtCapture,
    RegionFixed,
    WindowObjects,
    Links,
}

fn selection_row(ui: &mut egui::Ui, p: &mut Preset, out: &mut Vec<Action>) {
    let label = ui.label(tr("Seleção:"));
    let list = Selection::for_kind(p.kind);
    let shown = if p.selection == Selection::Region && p.region_fixed { tr("Região fixa") } else { p.selection.name() };
    let has_gear = selection_has_gear(p);
    let rows = || list.iter().enumerate().map(|(i, &s)| ListItem::new(s.name()).selected(s == p.selection).aid(format!("IDC_SELECTION.{i}"))).collect();
    let c = widgets::combo(ui, "IDC_SELECTION", &shown, BOX_W, Some(has_gear), rows, false);
    let r = c.response.labelled_by(label.id);
    tag_value(ui, &r, "IDC_SELECTION", &shown);
    if let Some(i) = c.picked
        && let Some(&s) = list.get(i)
        && s != p.selection
    {
        p.selection = s;
        out.push(Action::Changed);
    }
    if let Some(g) = c.gear {
        tag(ui, &g, "IDC_SELECTION.gear");
        if has_gear {
            let g = g.on_hover_text(tr("Configurações"));
            // the menu's rows and what each does
            let mut acts: Vec<Option<GearItem>> = Vec::new();
            let mut items: Vec<ListItem> = Vec::new();
            if p.selection == Selection::Region {
                items.push(ListItem::new(tr("Selecionar a região na captura")).selected(!p.region_fixed));
                acts.push(Some(GearItem::RegionAtCapture));
                items.push(ListItem::new(tr("Região fixa…")).selected(p.region_fixed));
                acts.push(Some(GearItem::RegionFixed));
            }
            if p.kind == CaptureKind::Image {
                if !items.is_empty() {
                    items.push(ListItem::separator());
                    acts.push(None);
                }
                if matches!(p.selection, Selection::FullScreen | Selection::Monitor) {
                    items.push(ListItem::new(tr("Cada janela como um objeto separado")).check(p.window_objects));
                    acts.push(Some(GearItem::WindowObjects));
                }
                items.push(ListItem::new(tr("Guardar os links da página (clicáveis, também no PDF)")).check(p.capture_links));
                acts.push(Some(GearItem::Links));
            }
            match widgets::menu_list(ui, &g, || items, false).and_then(|i| acts.get(i).copied().flatten()) {
                Some(GearItem::RegionAtCapture) => {
                    p.region_fixed = false;
                    out.push(Action::Changed);
                }
                Some(GearItem::RegionFixed) => out.push(Action::FixedRegion),
                Some(GearItem::WindowObjects) => {
                    p.window_objects = !p.window_objects;
                    out.push(Action::Changed);
                }
                Some(GearItem::Links) => {
                    p.capture_links = !p.capture_links;
                    out.push(Action::Changed);
                }
                None => {}
            }
        }
    }
    ui.end_row();
}

/// The "Effects" box: the current effect (+ how many others); the list turns effects on (their form), off, or all off.
fn effects_row(ui: &mut egui::Ui, p: &mut Preset, view: &mut View, out: &mut Vec<Action>) {
    let label = ui.label(tr("Efeitos:"));
    let e = &mut p.effects;
    if view.fx_current.is_none_or(|k| !*fx_on(e, k)) {
        view.fx_current = fx_first_on(e);
    }
    let shown = match view.fx_current {
        None => tr("Nenhum"),
        Some(k) => {
            let others = fx_count_on(e) - 1;
            let t = tr(FX_NAMES[k]);
            if others > 0 { format!("{t}  +{others}") } else { t }
        }
    };
    // the list: None, the effects (checked when on), then "Add another effect" and "Remove this effect"
    let n = FX_NAMES.len();
    let rows = || {
        let mut v = vec![ListItem::new(tr("Nenhum"))];
        for k in 0..n {
            v.push(ListItem::new(tr(FX_NAMES[k])).check(*fx_on(e, k)).aid(format!("IDC_EFFECTS.{k}")));
        }
        v.push(ListItem::separator());
        v.push(ListItem::new(tr("Adicionar outro efeito")));
        v.push(ListItem::new(tr("Remover este efeito")).enabled(view.fx_current.is_some()));
        v
    };
    let c = widgets::combo(ui, "IDC_EFFECTS", &shown, BOX_W, None, rows, false);
    let r = c.response.labelled_by(label.id);
    tag_value(ui, &r, "IDC_EFFECTS", &shown);
    match c.picked {
        Some(0) => {
            for k in 0..n {
                *fx_on(e, k) = false;
            }
            view.fx_current = None;
            out.push(Action::Changed);
        }
        Some(i) if i <= n => out.push(Action::EditFx(i - 1)),
        Some(i) if i == n + 2 => view.fx_add_mode = true,
        Some(i) if i == n + 3 => {
            if let Some(k) = view.fx_current.take() {
                *fx_on(e, k) = false;
                out.push(Action::Changed);
            }
        }
        _ => {}
    }
    // "Add another effect": a second list under the box, with only the effects that are off
    let off: Vec<usize> = (0..n).filter(|&k| !*fx_on(e, k)).collect();
    let add_id = r.id.with("add");
    if std::mem::take(&mut view.fx_add_mode) {
        let items = off.iter().map(|&k| ListItem::new(tr(FX_NAMES[k]))).collect();
        snapui::droplist::open(ui.ctx(), snapui::droplist::Request { popup_id: add_id, anchor: r.rect, min_width: BOX_W, items,
                                                                      align_end: false });
    }
    if let Some(i) = snapui::droplist::take_pick(ui.ctx(), add_id)
        && let Some(&k) = off.get(i)
    {
        out.push(Action::EditFx(k));
    }
    ui.end_row();
}

fn share_row(ui: &mut egui::Ui, p: &mut Preset, out: &mut Vec<Action>) {
    let label = ui.label(tr("Compartilhar:"));
    let cur = share_of(p);
    let names = Share::names();
    let rows = || names.iter().enumerate().map(|(i, n)| ListItem::new(n.clone()).selected(i == cur as usize).aid(format!("IDC_SHARE.{i}"))).collect();
    let c = widgets::combo(ui, "IDC_SHARE", &names[cur as usize], BOX_W, Some(cur.has_gear()), rows, false);
    let r = c.response.labelled_by(label.id);
    tag_value(ui, &r, "IDC_SHARE", &names[cur as usize]);
    if let Some(i) = c.picked {
        let s = Share::from_i32(i as i32);
        set_share(p, s);
        out.push(Action::Changed);
        out.push(Action::ShareChosen(s));
    }
    if let Some(g) = &c.gear {
        tag(ui, g, "IDC_SHARE.gear");
        if share_of(p).has_gear() && g.clicked() {
            out.push(Action::ShareGear);
        }
    }
    ui.end_row();
}

fn quality_row(ui: &mut egui::Ui, p: &mut Preset, out: &mut Vec<Action>) {
    let label = ui.label(tr("Qualidade:"));
    let names = [tr("Muito baixa"), tr("Baixa"), tr("Média"), tr("Alta")];
    let q = p.quality.clamp(0, 3) as usize;
    let rows = || names.iter().enumerate().map(|(i, n)| ListItem::new(n.clone()).selected(i == q).aid(format!("IDC_QUALITY.{i}"))).collect();
    let c = widgets::combo(ui, "IDC_QUALITY", &names[q], BOX_W, Some(true), rows, false);
    let r = c.response.labelled_by(label.id);
    tag_value(ui, &r, "IDC_QUALITY", &names[q]);
    if let Some(i) = c.picked
        && i != q
    {
        p.quality = i as i32;
        out.push(Action::Changed);
    }
    if let Some(g) = &c.gear {
        tag(ui, g, "IDC_QUALITY.gear");
        if g.clicked() {
            out.push(Action::VideoOptions);
        }
    }
    ui.end_row();
}

/// What a row of the webcam menu does.
#[derive(Clone, Copy)]
enum CamItem {
    Off,
    Camera(usize),
    Shape(i32),
    ChromaKey,
}

fn webcam_row(ui: &mut egui::Ui, p: &mut Preset, view: &mut View, out: &mut Vec<Action>) {
    ui.label(tr("Webcam:"));
    let text = if p.webcam { tr("Ligada") } else { tr("Desligada") };
    let r = widgets::link_row(ui, "", &text, BOX_W);
    tag(ui, &r, "IDC_WEBCAM");
    let menu_id = r.id.with("menu");
    if r.clicked() && !snapui::droplist::just_closed(ui.ctx(), menu_id) {
        // the cameras are listed after this frame; the menu opens on the next one
        out.push(Action::ListCameras);
        view.webcam_menu = true;
        ui.ctx().request_repaint();
    }
    let mut acts: Vec<Option<CamItem>> = vec![Some(CamItem::Off), None];
    let mut items = vec![ListItem::new(tr("Desligada")).selected(!p.webcam), ListItem::separator()];
    if view.cams.is_empty() {
        items.push(ListItem::new(tr("Nenhuma webcam encontrada")).enabled(false));
        acts.push(None);
    }
    for (i, (name, link)) in view.cams.iter().enumerate() {
        let on = p.webcam && (p.webcam_device == *link || (p.webcam_device.is_empty() && i == 0));
        items.push(ListItem::new(name.clone()).selected(on));
        acts.push(Some(CamItem::Camera(i)));
    }
    items.push(ListItem::separator());
    acts.push(None);
    for (s, n) in snapcore::webcam::SHAPE_NAMES.iter().enumerate() {
        items.push(ListItem::new(format!("{}: {}", tr("Formato"), tr(n))).selected(p.webcam_shape == s as i32).enabled(!p.chroma_key));
        acts.push(Some(CamItem::Shape(s as i32)));
    }
    items.push(ListItem::new(tr("Remover o fundo (chroma key)…")).check(p.chroma_key));
    acts.push(Some(CamItem::ChromaKey));
    if std::mem::take(&mut view.webcam_menu) && !r.clicked() {
        snapui::droplist::open(ui.ctx(), snapui::droplist::Request { popup_id: menu_id, anchor: r.rect, min_width: 0.0, items,
                                                                      align_end: true });
    }
    match snapui::droplist::take_pick(ui.ctx(), menu_id).and_then(|i| acts.get(i).copied().flatten()) {
        Some(CamItem::Off) => {
            p.webcam = false;
            out.push(Action::Changed);
        }
        Some(CamItem::Camera(i)) => {
            if let Some((name, link)) = view.cams.get(i) {
                (p.webcam, p.webcam_device, p.webcam_name) = (true, link.clone(), name.clone());
                out.push(Action::Changed);
            }
        }
        Some(CamItem::Shape(s)) => {
            p.webcam_shape = s;
            out.push(Action::Changed);
        }
        Some(CamItem::ChromaKey) => out.push(Action::ChromaKey),
        None => {}
    }
    ui.end_row();
}

fn switch(ui: &mut egui::Ui, v: &mut bool, text: &str, name: &str) -> bool {
    let r = widgets::toggle(ui, v, &tr(text), ui.available_width());
    tag(ui, &r, name);
    r.changed()
}

/// "%d s" or "Off"
pub fn delay_text(s: i32) -> String {
    if s <= 0 { tr("Desligado") } else { tr("%d s").replace("%d", &s.to_string()) }
}

fn delay_row(ui: &mut egui::Ui, p: &mut Preset, out: &mut Vec<Action>) {
    let r = widgets::link_row(ui, &tr("Atraso"), &delay_text(p.delay_seconds), ui.available_width());
    tag_value(ui, &r, "IDC_DELAY", &delay_text(p.delay_seconds));
    const DELAYS: [i32; 9] = [0, 1, 2, 3, 5, 10, 15, 30, 60];
    let rows = || {
        DELAYS.iter()
            .map(|&s| {
                let t = match s {
                    0 => tr("Desligado"),
                    1 => tr("1 segundo"),
                    _ => tr("%d segundos").replace("%d", &s.to_string()),
                };
                ListItem::new(t).selected(p.delay_seconds == s).aid(format!("IDC_DELAY.{s}"))
            })
            .collect()
    };
    if let Some(i) = widgets::menu_list(ui, &r, rows, true) {
        p.delay_seconds = DELAYS[i];
        out.push(Action::Changed);
    }
}

/// The round capture button (CAPTURE_RED, as the Editor's Capture); "Record" for video. The tooltip says the hotkey.
fn capture_button(ui: &mut egui::Ui, p: &Preset, view: &View, out: &mut Vec<Action>) {
    let video = p.kind == CaptureKind::Video;
    let text = if video { tr("Gravar") } else { tr("Capturar") };
    let d = 88.0;
    let (rect, r) = ui.allocate_exact_size(egui::vec2(d + 8.0, d + 8.0), egui::Sense::click());
    r.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, &text));
    let c = rect.center();
    let p_ = ui.painter();
    let base = Color32::from_rgb(214, 40, 60);
    let fill = if r.is_pointer_button_down_on() {
        base.lerp_to_gamma(Color32::BLACK, 0.16)
    } else if r.hovered() {
        base.lerp_to_gamma(Color32::WHITE, 0.10)
    } else {
        base
    };
    for i in (1..=3).rev() {
        let k = i as f32;
        p_.circle_filled(c + egui::vec2(0.0, 1.5 + k * 0.4), d / 2.0 + k - 1.0, Color32::from_black_alpha((16 * (4 - i)) as u8));
    }
    p_.circle_filled(c, d / 2.0, fill);
    if r.has_focus() {
        p_.circle_stroke(c, d / 2.0 + 3.0, egui::Stroke::new(1.5, snapui::theme::tokens().text));
    }
    p_.text(c, egui::Align2::CENTER_CENTER, &text, egui::FontId::proportional(15.0), Color32::WHITE);
    p_.text(c + egui::vec2(0.4, 0.0), egui::Align2::CENTER_CENTER, &text, egui::FontId::proportional(15.0), Color32::WHITE);
    let tip = if video { tr("Gravar vídeo  (") + &view.video_hotkey + ")" } else { tr("Capturar imagem  (") + &view.image_hotkey + ")" };
    let r = r.on_hover_text(tip);
    tag(ui, &r, "IDC_CAPTURE");
    if r.clicked() {
        out.push(Action::Capture);
    }
}

/// HotkeyLabel: the preset's hotkey, or "Add hotkey".
fn hotkey_label(p: &Preset, text: &dyn Fn(u32, u32) -> String) -> String {
    if p.hotkey_vk != 0 { text(p.hotkey_mods, p.hotkey_vk) } else { tr("Adicionar atalho") }
}

/// The preset list: the kind's icon, the name, the hotkey (or "Add hotkey") and the gear; a click selects, a double
/// click captures.
fn preset_list(ui: &mut egui::Ui, store: &mut PresetStore, hk_text: &dyn Fn(u32, u32) -> String, out: &mut Vec<Action>) {
    let t = snapui::theme::tokens();
    egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
        ui.spacing_mut().item_spacing.y = 0.0;
        for i in 0..store.presets.len() {
            let sel = i as i32 == store.selected;
            let p = &store.presets[i];
            let icon = if p.kind == CaptureKind::Video { Icon::VideoFill } else { Icon::CameraFill };
            let name = i18n::tr_str(&p.name);
            let hk = hotkey_label(p, hk_text);
            let has_hk = p.hotkey_vk != 0;
            // one row: the kind's icon and the name on the left, the hotkey and the gear on the right
            let (rect, _) = ui.allocate_exact_size(egui::vec2(ui.available_width(), 30.0), egui::Sense::hover());
            let gear_rect = egui::Rect::from_min_max(egui::pos2(rect.right() - 34.0, rect.top()), rect.max);
            let hk_rect = egui::Rect::from_min_max(egui::pos2(gear_rect.left() - 140.0, rect.top()), egui::pos2(gear_rect.left(), rect.bottom()));
            let row_rect = egui::Rect::from_min_max(rect.min, egui::pos2(hk_rect.left(), rect.bottom()));
            let id = ui.scope_id().with(("preset", i));
            let row = ui.interact(row_rect, id, egui::Sense::click());
            row.widget_info(|| egui::WidgetInfo::selected(egui::Role::ListBoxOption, true, sel, &name));
            let h = ui.interact(hk_rect, id.with("hotkey"), egui::Sense::click());
            h.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, &hk));
            let g = ui.interact(gear_rect, id.with("gear"), egui::Sense::click());
            g.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, tr("Configurações")));
            let painter = ui.painter();
            let any_hover = row.hovered() || h.hovered() || g.hovered();
            let fill_rect = rect.shrink2(egui::vec2(6.0, 1.0));
            if sel || any_hover {
                painter.rect_filled(fill_rect, egui::CornerRadius::same(t.radius), if any_hover && !sel { t.hover } else { t.pressed });
            }
            if sel {
                let bar = egui::Rect::from_center_size(egui::pos2(fill_rect.left() + 1.5, rect.center().y), egui::vec2(3.0, 16.0));
                painter.rect_filled(bar, egui::CornerRadius::same(2), t.accent);
            }
            let x = fill_rect.left() + 12.0;
            icons::draw(painter, icon, egui::Rect::from_min_size(egui::pos2(x, rect.center().y - 8.0), egui::vec2(16.0, 16.0)), t.text_dim);
            let font = egui::FontId::proportional(t.body);
            painter.with_clip_rect(row_rect).text(egui::pos2(x + 26.0, rect.center().y), egui::Align2::LEFT_CENTER, &name, font.clone(), t.text);
            let hk_color = if h.hovered() { t.accent } else if has_hk { t.text_dim } else { t.text_off };
            painter.text(egui::pos2(hk_rect.right() - 6.0, rect.center().y), egui::Align2::RIGHT_CENTER, &hk, egui::FontId::proportional(t.body - 1.0), hk_color);
            if g.hovered() {
                painter.rect_filled(gear_rect.shrink2(egui::vec2(4.0, 3.0)), egui::CornerRadius::same(t.radius), t.pressed);
            }
            icons::draw(painter, Icon::Gear, egui::Rect::from_center_size(gear_rect.center() - egui::vec2(3.0, 0.0), egui::vec2(16.0, 16.0)), t.text_dim);
            tag(ui, &row, &format!("IDC_PRESETS.{i}"));
            tag(ui, &h, &format!("IDC_PRESETS.{i}.hotkey"));
            tag(ui, &g, &format!("IDC_PRESETS.{i}.gear"));
            if row.clicked() && !sel {
                store.selected = i as i32;
                out.push(Action::Changed);
            }
            if row.double_clicked() {
                store.selected = i as i32;
                out.push(Action::Capture);
            }
            if h.clicked() {
                if !sel {
                    store.selected = i as i32;
                    out.push(Action::Changed);
                }
                out.push(Action::PresetHotkey(i));
            }
            if g.clicked() && !sel {
                store.selected = i as i32;
                out.push(Action::Changed);
            }
            let rows = || {
                vec![
                    ListItem::new(tr("Capturar com este preset")),
                    ListItem::separator(),
                    ListItem::new(tr("Renomear…")),
                    ListItem::new(tr("Atalho…")),
                    ListItem::new(tr("Duplicar")),
                    ListItem::separator(),
                    ListItem::new(tr("Excluir")),
                ]
            };
            const CMDS: [u32; 7] = [id::PRESET_RUN, 0, id::PRESET_RENAME, id::PRESET_HOTKEY, id::PRESET_DUP, 0, id::PRESET_DEL];
            if let Some(k) = widgets::menu_list(ui, &g, rows, true)
                && CMDS[k] != 0
            {
                out.push(Action::Command(CMDS[k]));
            }
        }
    });
}

/// Draws the window; `hk_text` writes a hotkey ("Ctrl+Alt+F12").
pub fn draw(store: &mut PresetStore, view: &mut View, hk_text: &dyn Fn(u32, u32) -> String, ui: &mut egui::Ui) -> Vec<Action> {
    let mut out = Vec::new();
    snapui::caption::bar(ui, view.hwnd, "", snapui::caption::Mark::Capture);
    menu_states(view);
    if store.presets.is_empty() {
        return out;
    }
    store.selected = store.selected.clamp(0, store.presets.len() as i32 - 1);
    if view.presets_open {
        let h = 8.0 + 30.0 * store.presets.len().clamp(2, 8) as f32;
        egui::Panel::bottom("IDC_PRESETS").exact_size(h).resizable(false).show(ui, |ui| preset_list(ui, store, hk_text, &mut out));
    }
    egui::Panel::bottom("bar").exact_size(40.0).resizable(false).show(ui, |ui| {
        ui.horizontal_centered(|ui| {
            let r = widgets::flat_button(ui, Some(Icon::Gear), &tr("Presets"), true, false);
            tag(ui, &r, "IDC_PRESETS_LABEL");
            if r.clicked() {
                out.push(Action::Command(id::SHOW_PRESETS));
                out.push(Action::PresetsToggled);
            }
            let add = widgets::flat_button(ui, None, "+", true, true).on_hover_text(tr("Novo preset"));
            tag(ui, &add, "IDC_PRESET_ADD");
            let rows = || {
                vec![
                    ListItem::new(tr("Novo preset com as configurações atuais")),
                    ListItem::new(tr("Novo preset…")),
                    ListItem::separator(),
                    ListItem::new(tr("Ajuda")),
                ]
            };
            const ADD: [u32; 4] = [id::PRESET_FROM_CURRENT, id::PRESET_NEW, 0, id::PRESET_HELP];
            if let Some(k) = widgets::menu_list(ui, &add, rows, false)
                && ADD[k] != 0
            {
                out.push(Action::Command(ADD[k]));
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let r = widgets::flat_button(ui, Some(Icon::Editor), &tr("Abrir Editor"), false, false);
                tag(ui, &r, "IDC_OPEN_EDITOR");
                if r.clicked() {
                    out.push(Action::Command(id::OPEN_EDITOR));
                }
            });
        });
    });
    egui::Panel::left("tabs").exact_size(76.0).resizable(false).show(ui, |ui| {
        ui.add_space(8.0);
        let i = store.selected as usize;
        tabs(ui, &mut store.presets[i], &mut out);
    });
    egui::CentralPanel::default().show(ui, |ui| {
        let i = store.selected as usize;
        let p = &mut store.presets[i];
        let video = p.kind == CaptureKind::Video;
        ui.add_space(10.0);
        ui.horizontal_top(|ui| {
            egui::Grid::new("fields").num_columns(2).spacing([12.0, 10.0]).min_row_height(widgets::ROW_H).show(ui, |ui| {
                selection_row(ui, p, &mut out);
                if video {
                    quality_row(ui, p, &mut out);
                    webcam_row(ui, p, view, &mut out);
                } else {
                    effects_row(ui, p, view, &mut out);
                    share_row(ui, p, &mut out);
                }
            });
            ui.add_space(16.0);
            ui.vertical(|ui| {
                ui.set_width((ui.available_width() - 102.0).max(180.0));
                ui.spacing_mut().item_spacing.y = 6.0;
                ui.add_space(2.0);
                let mut changed = switch(ui, &mut p.preview_in_editor, "Visualizar no Editor", "IDC_PREVIEW");
                if video {
                    changed |= switch(ui, &mut p.capture_cursor, "Capturar o cursor", "IDC_CURSOR");
                    changed |= switch(ui, &mut p.microphone, "Gravar microfone", "IDC_MIC");
                    changed |= switch(ui, &mut p.system_audio, "Gravar áudio do sistema", "IDC_SYS");
                } else {
                    changed |= switch(ui, &mut p.copy_to_clipboard, "Copiar para a área de transferência", "IDC_CLIPBOARD");
                    changed |= switch(ui, &mut p.capture_cursor, "Capturar o cursor", "IDC_CURSOR");
                    delay_row(ui, p, &mut out);
                }
                if changed {
                    out.push(Action::Changed);
                }
            });
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Min), |ui| {
                ui.add_space(2.0);
                capture_button(ui, p, view, &mut out);
            });
        });
    });
    out
}
