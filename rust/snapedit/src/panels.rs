//! The Editor's bars and side panels (src/editor/Panels.cpp, LayersPanel.cpp and the toolbar of main.cpp): the toolbar
//! with its automatic or customized items, the layers list and the UI Automation names the e2e suite finds them by.

use egui::{Color32, RichText};
use snapcore::doc::{DrawObj, ObjType};

use crate::canvas::{Canvas, Tool};
use crate::ids::*;
use crate::menu::TOOLS;
use crate::tr;
use snapui::icons::{self, Icon};

/// Tags a widget with a UI Automation id (AccessKit author id).
pub fn tag(ui: &egui::Ui, r: &egui::Response, name: &str) {
    ui.ctx().accesskit_node_builder(r.id, |n| n.set_author_id(name.to_owned()));
}

// ---------------------------------------------------------------- toolbar
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EntryKind {
    Tool,
    Effect,
    Output,
    Command,
    Spacer,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Entry {
    pub kind: EntryKind,
    pub id: u32,
}

const KINDS: &str = "teocs";

/// Registry text: "t:5,e:1039,o:6,c:1015,s:0" (type:id).
pub fn entries_to_string(v: &[Entry]) -> String {
    v.iter()
        .map(|e| format!("{}:{}", KINDS.as_bytes()[e.kind as usize] as char, e.id))
        .collect::<Vec<_>>()
        .join(",")
}

pub fn entries_from_string(s: &str) -> Vec<Entry> {
    let kinds = [EntryKind::Tool, EntryKind::Effect, EntryKind::Output, EntryKind::Command, EntryKind::Spacer];
    s.split(',')
        .filter_map(|it| {
            let (k, id) = it.split_once(':')?;
            let k = KINDS.find(k.trim())?;
            Some(Entry { kind: kinds[k], id: id.trim().parse().ok()? })
        })
        .collect()
}

pub struct CmdInfo {
    pub id: u32,
    pub label: &'static str,
    pub hint: &'static str,
}

pub const OUTPUTS: [CmdInfo; 5] = [
    CmdInfo { id: T_SAVE, label: "Arquivo", hint: "Salvar como arquivo (PNG, JPG, PDF…)" },
    CmdInfo { id: M_EMAIL, label: "E-mail", hint: "Enviar por e-mail" },
    CmdInfo { id: M_OPEN_PROGRAM, label: "Programa", hint: "Abrir em um programa externo" },
    CmdInfo { id: T_COPYALL, label: "Área de transferência", hint: "Copiar a imagem inteira" },
    CmdInfo { id: M_PRINT, label: "Impressora", hint: "Imprimir a imagem" },
];

pub const COMMANDS: [CmdInfo; 23] = [
    CmdInfo { id: M_UNDO, label: "Desfazer", hint: "Desfazer (Ctrl+Z)" },
    CmdInfo { id: M_REDO, label: "Refazer", hint: "Refazer (Ctrl+Y)" },
    CmdInfo { id: M_NEW_IMAGE, label: "Nova imagem", hint: "Nova imagem em branco" },
    CmdInfo { id: M_NEW_CLIPBOARD, label: "Da área de transf.", hint: "Nova imagem da área de transferência" },
    CmdInfo { id: M_OPEN, label: "Abrir", hint: "Abrir arquivo (Ctrl+O)" },
    CmdInfo { id: M_SAVE, label: "Salvar", hint: "Salvar na biblioteca (Ctrl+S)" },
    CmdInfo { id: M_SAVEAS, label: "Salvar como", hint: "Salvar como arquivo (Ctrl+Shift+S)" },
    CmdInfo { id: M_DUPLICATE, label: "Duplicar", hint: "Duplicar a imagem" },
    CmdInfo { id: M_DELETE_SEL, label: "Excluir objeto", hint: "Excluir o objeto selecionado (Del)" },
    CmdInfo { id: M_OCR, label: "Grab Text", hint: "Reconhecer o texto da imagem (Ctrl+Shift+T)" },
    CmdInfo { id: M_DELETE, label: "Excluir imagem", hint: "Enviar a imagem para a Lixeira" },
    CmdInfo { id: M_FLATTEN, label: "Mesclar tudo", hint: "Mesclar todos os objetos na imagem" },
    CmdInfo { id: M_ROT_CW, label: "Girar", hint: "Girar 90° no sentido horário (Ctrl+R)" },
    CmdInfo { id: M_TRIM, label: "Corte automático", hint: "Cortar as bordas de cor uniforme" },
    CmdInfo { id: M_ALIGN_LEFT, label: "Alinhar à esquerda", hint: "Alinhar os objetos à esquerda" },
    CmdInfo { id: M_ALIGN_CENTER, label: "Alinhar ao centro", hint: "Alinhar os objetos ao centro" },
    CmdInfo { id: M_ALIGN_RIGHT, label: "Alinhar à direita", hint: "Alinhar os objetos à direita" },
    CmdInfo { id: M_ALIGN_TOP, label: "Alinhar em cima", hint: "Alinhar os objetos em cima" },
    CmdInfo { id: M_ALIGN_MIDDLE, label: "Alinhar no meio", hint: "Alinhar os objetos no meio" },
    CmdInfo { id: M_ALIGN_BOTTOM, label: "Alinhar embaixo", hint: "Alinhar os objetos embaixo" },
    CmdInfo { id: M_DIST_H, label: "Distribuir na horizontal", hint: "Espaçar 3 ou mais objetos igualmente na horizontal" },
    CmdInfo { id: M_DIST_V, label: "Distribuir na vertical", hint: "Espaçar 3 ou mais objetos igualmente na vertical" },
    CmdInfo { id: M_LOCK, label: "Bloquear", hint: "Bloquear o objeto selecionado (Ctrl+L)" },
];

pub const RULERS_CMD: CmdInfo = CmdInfo { id: M_RULERS, label: "Réguas", hint: "Mostrar réguas, guias e coordenadas (Ctrl+Shift+R)" };

/// The icon of each tool (the C++ kTools table).
pub fn tool_icon(t: Tool) -> Icon {
    match t {
        Tool::Select => Icon::Select,
        Tool::Arrow => Icon::Arrow,
        Tool::Line => Icon::Line,
        Tool::Shape => Icon::Rect,
        Tool::Pen => Icon::Pen,
        Tool::Highlighter => Icon::Highlighter,
        Tool::Text => Icon::Text,
        Tool::Callout => Icon::Callout,
        Tool::Step => Icon::Step,
        Tool::Stamp => Icon::Stamp,
        Tool::Blur => Icon::Blur,
        Tool::Spotlight => Icon::Spotlight,
        Tool::Magnify => Icon::Magnify,
        Tool::Fill => Icon::Fill,
        Tool::Eraser => Icon::Eraser,
        Tool::Crop => Icon::Crop,
        Tool::Selection => Icon::Selection,
        Tool::Cutout => Icon::Cutout,
        Tool::Eyedropper => Icon::Eyedropper,
        Tool::Favorites => Icon::Star,
        Tool::MagicWand => Icon::Wand,
    }
}

/// The icon of a menu command, an output or an effect (the C++ kCommands, kOutputs and DescribeEntry).
pub fn command_icon(id: u32) -> Option<Icon> {
    Some(match id {
        T_CAPTURE => Icon::Capture,
        T_RECORD => Icon::VideoFill,
        T_COPYALL => Icon::Copy,
        T_SHARE => Icon::Share,
        T_SAVE => Icon::Save,
        M_EMAIL => Icon::Mail,
        M_OPEN_PROGRAM => Icon::Program,
        M_PRINT => Icon::Print,
        M_UNDO => Icon::Undo,
        M_REDO => Icon::Redo,
        M_NEW_IMAGE => Icon::NewDoc,
        M_NEW_CLIPBOARD => Icon::NewClipboard,
        M_OPEN => Icon::Folder,
        M_SAVE => Icon::Save,
        M_SAVEAS => Icon::SaveAs,
        M_DUPLICATE => Icon::Duplicate,
        M_DELETE_SEL => Icon::DeleteObject,
        M_OCR => Icon::Ocr,
        M_DELETE => Icon::Trash,
        M_FLATTEN => Icon::Layers,
        M_ROT_CW => Icon::Rotate,
        M_TRIM => Icon::AutoTrim,
        M_ALIGN_LEFT => Icon::AlignLeft,
        M_ALIGN_CENTER => Icon::AlignCenter,
        M_ALIGN_RIGHT => Icon::AlignRight,
        M_ALIGN_TOP => Icon::AlignTop,
        M_ALIGN_MIDDLE => Icon::AlignMiddle,
        M_ALIGN_BOTTOM => Icon::AlignBottom,
        M_DIST_H => Icon::DistributeH,
        M_DIST_V => Icon::DistributeV,
        M_LOCK => Icon::Lock,
        M_UNLOCK => Icon::Unlock,
        M_RULERS => Icon::Ruler,
        M_PIN => Icon::Pin,
        M_UPLOAD => Icon::Upload,
        M_COMBINE => Icon::Combine,
        M_COMPARE => Icon::Compare,
        M_BEAUTIFY => Icon::Beautify,
        M_READ_QR => Icon::Qr,
        M_DATESTAMP => Icon::Clock,
        M_CONTRAST => Icon::Contrast,
        M_ASSISTANT => Icon::Sparkle,
        M_SPLIT => Icon::Split,
        M_FX_BORDER => Icon::FxBorder,
        M_FX_EDGE => Icon::FxEdges,
        M_FX_CAPTION => Icon::FxCaption,
        M_FX_PERSPECTIVE => Icon::FxPerspective,
        M_FX_PAGECURL => Icon::FxPageCurl,
        M_FX_SHADOW => Icon::FxShadow,
        M_FX_FILTER => Icon::FxFilters,
        M_FX_ADJUST => Icon::FxColorAdjust,
        M_FX_REPLACE => Icon::FxColorReplace,
        M_FX_SPOTLIGHT => Icon::FxSpotlight,
        M_FX_WATERMARK => Icon::FxWatermark,
        _ => return None,
    })
}

/// The icon of a toolbar entry.
pub fn entry_icon(e: &Entry) -> Icon {
    match e.kind {
        EntryKind::Tool => TOOLS.iter().map(|(t, _)| *t).find(|t| t.index() as u32 == e.id).map_or(Icon::Gear, tool_icon),
        EntryKind::Effect => command_icon(e.id).unwrap_or(Icon::Effects),
        EntryKind::Output | EntryKind::Command => command_icon(e.id).unwrap_or(Icon::Gear),
        EntryKind::Spacer => Icon::Rect,
    }
}

pub const FIXED_TOOLS: [Tool; 5] = [Tool::Select, Tool::Selection, Tool::Text, Tool::Highlighter, Tool::Shape];

pub fn is_fixed_tool(t: Tool) -> bool {
    FIXED_TOOLS.contains(&t) || t == Tool::Magnify
}

/// The label and hint of a toolbar entry.
pub fn describe(e: &Entry, effect_name: &dyn Fn(u32) -> Option<String>) -> (String, String) {
    match e.kind {
        EntryKind::Tool => TOOLS
            .iter()
            .find(|(t, _)| t.index() as u32 == e.id)
            .map(|(t, h)| (t.name(), tr(h)))
            .unwrap_or_default(),
        EntryKind::Effect => effect_name(e.id)
            .map(|n| (n.clone(), format!("{}{}{}", tr("Efeito: "), n, tr(" — abre os ajustes no painel de efeitos"))))
            .unwrap_or_default(),
        EntryKind::Output => OUTPUTS.iter().find(|c| c.id == e.id).map(|c| (tr(c.label), tr(c.hint))).unwrap_or_default(),
        EntryKind::Command => COMMANDS
            .iter()
            .chain(std::iter::once(&RULERS_CMD))
            .find(|c| c.id == e.id)
            .map(|c| (tr(c.label), tr(c.hint)))
            .unwrap_or_default(),
        EntryKind::Spacer => (String::new(), String::new()),
    }
}

/// What the toolbar asks for.
#[derive(Clone, Debug)]
pub enum BarAction {
    Command(u32),
    Entry(Entry),
    More(egui::Pos2),
    Hint(String),
}

/// Draws the toolbar: Capture and Record on the left, the tools and items in the middle, the outputs on the right.
pub fn toolbar(ui: &mut egui::Ui, center: &[Entry], active: Tool, effect_name: &dyn Fn(u32) -> Option<String>) -> Vec<BarAction> {
    let mut acts = Vec::new();
    let hint = |r: &egui::Response, h: &str, acts: &mut Vec<BarAction>| {
        if r.hovered() && !h.is_empty() {
            acts.push(BarAction::Hint(h.to_owned()));
        }
    };
    ui.horizontal_centered(|ui| {
        ui.spacing_mut().item_spacing.x = 2.0;
        for (id, label, h, primary) in [
            (T_CAPTURE, "Capturar", "Nova captura de imagem (pede ao Snapnab)", true),
            (T_RECORD, "Gravar", "Nova gravação de vídeo", false),
        ] {
            let icon = command_icon(id).unwrap_or(Icon::Capture);
            let r = icons::tile(ui, icon, &tr(label), false, primary.then_some(CAPTURE_RED));
            tag(ui, &r, &format!("SnapnabToolbar.{id}"));
            hint(&r, &tr(h), &mut acts);
            if r.clicked() {
                acts.push(BarAction::Command(id));
            }
        }
        ui.add_space(6.0);
        ui.separator();
        // the tools and items are centered on the window, as long as they clear both ends
        let outputs = [
            (T_SHARE, "Compartilhar", "Compartilhar: arquivo, área de transferência, e-mail, impressora, programa"),
            (T_SAVE, "Salvar como", "Salvar como arquivo (Ctrl+Shift+S)"),
            (T_COPYALL, "Copiar tudo", "Copiar a imagem inteira (Ctrl+Shift+C)"),
        ];
        let spacing = ui.spacing().item_spacing.x;
        let right_w: f32 = outputs.iter().map(|o| icons::tile_width(ui, &tr(o.1)) + spacing).sum();
        let width_id = egui::Id::NULL.with("snapedit.toolbar.center");
        let center_w = ui.ctx().data(|d| d.get_temp::<f32>(width_id)).unwrap_or(0.0);
        let full = ui.max_rect();
        let left_end = ui.cursor().left() + 6.0;
        let start = (full.center().x - center_w / 2.0).min(full.right() - right_w - 12.0 - center_w).max(left_end);
        ui.add_space(start - ui.cursor().left());
        let x0 = ui.cursor().left();
        for e in center {
            if e.kind == EntryKind::Spacer {
                ui.add_space(6.0);
                ui.separator();
                ui.add_space(6.0);
                continue;
            }
            let (label, h) = describe(e, effect_name);
            if label.is_empty() {
                continue;
            }
            let on = e.kind == EntryKind::Tool && e.id == active.index() as u32;
            let r = icons::tile(ui, entry_icon(e), &label, on, None);
            let id = match e.kind {
                EntryKind::Tool => T_TOOL_BASE + e.id,
                _ => e.id,
            };
            tag(ui, &r, &format!("SnapnabToolbar.{id}"));
            hint(&r, &h, &mut acts);
            if r.clicked() {
                acts.push(BarAction::Entry(*e));
            }
        }
        let r = more_tile(ui);
        tag(ui, &r, "SnapnabToolbar.More");
        if r.clicked() {
            acts.push(BarAction::More(r.rect.left_bottom()));
        }
        let w = r.rect.right() - x0;
        if (w - center_w).abs() > 0.5 {
            // measured now: drawn centered from the next pass on, without showing this one
            ui.ctx().data_mut(|d| d.insert_temp(width_id, w));
            ui.ctx().request_discard("toolbar centering");
        }
        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            for (id, label, h) in outputs {
                let r = icons::tile(ui, command_icon(id).unwrap_or(Icon::Gear), &tr(label), false, None);
                tag(ui, &r, &format!("SnapnabToolbar.{id}"));
                hint(&r, &tr(h), &mut acts);
                if r.clicked() {
                    acts.push(BarAction::Command(id));
                }
            }
        });
    });
    acts
}

