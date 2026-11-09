//! The program's former name. What the versions released under it left on the computer moves over to AnyCapture the
//! first time either program starts, so the settings, presets, library, captures and saved logins carry on. This module
//! (with the installer, which removes the old install) is the only place that still knows the old name:
//!
//! - the registry key HKCU\Software\<old> becomes HKCU\Software\AnyCapture (copied, then the old key is deleted);
//! - the folders <old> in %LOCALAPPDATA%, Pictures and Music move to AnyCapture (what the new folder already has stays,
//!   and the old folder goes away once it is empty);
//! - paths into those folders kept in the settings (registry strings and the XML settings files), and the defaults that
//!   carried the old name (the watermark text, the upload folder and the S3 prefix), follow;
//! - the "Start with Windows" entry gets the new name and program;
//! - the saved logins in the Credential Manager ("<old>/<name>") move when they are first read (net::load_secret);
//! - .snap documents and settings files written under the old name still open (container, settings).
//!
//! ANYCAPTURE_NO_MIGRATE=1 turns the move off: the end-to-end tests set it, so a test run never moves the data of the
//! computer's user.

use std::ffi::c_void;
use std::path::{Path, PathBuf};

use windows::Win32::Foundation::{CloseHandle, ERROR_NO_MORE_ITEMS, ERROR_SUCCESS, WAIT_ABANDONED, WAIT_OBJECT_0};
use windows::Win32::System::Registry::{
    HKEY, HKEY_CURRENT_USER, KEY_ALL_ACCESS, KEY_READ, KEY_SET_VALUE, REG_EXPAND_SZ, REG_OPTION_NON_VOLATILE, REG_SZ,
    REG_VALUE_TYPE, RRF_RT_REG_SZ, RegCloseKey, RegCopyTreeW, RegCreateKeyExW, RegDeleteKeyValueW, RegDeleteKeyW,
    RegDeleteTreeW, RegEnumKeyExW, RegEnumValueW, RegGetValueW, RegOpenKeyExW, RegSetKeyValueW, RegSetValueExW,
};
use windows::Win32::System::Threading::{CreateMutexW, ReleaseMutex, WaitForSingleObject};
use windows::Win32::UI::Shell::{FOLDERID_LocalAppData, FOLDERID_Music, FOLDERID_Pictures};
use windows::core::{PCWSTR, PWSTR, w};

use crate::wide::to_wide;

/// The former name, as the folders, the registry key and the logins were called.
const OLD: &str = "Snapnab";
const NEW: &str = "AnyCapture";

/// The first 8 bytes of a .snap written under the former name (0xCB and the name); the 16 bytes after them are the same.
pub const SNAP_SIGNATURE: [u8; 8] = [0xCB, b'S', b'N', b'A', b'P', b'N', b'A', b'B'];

/// The root element of a settings file (.snapsettings, and the automatic backups) written under the former name.
pub const SETTINGS_ROOT: &str = "SnapnabSettings";

/// The Credential Manager entry of a saved login under the former name.
pub fn credential_target(name: &str) -> String {
    format!("{OLD}/{name}")
}

const RUN_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\Run";

/// False while ANYCAPTURE_NO_MIGRATE=1 (the end-to-end tests): what the former name left is then only read, never moved.
pub fn enabled() -> bool {
    std::env::var_os("ANYCAPTURE_NO_MIGRATE").is_none_or(|v| v != "1")
}

/// Moves what the former name left (see the module notes). Called by both programs before anything else reads the
/// settings; returns what it did, for the log (which only opens afterwards).
pub fn migrate() -> Vec<String> {
    if !enabled() {
        return Vec::new();
    }
    // both programs may start together (the Editor starts the capture app): one moves, the other waits for it
    let Ok(m) = (unsafe { CreateMutexW(None, false, w!("AnyCapture-Migrate")) }) else { return Vec::new() };
    let r = unsafe { WaitForSingleObject(m, 60_000) };
    let notes = if r == WAIT_OBJECT_0 || r == WAIT_ABANDONED { migrate_all() } else { Vec::new() };
    unsafe {
        let _ = ReleaseMutex(m);
        let _ = CloseHandle(m);
    }
    notes
}

fn migrate_all() -> Vec<String> {
    let mut notes = Vec::new();
    let mut pairs = Vec::new();
    let mut moved = false;
    for id in [FOLDERID_LocalAppData, FOLDERID_Pictures, FOLDERID_Music] {
        let Some(base) = crate::paths::known_folder(&id) else { continue };
        let (old, new) = (base.join(OLD), base.join(NEW));
        if old.is_dir() {
            move_merge(&old, &new);
            moved = true;
            notes.push(format!("Moved {} to {}", old.display(), new.display()));
        }
        pairs.push((old.to_string_lossy().into_owned(), new.to_string_lossy().into_owned()));
    }
    let (old_key, new_key) = (format!(r"Software\{OLD}"), format!(r"Software\{NEW}"));
    if move_key(&old_key, &new_key) {
        moved = true;
        notes.push(format!("Moved the settings HKCU\\{old_key} to HKCU\\{new_key}"));
    }
    if moved {
        rewrite_key(&new_key, &pairs);
        if let Some(base) = crate::paths::known_folder(&FOLDERID_LocalAppData) {
            rewrite_files(&base.join(NEW), &pairs);
        }
    }
    if move_startup_entry() {
        notes.push("Start with Windows: the entry has the new name".into());
    }
    notes
}

