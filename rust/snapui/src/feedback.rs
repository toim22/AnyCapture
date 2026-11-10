//! Help › Report a problem… and Help › Send a suggestion or feedback… in both programs: an egui window of its own
//! (class AnyCaptureFeedback, in the programs' theme) with the title, the description, the steps or the reason and the
//! system information; Send files the GitHub issue in the background (`snapcore::project::send_issue`: the relay, no
//! GitHub account needed) and the window then says what became of it. The program keeps the `Feedback`, draws it
//! with `ui` and closes the window when `ui` says so.

use std::sync::mpsc::{Receiver, channel};

use egui::{Color32, RichText, vec2};
use snapcore::project::{self, Issue, IssueKind, Sent};

use crate::host::{Ctl, WinKey, WindowSpec};
use crate::theme;
use crate::widgets;

fn tr(s: &str) -> String {
    snapcore::i18n::tr_str(s)
}

pub struct Feedback {
    pub key: WinKey,
    kind: IssueKind,
    program: String,
    version: String,
    title: String,
    text: String,
    more: String,
    system: bool,
    system_text: String,
    /// the title and the description are required: said once Send was clicked without them
    missing: bool,
    focused: bool,
    sending: Option<Receiver<Sent>>,
    result: Option<Sent>,
}

impl Feedback {
    /// The window's title.
    fn caption(kind: IssueKind) -> String {
        if kind == IssueKind::Bug { tr("Relatar um problema") } else { tr("Enviar sugestão ou feedback") }
    }

    /// Opens the window. `program` names the program in the issue ("AnyCapture", "AnyCapture Editor").
    pub fn open(ctl: &mut Ctl, kind: IssueKind, program: &str, version: &str) -> Option<Feedback> {
        let [r, g, b, _] = theme::tokens().window.to_normalized_gamma_f32();
        let spec = WindowSpec {
            class: "AnyCaptureFeedback",
            title: Self::caption(kind),
            size: (560.0, 560.0),
            resizable: false,
            clear_color: [r, g, b, 1.0],
            ..Default::default()
        };
        let key = ctl.create(&spec)?;
        Some(Feedback {
            key,
            kind,
            program: program.to_owned(),
            version: version.to_owned(),
            title: String::new(),
            text: String::new(),
            more: String::new(),
            system: kind == IssueKind::Bug,
            system_text: project::system_info(),
            missing: false,
            focused: false,
            sending: None,
            result: None,
        })
    }

    /// Brings the open window to the front (the menu chosen again).
    pub fn focus(&self, ctl: &Ctl) {
        if let Some(w) = ctl.window(self.key) {
            w.focus_window();
        }
    }

    /// Whether closing now would drop a report on its way (the close button waits for it).
    pub fn busy(&self) -> bool {
        self.sending.is_some()
    }