/// The Capture button's red (ui::kCaptureRed).
pub const CAPTURE_RED: Color32 = Color32::from_rgb(214, 40, 60);

/// "More": three dots over the label, like the other tiles.
fn more_tile(ui: &mut egui::Ui) -> egui::Response {
    let label = tr("Mais");
    let font = egui::FontId::proportional(11.5);
    let w = (ui.painter().layout_no_wrap(label.clone(), font.clone(), Color32::WHITE).size().x + 14.0).max(52.0);
    let (rect, r) = ui.allocate_exact_size(egui::vec2(w, 52.0), egui::Sense::click());
    let v = ui.visuals();
    let fg = v.text_color();
    if r.hovered() {
        ui.painter().rect_filled(rect, v.widgets.inactive.corner_radius.at_least(6), v.widgets.hovered.weak_bg_fill);
    }
    let cy = rect.top() + 18.0;
    for k in -1..=1 {
        ui.painter().circle_filled(egui::pos2(rect.center().x + k as f32 * 7.6, cy), 2.0, fg);
    }
    ui.painter().text(egui::pos2(rect.center().x, rect.bottom() - 10.0), egui::Align2::CENTER_CENTER, label, font, fg);
    r
}

// ---------------------------------------------------------------- layers
/// The icon of an object's type in the layers list (TypeIcon).
pub fn type_icon(o: &DrawObj) -> Icon {
    match o.kind {
        ObjType::Arrow => Icon::Arrow,
        ObjType::Line => Icon::Line,
        ObjType::Pen => Icon::Pen,
        ObjType::Highlighter => Icon::Highlighter,
        ObjType::Text => Icon::Text,
        ObjType::Callout => Icon::Callout,
        ObjType::Step => Icon::Step,
        ObjType::Stamp => Icon::Stamp,
        ObjType::Blur | ObjType::Pixelate => Icon::Blur,
        ObjType::Spotlight => Icon::Spotlight,
        ObjType::Magnify => Icon::Magnify,
        ObjType::Picture => Icon::Image,
        ObjType::Group => Icon::Group,
        ObjType::Link => Icon::Link,
        ObjType::Ellipse => Icon::Ellipse,
        _ => Icon::Rect,
    }
}

