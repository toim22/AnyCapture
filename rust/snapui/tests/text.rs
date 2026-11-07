//! The interface's text in every script Snapnab ships, rendered on the GPU (wgpu) as the window draws it, against the
//! image in tests/snapshots (Arabic and Urdu joined and right to left, Hindi and Bengali with their conjuncts).
//! After an intended change: UPDATE_SNAPSHOTS=1 cargo test -p snapui, then look at the new image before committing.

use egui_kittest::Harness;

#[test]
fn scripts_render() {
    // Windows Server (the CI runner) has no Nirmala UI, the font of the Indic scripts
    let dir = std::env::var("WINDIR").unwrap_or_else(|_| r"C:\Windows".into());
    if !std::path::Path::new(&dir).join(r"Fonts\Nirmala.ttc").exists() {
        eprintln!("skipped: Nirmala UI is not installed");
        return;
    }
    snapui::fixed_look_for_tests();
    let mut harness = Harness::builder().with_size(egui::vec2(760.0, 330.0)).build_ui(|ui| {
        snapui::install_fonts(ui.ctx());
        snapui::script_probe(ui);
    });
    harness.run();
    harness.snapshot("scripts");
}