/// Moves the folder `old` into `new`: a plain rename when `new` does not exist yet, otherwise entry by entry (what
/// `new` already has stays, and the old copy of it remains in `old`). `old` is removed once it is empty.
fn move_merge(old: &Path, new: &Path) {
    if !new.exists() && std::fs::rename(old, new).is_ok() {
        return;
    }
    let _ = std::fs::create_dir_all(new);
    if let Ok(rd) = std::fs::read_dir(old) {
        for e in rd.flatten() {
            let (src, dst) = (e.path(), new.join(e.file_name()));
            if e.file_type().is_ok_and(|t| t.is_dir()) {
                move_merge(&src, &dst);
            } else if !dst.exists() && std::fs::rename(&src, &dst).is_err() && std::fs::copy(&src, &dst).is_ok() {
                let _ = std::fs::remove_file(&src); // a file in use is copied; the original goes when it can
            }
        }
    }
    let _ = std::fs::remove_dir(old);
}

/// Copies HKCU\`old` with its subkeys and values into HKCU\`new` (its values win over the ones `new` already has), then
/// deletes `old`. False when there was no `old`.
fn move_key(old: &str, new: &str) -> bool {
    let (o, n) = (to_wide(old), to_wide(new));
    let (mut src, mut dst) = (HKEY::default(), HKEY::default());
    unsafe {
        if RegOpenKeyExW(HKEY_CURRENT_USER, PCWSTR(o.as_ptr()), None, KEY_READ, &mut src) != ERROR_SUCCESS {
            return false;
        }
        let created = RegCreateKeyExW(HKEY_CURRENT_USER, PCWSTR(n.as_ptr()), None, PCWSTR::null(), REG_OPTION_NON_VOLATILE,
                                      KEY_ALL_ACCESS, None, &mut dst, None) == ERROR_SUCCESS;
        let copied = created && RegCopyTreeW(src, PCWSTR::null(), dst) == ERROR_SUCCESS;
        let _ = RegCloseKey(src);
        if created {
            let _ = RegCloseKey(dst);
        }
        if copied {
            let _ = RegDeleteTreeW(HKEY_CURRENT_USER, PCWSTR(o.as_ptr()));
            let _ = RegDeleteKeyW(HKEY_CURRENT_USER, PCWSTR(o.as_ptr()));
        }
        copied
    }
}

/// `text` with every (old, new) path replaced (ignoring the case of ASCII letters, as Windows paths do), and the
/// settings that defaulted to the former name given the new one.
fn rewrite_text(text: &str, pairs: &[(String, String)]) -> String {
    let mut s = text.to_owned();
    for (old, new) in pairs.iter().filter(|(o, _)| !o.is_empty()) {
        s = replace_ascii_ci(&s, old, new);
    }
    for (a, b) in [
        (format!("WatermarkText=\"{OLD}\""), format!("WatermarkText=\"{NEW}\"")),
        (format!(" Folder=\"{OLD}\""), format!(" Folder=\"{NEW}\"")),
        (format!("S3Prefix=\"{}/\"", OLD.to_lowercase()), format!("S3Prefix=\"{}/\"", NEW.to_lowercase())),
    ] {
        s = s.replace(&a, &b);
    }
    s
}

fn replace_ascii_ci(s: &str, from: &str, to: &str) -> String {
    let (hay, pat) = (s.as_bytes(), from.as_bytes());
    let mut out = String::with_capacity(s.len());
    let (mut i, mut last) = (0, 0);
    while i + pat.len() <= hay.len() {
        if hay[i..i + pat.len()].eq_ignore_ascii_case(pat) && s.is_char_boundary(i) && s.is_char_boundary(i + pat.len()) {
            out.push_str(&s[last..i]);
            out.push_str(to);
            i += pat.len();
            last = i;
        } else {
            i += 1;
        }
    }
    out.push_str(&s[last..]);
    out
}

/// The XML settings files at the top of the data folder.
fn rewrite_files(dir: &Path, pairs: &[(String, String)]) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for p in rd.flatten().map(|e| e.path()).filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("xml"))) {
        let Ok(text) = std::fs::read_to_string(&p) else { continue };
        let new = rewrite_text(&text, pairs);
        if new != text {
            let _ = std::fs::write(&p, new);
        }
    }
}

