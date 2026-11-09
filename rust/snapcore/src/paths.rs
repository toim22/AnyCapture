//! Folders of the program and of its local state (ds_arch §16.2), as ExeDir / AppDataDir / LogsDir in Util.cpp.

use std::path::PathBuf;

use windows::Win32::System::Com::CoTaskMemFree;
use windows::Win32::UI::Shell::{FOLDERID_LocalAppData, KF_FLAG_CREATE, SHGetKnownFolderPath};
use windows::core::GUID;

/// Folder of the running executable (where lang\ and help\ are).
pub fn exe_dir() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(PathBuf::from)).unwrap_or_default()
}

/// A known folder of the user (FOLDERID_*), created if missing.
pub(crate) fn known_folder(id: &GUID) -> Option<PathBuf> {
    unsafe {
        let p = SHGetKnownFolderPath(id, KF_FLAG_CREATE, None).ok()?;
        let dir = PathBuf::from(crate::wide::to_os(p.as_wide()));
        CoTaskMemFree(Some(p.0 as *const _));
        Some(dir)
    }
}

fn local_app_data() -> PathBuf {
    known_folder(&FOLDERID_LocalAppData).unwrap_or_default()
}

fn ensured(dir: PathBuf) -> PathBuf {
    let _ = std::fs::create_dir_all(&dir);
    dir
}

/// %LOCALAPPDATA%\AnyCapture (created if missing)
pub fn app_data_dir() -> PathBuf {
    ensured(local_app_data().join("AnyCapture"))
}

/// %LOCALAPPDATA%\AnyCapture\Logs (created if missing)
pub fn logs_dir() -> PathBuf {
    ensured(app_data_dir().join("Logs"))
}
