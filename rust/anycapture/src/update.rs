//! Updates (comp_9): AnyCapture.exe asks GitHub for a newer full release ten seconds after it starts (Options ›
//! Check for updates at start) and when Help › Check for updates is chosen, here or in the Editor (which runs
//! `AnyCapture.exe /checkupdates`). A newer version is offered in a task dialog: Update now downloads its installer
//! (snapcore::update checks the size and SHA-256), closes the Editor, runs the installer with /UPDATE and exits; the
//! installer waits for both programs to end, installs over this copy and starts AnyCapture again (and the Editor, when
//! it was open). Skip this version is remembered in SkippedVersion and only silences the check at start.

use std::ffi::OsStr;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use anycapture_app::tr;
use snapcore::update::{self, Release};
use snapcore::{log, reg};
use windows::Win32::Foundation::{HWND, LPARAM, S_OK, WPARAM};
use windows::Win32::UI::Controls::{
    TASKDIALOG_BUTTON, TASKDIALOG_NOTIFICATIONS, TASKDIALOGCONFIG, TDCBF_CANCEL_BUTTON, TDF_ALLOW_DIALOG_CANCELLATION, TDF_CALLBACK_TIMER,
    TDF_SHOW_MARQUEE_PROGRESS_BAR, TDF_USE_COMMAND_LINKS, TDM_CLICK_BUTTON, TDM_SET_PROGRESS_BAR_MARQUEE, TDN_CREATED, TDN_TIMER,
    TaskDialogIndirect,
};
use windows::Win32::UI::WindowsAndMessaging::{IDOK, MB_ICONINFORMATION, MB_ICONWARNING, PostMessageW, SW_SHOWNORMAL, SendMessageW, WM_COMMAND};
use windows::core::{HRESULT, PCWSTR};

use crate::VERSION;
use crate::dialogs::message;

/// One check (and its dialog) at a time.
static BUSY: AtomicBool = AtomicBool::new(false);
/// A check from the menu came first (AnyCapture started by the Editor's /checkupdates): the one at start is skipped.
static ASKED: AtomicBool = AtomicBool::new(false);

/// Options › Check for updates at start (on by default).
pub fn enabled() -> bool {
    reg::get_dword(OsStr::new("CheckForUpdates"), 1, None) != 0
}

fn skipped() -> String {
    reg::get_string(OsStr::new("SkippedVersion"), None).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default()
}

/// The check at start: on its own thread, ten seconds after the start (the capture window and the tray come first).
/// ANYCAPTURE_NO_UPDATE_CHECK turns it off (the e2e suite sets it).
pub fn check_at_start(owner: isize) {
    if !enabled() || std::env::var_os("ANYCAPTURE_NO_UPDATE_CHECK").is_some() {
        return;
    }
    std::thread::spawn(move || {
        std::thread::sleep(std::time::Duration::from_secs(10));
        if ASKED.load(Ordering::SeqCst) || BUSY.swap(true, Ordering::SeqCst) {
            return;
        }
        match update::latest(VERSION) {
            Ok(Some(rel)) if rel.version != skipped() => {
                log::write(&format!("Update: {} is available", rel.version));
                offer(owner, &rel);
            }
            Ok(Some(rel)) => log::write(&format!("Update: {} is available (skipped by the user)", rel.version)),
            Ok(None) => log::write("Update: this is the latest version"),
            Err(e) => log::write(&format!("Update: the check failed: {e}")),
        }
        BUSY.store(false, Ordering::SeqCst);
    });
}

