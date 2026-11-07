//! The capture window drawn on the GPU against tests/snapshots/capture_window.png, and its clicks through the
//! accessibility tree (the names a screen reader and UI Automation see).
//! After an intended change: UPDATE_SNAPSHOTS=1 cargo test -p snapnab, then look at the new image before committing.

use egui_kittest::Harness;
use egui_kittest::kittest::Queryable;
use snapcore::presets::{CaptureKind, PresetStore, Selection, default_presets};
use snapnab_app::window::{Action, draw};

struct State {
    store: PresetStore,
    actions: Vec<Action>,
}

fn harness() -> Harness<'static, State> {
    let state = State { store: PresetStore { presets: default_presets(), selected: 0 }, actions: Vec::new() };
    Harness::builder().with_size(egui::vec2(640.0, 330.0)).build_ui_state(
        |ui, s: &mut State| {
            snapui::install_fonts(ui.ctx());
            let a = draw(&mut s.store, ui);
            s.actions.extend(a);
        },
        state,
    )
}

#[test]
fn looks_like_the_snapshot() {
    let mut h = harness();
    h.run();
    h.snapshot("capture_window");
}

#[test]
fn clicks_change_the_preset_and_capture() {
    let mut h = harness();
    h.run();
    let video = h.state().store.presets.iter().position(|p| p.kind == CaptureKind::Video).unwrap();
    let name = format!("🎬 {}", snapcore::i18n::tr_str(&h.state().store.presets[video].name));
    h.get_by_label(&name).click();
    h.run();
    assert_eq!(h.state().store.selected, video as i32);
    h.get_by_label("Imagem").click();
    h.run();
    let p = &h.state().store.presets[video];
    assert_eq!(p.kind, CaptureKind::Image);
    assert!(Selection::for_kind(CaptureKind::Image).contains(&p.selection));
    h.get_by_label("Capturar imagem").click();
    h.run();
    let a = &h.state().actions;
    assert!(a.contains(&Action::Changed) && a.last() == Some(&Action::Capture), "{a:?}");
}
