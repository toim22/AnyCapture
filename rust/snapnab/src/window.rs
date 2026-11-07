//! The capture window (comp_1 §2, src/capture/main.cpp): the presets on the left; on the right the current preset's
//! kind (image or video), selection mode, delay, a few switches and the capture button. Drawing returns what the
//! clicks ask for; the app does it.

use egui::{Color32, RichText};
use snapcore::i18n;
use snapcore::presets::{CaptureKind, PresetStore, Selection};

use crate::tr;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    /// the presets changed (to be saved)
    Changed,
    Capture,
    OpenEditor,
    LibraryFolder,
    Logs,
    About,
    Exit,
}

pub fn draw(store: &mut PresetStore, ui: &mut egui::Ui) -> Vec<Action> {
    let mut out = Vec::new();
    egui::Panel::top("menu").show(ui, |ui| {
        egui::MenuBar::new().ui(ui, |ui| {
            ui.menu_button(tr("Arquivo"), |ui| {
                for (text, a) in [
                    ("Abrir o Editor", Action::OpenEditor),
                    ("Abrir a pasta da biblioteca", Action::LibraryFolder),
                    ("Abrir a pasta de logs", Action::Logs),
                ] {
                    if ui.button(tr(text)).clicked() {
                        out.push(a);
                    }
                }
                ui.separator();
                if ui.button(tr("Sair do Snapnab")).clicked() {
                    out.push(Action::Exit);
                }
            });
            ui.menu_button(tr("Ajuda"), |ui| {
                if ui.button(tr("Sobre o Snapnab")).clicked() {
                    out.push(Action::About);
                }
            });
        });
    });
    egui::Panel::left("presets").resizable(false).exact_size(220.0).show(ui, |ui| {
        ui.label(RichText::new(tr("Presets")).strong());
        egui::ScrollArea::vertical().show(ui, |ui| {
            for (i, p) in store.presets.iter().enumerate() {
                let sel = i as i32 == store.selected;
                let icon = if p.kind == CaptureKind::Video { "🎬" } else { "📷" };
                if ui.selectable_label(sel, format!("{icon} {}", i18n::tr_str(&p.name))).clicked() && !sel {
                    store.selected = i as i32;
                    out.push(Action::Changed);
                }
            }
        });
    });
    egui::CentralPanel::default().show(ui, |ui| {
        let Some(p) = store.presets.get_mut(store.selected as usize) else { return };
        let mut changed = false;
        ui.horizontal(|ui| {
            for (k, name) in [(CaptureKind::Image, tr("Imagem")), (CaptureKind::Video, tr("Vídeo"))] {
                if ui.selectable_label(p.kind == k, name).clicked() && p.kind != k {
                    p.kind = k;
                    if !Selection::for_kind(k).contains(&p.selection) {
                        p.selection = Selection::Region;
                    }
                    changed = true;
                }
            }
        });
        ui.add_space(6.0);
        egui::Grid::new("opts").num_columns(2).spacing([12.0, 6.0]).show(ui, |ui| {
            let label = ui.label(tr("Seleção"));
            egui::ComboBox::from_id_salt("selection")
                .selected_text(p.selection.name())
                .show_ui(ui, |ui| {
                    for &s in Selection::for_kind(p.kind) {
                        changed |= ui.selectable_value(&mut p.selection, s, s.name()).changed();
                    }
                })
                .response
                .labelled_by(label.id);
            ui.end_row();
            let label = ui.label(tr("Atraso"));
            let delay = ui.add(egui::DragValue::new(&mut p.delay_seconds).range(0..=600).suffix(" s"));
            changed |= delay.labelled_by(label.id).changed();
            ui.end_row();
        });
        ui.add_space(6.0);
        changed |= ui.checkbox(&mut p.preview_in_editor, tr("Abrir no Editor")).changed();
        changed |= ui.checkbox(&mut p.copy_to_clipboard, tr("Copiar para a área de transferência")).changed();
        changed |= ui.checkbox(&mut p.capture_cursor, tr("Capturar o cursor")).changed();
        ui.add_space(10.0);
        let video = p.kind == CaptureKind::Video;
        let label = if video { tr("Gravar vídeo") } else { tr("Capturar imagem") };
        let fill = if video { Color32::from_rgb(214, 40, 60) } else { Color32::from_rgb(30, 120, 230) };
        let b = egui::Button::new(RichText::new(label).size(16.0).color(Color32::WHITE))
            .fill(fill)
            .min_size(egui::vec2(180.0, 40.0))
            .corner_radius(20.0);
        if ui.add(b).clicked() {
            out.push(Action::Capture);
        }
        if changed {
            out.push(Action::Changed);
        }
    });
    out
}