/// Help › Check for updates: always answers (latest version, the error, or the offer).
pub fn check_now(owner: isize) {
    ASKED.store(true, Ordering::SeqCst);
    std::thread::spawn(move || {
        if BUSY.swap(true, Ordering::SeqCst) {
            return;
        }
        match update::latest(VERSION) {
            Ok(Some(rel)) => offer(owner, &rel),
            Ok(None) => {
                message(owner, &tr("Você já está usando a versão mais recente do AnyCapture (%s).").replace("%s", VERSION), "AnyCapture",
                        MB_ICONINFORMATION);
            }
            Err(e) => {
                log::write(&format!("Update: the check failed: {e}"));
                message(owner, &(tr("Não foi possível verificar se há atualizações. Verifique a conexão com a internet.") + "\n\n" + &e),
                        "AnyCapture", MB_ICONWARNING);
            }
        }
        BUSY.store(false, Ordering::SeqCst);
    });
}

fn w(s: &str) -> Vec<u16> {
    snapcore::wide::to_wide(s)
}

/// The new version and what to do with it.
fn offer(owner: isize, rel: &Release) {
    const UPDATE: i32 = 100;
    const NOTES: i32 = 101;
    const SKIP: i32 = 102;
    let main = w(&tr("O AnyCapture %s está disponível").replace("%s", &rel.version));
    let content = w(&tr("Você está usando a versão %s.").replace("%s", VERSION));
    // the release notes, as written on GitHub, under "See details"
    let mut notes: String = rel.notes.replace('\r', "").chars().take(2000).collect();
    if notes.len() < rel.notes.replace('\r', "").len() {
        notes += "…";
    }
    let notes = w(&notes);
    let (more, less) = (w(&tr("Mostrar as novidades")), w(&tr("Ocultar as novidades")));
    let title = w("AnyCapture");
    let texts = [
        (UPDATE, w(&tr("Atualizar agora\nBaixa o instalador, fecha o AnyCapture e o Editor e instala a nova versão."))),
        (NOTES, w(&tr("Ver a página da versão\nAbre a versão no GitHub, com as novidades e os arquivos."))),
        (SKIP, w(&tr("Pular esta versão\nNão avisa mais desta versão ao iniciar."))),
    ];
    let btns: Vec<TASKDIALOG_BUTTON> = texts
        .iter()
        .filter(|(id, _)| *id != UPDATE || !rel.setup_url.is_empty())
        .map(|(id, t)| TASKDIALOG_BUTTON { nButtonID: *id, pszButtonText: PCWSTR(t.as_ptr()) })
        .collect();
    let tc = TASKDIALOGCONFIG {
        cbSize: std::mem::size_of::<TASKDIALOGCONFIG>() as u32,
        hwndParent: HWND(owner as _),
        dwFlags: TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION,
        pszWindowTitle: PCWSTR(title.as_ptr()),
        pszMainInstruction: PCWSTR(main.as_ptr()),
        pszContent: PCWSTR(content.as_ptr()),
        pszExpandedInformation: if rel.notes.trim().is_empty() { PCWSTR::null() } else { PCWSTR(notes.as_ptr()) },
        pszExpandedControlText: PCWSTR(less.as_ptr()),
        pszCollapsedControlText: PCWSTR(more.as_ptr()),
        cButtons: btns.len() as u32,
        pButtons: btns.as_ptr(),
        dwCommonButtons: TDCBF_CANCEL_BUTTON,
        ..Default::default()
    };
    let mut btn = 0;
    if unsafe { TaskDialogIndirect(&tc, Some(&mut btn), None, None) }.is_err() {
        return;
    }
    match btn {
        UPDATE => install(owner, rel),
        NOTES => crate::shell_open(&rel.page),
        SKIP => {
            reg::set_string(OsStr::new("SkippedVersion"), OsStr::new(&rel.version), None);
            log::write(&format!("Update: {} skipped", rel.version));
        }
        _ => {}
    }
}

type Outcome = Arc<Mutex<Option<Result<std::path::PathBuf, String>>>>;