/// A small frameless button with an icon; `faint` grays it (a hidden object's eye, an unlocked padlock).
fn icon_button(ui: &mut egui::Ui, icon: Icon, faint: bool) -> egui::Response {
    let c = if faint { ui.visuals().weak_text_color() } else { ui.visuals().text_color() };
    ui.add(egui::Button::image(icons::image(ui.ctx(), icon, 16.0, c)).frame(false))
}

pub fn display_name(o: &DrawObj) -> String {
    if !o.name.is_empty() {
        return o.name.clone();
    }
    let text = String::from_utf16_lossy(&o.text);
    let first = || {
        let l = text.lines().next().unwrap_or("").to_owned();
        if l.chars().count() > 40 { l.chars().take(40).collect::<String>() + "…" } else { l }
    };
    const SHAPES: [&str; 8] = ["Retângulo", "Retângulo arredondado", "Elipse", "Triângulo", "Losango", "Paralelogramo", "Hexágono", "Estrela"];
    match o.kind {
        ObjType::Arrow => if text.is_empty() { tr("Seta") } else { format!("{}: {}", tr("Seta"), first()) },
        ObjType::Line => tr("Linha"),
        ObjType::Pen => tr("Caneta"),
        ObjType::Highlighter => tr("Marcador"),
        ObjType::Text => if text.is_empty() { tr("Texto") } else { first() },
        ObjType::Callout => if text.is_empty() { tr("Balão") } else { first() },
        ObjType::Step => tr("Passo %s").replace("%s", &snapcore::doc::step_label(o.step_value, o.style.step_type)),
        ObjType::Stamp => {
            if let Some(rest) = text.strip_prefix("label:") {
                rest.split_once(':').map_or(rest.to_owned(), |(_, t)| t.to_owned())
            } else {
                tr("Carimbo")
            }
        }
        ObjType::Blur => tr("Desfoque"),
        ObjType::Pixelate => tr("Pixelado"),
        ObjType::Spotlight => tr("Destaque"),
        ObjType::Magnify => tr("Lupa"),
        ObjType::Picture => if o.floating { tr("Peça solta") } else { tr("Imagem") },
        ObjType::Group => tr("Grupo (%zu)").replace("%zu", &o.children.len().to_string()),
        ObjType::Link => format!("{}{}", tr("Link: "), text),
        _ => tr(SHAPES[o.shape_kind().clamp(0, 7) as usize]),
    }
}

