//! The parts of Snapnab.exe that do not need its windows running, so the tests reach them: the command line, the
//! capture window's drawing and the selection over the frozen screen.

pub mod cmdline;
pub mod hud;
pub mod select;
pub mod window;

/// The text in the interface language (the program's own strings are in Portuguese).
pub fn tr(s: &str) -> String {
    snapcore::i18n::tr_str(s)
}