/// The download behind a dialog with a moving bar; None when it was cancelled.
fn download(owner: isize, rel: &Release) -> Option<Result<std::path::PathBuf, String>> {
    let done: Outcome = Arc::new(Mutex::new(None));
    {
        let (done, rel) = (done.clone(), rel.clone());
        std::thread::spawn(move || {
            let r = update::download(&rel, VERSION);
            *done.lock().unwrap() = Some(r);
        });
    }
    unsafe extern "system" fn cb(h: HWND, n: TASKDIALOG_NOTIFICATIONS, _w: WPARAM, _l: LPARAM, data: isize) -> HRESULT {
        let done = unsafe { &*(data as *const Mutex<Option<Result<std::path::PathBuf, String>>>) };
        unsafe {
            if n == TDN_CREATED {
                SendMessageW(h, TDM_SET_PROGRESS_BAR_MARQUEE.0 as u32, Some(WPARAM(1)), Some(LPARAM(0)));
            }
            if n == TDN_TIMER && done.lock().map(|d| d.is_some()).unwrap_or(false) {
                SendMessageW(h, TDM_CLICK_BUTTON.0 as u32, Some(WPARAM(IDOK.0 as usize)), Some(LPARAM(0)));
            }
        }
        S_OK
    }
    let (title, main, content) = (w("AnyCapture"), w(&tr("Baixando o AnyCapture %s…").replace("%s", &rel.version)),
                                  w(&tr("A instalação começa assim que o download terminar.")));
    let tc = TASKDIALOGCONFIG {
        cbSize: std::mem::size_of::<TASKDIALOGCONFIG>() as u32,
        hwndParent: HWND(owner as _),
        dwFlags: TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SHOW_MARQUEE_PROGRESS_BAR,
        pszWindowTitle: PCWSTR(title.as_ptr()),
        pszMainInstruction: PCWSTR(main.as_ptr()),
        pszContent: PCWSTR(content.as_ptr()),
        dwCommonButtons: TDCBF_CANCEL_BUTTON,
        pfCallback: Some(cb),
        lpCallbackData: Arc::as_ptr(&done) as isize,
        ..Default::default()
    };
    let mut btn = 0;
    let _ = unsafe { TaskDialogIndirect(&tc, Some(&mut btn), None, None) };
    if btn != IDOK.0 {
        log::write("Update: the download was cancelled");
        return None;
    }
    done.lock().ok()?.take()
}

/// Update now: downloads, closes the Editor, runs the installer and exits (the installer starts AnyCapture again).
fn install(owner: isize, rel: &Release) {
    let setup = match download(owner, rel) {
        None => return,
        Some(Ok(p)) => p,
        Some(Err(e)) => {
            log::write(&format!("Update: the download failed: {e}"));
            message(owner, &(tr("Não foi possível baixar a atualização.") + "\n\n" + &e), "AnyCapture", MB_ICONWARNING);
            return;
        }
    };
    log::write(&format!("Update: running {}", setup.display()));
    let editor_open = crate::dialogs::close_editor_and_wait();
    let args = if editor_open { "/SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE /EDITOR" } else { "/SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE" };
    let (file, params) = (w(&setup.to_string_lossy()), w(args));
    let r = unsafe {
        windows::Win32::UI::Shell::ShellExecuteW(None, windows::core::w!("open"), PCWSTR(file.as_ptr()), PCWSTR(params.as_ptr()),
                                                 PCWSTR::null(), SW_SHOWNORMAL)
    };
    if r.0 as isize <= 32 {
        // the administrator prompt was declined, or the file was blocked
        log::write(&format!("Update: the installer did not start ({})", r.0 as isize));
        if editor_open {
            snapcore::instance::launch_sibling("AnyCaptureEditor.exe", "", false);
        }
        message(owner, &tr("O instalador não foi iniciado. Você pode instalar a nova versão pela página da versão no GitHub."), "AnyCapture",
                MB_ICONWARNING);
        return;
    }
    unsafe {
        let _ = PostMessageW(Some(HWND(owner as _)), WM_COMMAND, WPARAM(crate::id::EXIT as usize), LPARAM(0));
    }
}