/// Every string value of HKCU\`key` and its subkeys.
fn rewrite_key(key: &str, pairs: &[(String, String)]) {
    let k = to_wide(key);
    let mut h = HKEY::default();
    if unsafe { RegOpenKeyExW(HKEY_CURRENT_USER, PCWSTR(k.as_ptr()), None, KEY_READ | KEY_SET_VALUE, &mut h) } != ERROR_SUCCESS {
        return;
    }
    let mut changes: Vec<(Vec<u16>, REG_VALUE_TYPE, Vec<u16>)> = Vec::new();
    for i in 0.. {
        let mut name = vec![0u16; 16384];
        let (mut nl, mut ty, mut cb) = (16384u32, 0u32, 0u32);
        let r = unsafe { RegEnumValueW(h, i, Some(PWSTR(name.as_mut_ptr())), &mut nl, None, Some(&mut ty), None, Some(&mut cb)) };
        if r == ERROR_NO_MORE_ITEMS {
            break;
        }
        let ty = REG_VALUE_TYPE(ty);
        if r != ERROR_SUCCESS || (ty != REG_SZ && ty != REG_EXPAND_SZ) {
            continue;
        }
        let mut data = vec![0u8; cb as usize + 2];
        nl = 16384;
        if unsafe { RegEnumValueW(h, i, Some(PWSTR(name.as_mut_ptr())), &mut nl, None, None, Some(data.as_mut_ptr()), Some(&mut cb)) }
            != ERROR_SUCCESS
        {
            continue;
        }
        let w: Vec<u16> = data[..cb as usize].chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).take_while(|&c| c != 0).collect();
        let text = String::from_utf16_lossy(&w);
        let new = rewrite_text(&text, pairs);
        if new != text {
            let mut n = name[..nl as usize].to_vec();
            n.push(0);
            changes.push((n, ty, to_wide(new)));
        }
    }
    for (name, ty, value) in changes {
        let bytes: Vec<u8> = value.iter().flat_map(|c| c.to_le_bytes()).collect();
        unsafe {
            let _ = RegSetValueExW(h, PCWSTR(name.as_ptr()), None, ty, Some(&bytes));
        }
    }
    let mut subs = Vec::new();
    for i in 0.. {
        let mut name = vec![0u16; 256];
        let mut nl = 256u32;
        if unsafe { RegEnumKeyExW(h, i, Some(PWSTR(name.as_mut_ptr())), &mut nl, None, None, None, None) } != ERROR_SUCCESS {
            break;
        }
        subs.push(String::from_utf16_lossy(&name[..nl as usize]));
    }
    unsafe {
        let _ = RegCloseKey(h);
    }
    for s in subs {
        rewrite_key(&format!("{key}\\{s}"), pairs);
    }
}

