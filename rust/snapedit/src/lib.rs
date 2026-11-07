//! The parts of SnapnabEditor.exe that do not need its windows running, so the tests reach them: the document and its
//! undo history, the canvas (tools, selection, zoom) and the panels' drawing.

pub mod canvas;
pub mod doc;
pub mod files;
pub mod tools;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// The text in the interface language (the program's own strings are in Portuguese).
pub fn tr(s: &str) -> String {
    snapcore::i18n::tr_str(s)
}

/// Opens a file, folder or address with its default program (ShellExecute "open").
pub fn shell_open(target: &str) {
    use windows::Win32::UI::Shell::ShellExecuteW;
    use windows::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL;
    use windows::core::{PCWSTR, w};
    let t: Vec<u16> = target.encode_utf16().chain([0]).collect();
    unsafe {
        ShellExecuteW(None, w!("open"), PCWSTR(t.as_ptr()), PCWSTR::null(), PCWSTR::null(), SW_SHOWNORMAL);
    }
}