#[derive(Clone, Debug)]
pub enum LayerAction {
    Select(usize, bool),
    Hidden(usize, bool),
    Locked(usize, bool),
    /// moves object `.0` to just before index `.1` (in document order)
    Move(usize, usize),
    Rename(usize),
    ContextMenu,
}

/// The layers list: the objects from the front (last drawn) to the back, then the image itself.
pub fn layers(ui: &mut egui::Ui, c: &Canvas) -> Vec<LayerAction> {
    let mut acts = Vec::new();
    let bg = ui.allocate_rect(ui.max_rect(), egui::Sense::hover());
    tag(ui, &bg, "SnapnabLayers");
    let mut ui = ui.new_child(egui::UiBuilder::new().max_rect(ui.max_rect().shrink(6.0)));
    ui.label(RichText::new(tr("Camadas")).strong());
    let Some(d) = c.doc().filter(|d| !d.is_video) else {
        ui.label(RichText::new(tr("Nenhuma imagem aberta.")).color(Color32::from_gray(120)));
        return acts;
    };
    let n = d.st.objs.len();
    if n == 0 {
        ui.label(RichText::new(tr("Os objetos desenhados (setas, textos, formas…) aparecem aqui, do mais à frente ao mais atrás."))
            .color(Color32::from_gray(120)));
    }
    egui::ScrollArea::vertical().id_salt("layers-scroll").auto_shrink([false, false]).show(&mut ui, |ui| {
        for i in (0..n).rev() {
            let o = &d.st.objs[i];
            let sel = c.is_selected(i);
            ui.horizontal(|ui| {
                let eye = icon_button(ui, if o.hidden { Icon::EyeOff } else { Icon::Eye }, o.hidden).on_hover_text(tr("Mostrar ou ocultar"));
                tag(ui, &eye, &format!("SnapnabLayers.Eye.{i}"));
                if eye.clicked() {
                    acts.push(LayerAction::Hidden(i, !o.hidden));
                }
                let lock = icon_button(ui, if o.locked { Icon::Lock } else { Icon::Unlock }, !o.locked).on_hover_text(tr("Bloquear ou desbloquear"));
                tag(ui, &lock, &format!("SnapnabLayers.Lock.{i}"));
                if lock.clicked() {
                    acts.push(LayerAction::Locked(i, !o.locked));
                }
                let img = icons::image(ui.ctx(), type_icon(o), 16.0, ui.visuals().text_color());
                let r = ui.add(egui::Button::image_and_text(img, display_name(o)).selected(sel).frame_when_inactive(false));
                tag(ui, &r, &format!("SnapnabLayers.{i}"));
                if r.clicked() {
                    acts.push(LayerAction::Select(i, ui.input(|x| x.modifiers.ctrl)));
                }
                if r.double_clicked() {
                    acts.push(LayerAction::Rename(i));
                }
                if r.secondary_clicked() {
                    if !sel {
                        acts.push(LayerAction::Select(i, false));
                    }
                    acts.push(LayerAction::ContextMenu);
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if i > 0 && ui.small_button("▼").on_hover_text(tr("Enviar para trás")).clicked() {
                        acts.push(LayerAction::Move(i, i - 1));
                    }
                    if i + 1 < n && ui.small_button("▲").on_hover_text(tr("Trazer para a frente")).clicked() {
                        acts.push(LayerAction::Move(i, i + 2));
                    }
                });
            });
        }
        ui.label(RichText::new(tr("Fundo (imagem)")).color(Color32::from_gray(110)));
    });
    acts
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn toolbar_entries_round_trip() {
        let v = vec![
            Entry { kind: EntryKind::Tool, id: 5 },
            Entry { kind: EntryKind::Effect, id: 1039 },
            Entry { kind: EntryKind::Output, id: 6 },
            Entry { kind: EntryKind::Command, id: 1015 },
            Entry { kind: EntryKind::Spacer, id: 0 },
        ];
        let s = entries_to_string(&v);
        assert_eq!(s, "t:5,e:1039,o:6,c:1015,s:0");
        assert_eq!(entries_from_string(&s), v);
    }
}