/// The Run entry of the former name becomes the AnyCapture entry for the capture program next to this one.
fn move_startup_entry() -> bool {
    let (k, o, n) = (to_wide(RUN_KEY), to_wide(OLD), to_wide(NEW));
    unsafe {
        if RegGetValueW(HKEY_CURRENT_USER, PCWSTR(k.as_ptr()), PCWSTR(o.as_ptr()), RRF_RT_REG_SZ, None, None, None) != ERROR_SUCCESS {
            return false;
        }
        let _ = RegDeleteKeyValueW(HKEY_CURRENT_USER, PCWSTR(k.as_ptr()), PCWSTR(o.as_ptr()));
        let exe: PathBuf = crate::paths::exe_dir().join(format!("{NEW}.exe"));
        let v = to_wide(format!("\"{}\" /hide", exe.display()));
        let _ = RegSetKeyValueW(HKEY_CURRENT_USER, PCWSTR(k.as_ptr()), PCWSTR(n.as_ptr()), REG_SZ.0, Some(v.as_ptr() as *const c_void),
                                (v.len() * 2) as u32);
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("anycapture-legacy-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        d
    }

    #[test]
    fn folders_move_and_merge() {
        let base = tmp("move");
        let (old, new) = (base.join(OLD), base.join(NEW));
        std::fs::create_dir_all(old.join("Library").join("Thumbnails")).unwrap();
        std::fs::write(old.join("Presets1.xml"), "old presets").unwrap();
        std::fs::write(old.join("Library").join("a.snap"), "a").unwrap();
        std::fs::write(old.join("Library").join("Thumbnails").join("a.png"), "t").unwrap();
        // the new folder already exists (a log written by a test run): it stays, and everything else moves into it
        std::fs::create_dir_all(new.join("Logs")).unwrap();
        std::fs::write(new.join("Logs").join("AnyCapture.log"), "log").unwrap();
        std::fs::write(new.join("Presets1.xml"), "new presets").unwrap();
        move_merge(&old, &new);
        assert_eq!(std::fs::read_to_string(new.join("Library").join("a.snap")).unwrap(), "a");
        assert_eq!(std::fs::read_to_string(new.join("Library").join("Thumbnails").join("a.png")).unwrap(), "t");
        assert_eq!(std::fs::read_to_string(new.join("Logs").join("AnyCapture.log")).unwrap(), "log");
        assert_eq!(std::fs::read_to_string(new.join("Presets1.xml")).unwrap(), "new presets");
        // the one file the new folder already had is left in the old one; nothing else is
        assert_eq!(std::fs::read_to_string(old.join("Presets1.xml")).unwrap(), "old presets");
        assert!(!old.join("Library").exists());
        // a plain rename when there is no new folder yet
        let (old2, new2) = (base.join("x").join(OLD), base.join("x").join(NEW));
        std::fs::create_dir_all(&old2).unwrap();
        std::fs::write(old2.join("f.txt"), "f").unwrap();
        move_merge(&old2, &new2);
        assert!(!old2.exists() && new2.join("f.txt").exists());
        std::fs::remove_dir_all(&base).unwrap();
    }

    #[test]
    fn paths_and_old_defaults_are_rewritten() {
        let pairs = vec![(format!(r"C:\Users\Ana\Pictures\{OLD}"), format!(r"C:\Users\Ana\Pictures\{NEW}"))];
        let old_lower = OLD.to_lowercase();
        let xml = format!(r#"<P OutputFolder="c:\users\ana\pictures\{old_lower}\2026" WatermarkText="{OLD}" CaptionText="{OLD} rocks"/>"#);
        assert_eq!(rewrite_text(&xml, &pairs),
                   format!(r#"<P OutputFolder="C:\Users\Ana\Pictures\{NEW}\2026" WatermarkText="{NEW}" CaptionText="{OLD} rocks"/>"#));
        let up = format!(r#"<Uploaders S3Prefix="{old_lower}/" Folder="{OLD}"/>"#);
        assert_eq!(rewrite_text(&up, &pairs), format!(r#"<Uploaders S3Prefix="{}/" Folder="{NEW}"/>"#, NEW.to_lowercase()));
        assert_eq!(replace_ascii_ci("çã ABC çã abc", "abc", "x"), "çã x çã x");
    }

    #[test]
    fn the_registry_key_moves() {
        let id = std::process::id();
        let (old, new) = (format!(r"Software\AnyCaptureTest-{id}-{OLD}"), format!(r"Software\AnyCaptureTest-{id}-{NEW}"));
        let set = |key: &str, name: &str, v: &str| {
            let (k, n, w) = (to_wide(key), to_wide(name), to_wide(v));
            unsafe {
                let _ = RegSetKeyValueW(HKEY_CURRENT_USER, PCWSTR(k.as_ptr()), PCWSTR(n.as_ptr()), REG_SZ.0, Some(w.as_ptr() as *const c_void),
                                (w.len() * 2) as u32);
            }
        };
        let get = |key: &str, name: &str| -> Option<String> {
            let (k, n) = (to_wide(key), to_wide(name));
            let mut buf = vec![0u16; 512];
            let mut cb = (buf.len() * 2) as u32;
            let r = unsafe {
                RegGetValueW(HKEY_CURRENT_USER, PCWSTR(k.as_ptr()), PCWSTR(n.as_ptr()), RRF_RT_REG_SZ, None,
                             Some(buf.as_mut_ptr() as *mut c_void), Some(&mut cb))
            };
            (r == ERROR_SUCCESS).then(|| String::from_utf16_lossy(&buf[..cb as usize / 2]).trim_end_matches('\0').to_owned())
        };
        set(&format!(r"{old}\1"), "OutputFolder", &format!(r"D:\Pics\{OLD}"));
        set(&format!(r"{old}\1\Editor"), "BatchFolder", &format!(r"d:\pics\{}\Lote", OLD.to_lowercase()));
        assert!(move_key(&old, &new));
        assert!(!move_key(&old, &new)); // nothing left to move
        rewrite_key(&new, &[(format!(r"D:\Pics\{OLD}"), format!(r"D:\Pics\{NEW}"))]);
        assert_eq!(get(&format!(r"{new}\1"), "OutputFolder").as_deref(), Some(&*format!(r"D:\Pics\{NEW}")));
        assert_eq!(get(&format!(r"{new}\1\Editor"), "BatchFolder").as_deref(), Some(&*format!(r"D:\Pics\{NEW}\Lote")));
        let k = to_wide(&new);
        unsafe {
            let _ = RegDeleteTreeW(HKEY_CURRENT_USER, PCWSTR(k.as_ptr()));
            let _ = RegDeleteKeyW(HKEY_CURRENT_USER, PCWSTR(k.as_ptr()));
        }
    }
}