    /// Draws a frame; true when the window should close.
    pub fn ui(&mut self, ui: &mut egui::Ui) -> bool {
        if let Some(rx) = &self.sending {
            match rx.try_recv() {
                Ok(r) => {
                    self.sending = None;
                    self.result = Some(r);
                }
                Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                    self.sending = None;
                    self.result = Some(Sent::Failed(format!("{}/issues", project::home())));
                }
                Err(std::sync::mpsc::TryRecvError::Empty) => ui.ctx().request_repaint_after(std::time::Duration::from_millis(100)),
            }
        }
        let mut close = self.sending.is_none() && ui.input(|i| i.key_pressed(egui::Key::Escape));
        let t = theme::tokens();
        egui::Panel::bottom("buttons").exact_size(60.0).resizable(false).show_separator_line(false).show(ui, |ui| {
            ui.add_space(12.0);
            ui.horizontal(|ui| {
                ui.add_space(8.0);
                if self.result.is_none() {
                    let (text, filter) = if self.kind == IssueKind::Bug {
                        (tr("Ver os relatos enviados"), "label%3Abug")
                    } else {
                        (tr("Ver as sugestões enviadas"), "label%3Aenhancement")
                    };
                    if ui.link(RichText::new(text).color(t.accent)).clicked() {
                        project::open_url(&format!("{}/issues?q=is%3Aissue+{filter}", project::home()));
                    }
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.add_space(8.0);
                    if self.result.is_some() {
                        close |= primary(ui, &tr("Fechar"), true).clicked();
                        if matches!(self.result, Some(Sent::Later | Sent::Failed(_))) && secondary(ui, &tr("Voltar"), true).clicked() {
                            self.result = None;
                        }
                        return;
                    }
                    let sending = self.sending.is_some();
                    let label = if sending { tr("Enviando…") } else { tr("Enviar") };
                    if primary(ui, &label, !sending).clicked() {
                        self.send();
                    }
                    if secondary(ui, &tr("Cancelar"), !sending).clicked() {
                        close = true;
                    }
                    if sending {
                        ui.spinner();
                    }
                });
            });
        });
        egui::CentralPanel::default().frame(egui::Frame::new().fill(t.window).inner_margin(egui::Margin::symmetric(20, 16))).show(ui, |ui| {
            if self.result.is_some() {
                self.result_ui(ui);
            } else {
                self.form_ui(ui);
            }
        });
        close
    }

    fn form_ui(&mut self, ui: &mut egui::Ui) {
        let t = theme::tokens();
        let bug = self.kind == IssueKind::Bug;
        let enabled = self.sending.is_none();
        ui.spacing_mut().item_spacing.y = 6.0;
        ui.label(RichText::new(if bug {
            tr("Descreva o problema. Ele é enviado ao projeto AnyCapture no GitHub como um novo relato (bug); não é preciso ter conta no GitHub.")
        } else {
            tr("Conte sua ideia ou o que acha do AnyCapture. Ela é enviada ao projeto AnyCapture no GitHub como uma sugestão; não é preciso ter conta no GitHub.")
        }).color(t.text_dim));
        ui.add_space(8.0);
        ui.add_enabled_ui(enabled, |ui| {
            ui.label(tr("Título"));
            let hint = if bug { tr("Ex.: o Editor fecha ao colar uma imagem") } else { tr("Ex.: atalho para repetir a última região") };
            let r = ui.add(egui::TextEdit::singleline(&mut self.title).hint_text(hint).desired_width(f32::INFINITY).margin(vec2(8.0, 6.0)));
            if !self.focused {
                r.request_focus();
                self.focused = true;
            }
            ui.add_space(4.0);
            ui.label(if bug { tr("O que aconteceu?") } else { tr("Sua sugestão ou feedback") });
            ui.add(egui::TextEdit::multiline(&mut self.text).desired_rows(6).desired_width(f32::INFINITY).margin(vec2(8.0, 6.0)));
            ui.add_space(4.0);
            ui.label(if bug { tr("Passos para reproduzir (opcional)") } else { tr("Por que ajudaria? (opcional)") });
            let hint = if bug { "1. …\n2. …".to_owned() } else { String::new() };
            ui.add(egui::TextEdit::multiline(&mut self.more).hint_text(hint).desired_rows(3).desired_width(f32::INFINITY).margin(vec2(8.0, 6.0)));
            ui.add_space(8.0);
            widgets::toggle(ui, &mut self.system, &tr("Incluir a versão do Windows e o idioma"), ui.available_width());
            ui.label(RichText::new(format!("AnyCapture {}{}", self.version, if self.system { format!(" · {}", self.system_text) } else { String::new() }))
                .size(t.body - 1.5)
                .color(t.text_dim));
        });
        if self.missing && (self.title.trim().is_empty() || self.text.trim().is_empty()) {
            ui.add_space(6.0);
            ui.label(RichText::new(tr("Preencha o título e a descrição.")).color(warning()));
        }
    }

    fn result_ui(&mut self, ui: &mut egui::Ui) {
        let t = theme::tokens();
        let bug = self.kind == IssueKind::Bug;
        let Some(r) = self.result.clone() else { return };
        let (head, body) = match &r {
            Sent::Created(_) => (if bug { tr("Obrigado! Seu relato foi enviado.") } else { tr("Obrigado! Sua sugestão foi enviada.") },
                                 tr("Ele está no projeto AnyCapture no GitHub:")),
            Sent::Browser => (tr("Quase lá: termine no navegador"),
                              tr("Não foi possível enviar direto agora, então o GitHub abriu no navegador com tudo já preenchido. Entre na sua conta do GitHub se for pedido, revise e clique em \"Create\" para enviar.")),
            Sent::Later => (tr("Aguarde um pouco"), tr("Vários envios seguidos deste computador. Tente de novo em um minuto.")),
            Sent::Failed(_) => (tr("Não foi possível enviar"), tr("Não foi possível enviar nem abrir o navegador. Envie pelo endereço:")),
        };
        ui.add_space(24.0);
        ui.label(RichText::new(head).size(20.0).color(t.text));
        ui.add_space(10.0);
        ui.label(RichText::new(body).color(if matches!(r, Sent::Failed(_)) { warning() } else { t.text_dim }));
        if let Sent::Created(url) | Sent::Failed(url) = &r {
            ui.add_space(6.0);
            if ui.link(RichText::new(url).color(t.accent)).clicked() {
                project::open_url(url);
            }
        }
    }

    /// Checks the required fields, then sends on a thread of its own.
    fn send(&mut self) {
        if self.title.trim().is_empty() || self.text.trim().is_empty() {
            self.missing = true;
            return;
        }
        let issue = Issue {
            title: self.title.trim().to_owned(),
            text: self.text.clone(),
            more: self.more.clone(),
            app_version: self.version.clone(),
            program: self.program.clone(),
            system: self.system,
        };
        let kind = self.kind;
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let _ = tx.send(project::send_issue(kind, &issue));
        });
        self.sending = Some(rx);
    }
}

fn warning() -> Color32 {
    if theme::tokens().dark { Color32::from_rgb(0xFF, 0x99, 0xA4) } else { Color32::from_rgb(0xC4, 0x2B, 0x1C) }
}

/// The accent button (Send, Close).
fn primary(ui: &mut egui::Ui, text: &str, enabled: bool) -> egui::Response {
    let t = theme::tokens();
    ui.add_enabled(enabled, egui::Button::new(RichText::new(text).color(t.on_accent)).fill(t.accent).min_size(vec2(96.0, 32.0)))
}

/// A plain button (Cancel, Back).
fn secondary(ui: &mut egui::Ui, text: &str, enabled: bool) -> egui::Response {
    ui.add_enabled(enabled, egui::Button::new(text).min_size(vec2(96.0, 32.0)))
}
