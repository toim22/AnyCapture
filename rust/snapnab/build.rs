//! Embeds the app manifest of the C++ build (src/resources/app.manifest) in Snapnab.exe: per-monitor DPI awareness v2,
//! Common Controls 6 (TaskDialog, themed tooltips) and asInvoker.

fn main() {
    let dir = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let manifest = dir.join("../../src/resources/app.manifest").canonicalize().expect("src/resources/app.manifest");
    println!("cargo:rerun-if-changed={}", manifest.display());
    if std::env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc") {
        println!("cargo:rustc-link-arg-bins=/MANIFEST:EMBED");
        println!("cargo:rustc-link-arg-bins=/MANIFESTINPUT:{}", manifest.display().to_string().trim_start_matches(r"\?\"));
    }
}
