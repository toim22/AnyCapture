//! Where AnyCapture lives on GitHub: the issues sent from Help › Report a problem and Send a suggestion or feedback
//! (created with the GitHub CLI when it is signed in, otherwise GitHub's issue form opens in the browser filled in),
//! and the releases the update check reads (`update.rs`).

use crate::net::url_encode;

/// owner/name of the repository
pub const REPO: &str = "toim22/AnyCapture";

pub fn home() -> String {
    format!("https://github.com/{REPO}")
}

/// "Windows 11 25H2 (build 26200.1234)", from the registry (the version the user sees in Settings › About).
pub fn windows_version() -> String {
    use windows::Win32::System::Registry::{HKEY_LOCAL_MACHINE, RRF_RT_REG_DWORD, RRF_RT_REG_SZ, RegGetValueW};
    use windows::core::w;
    let key = w!(r"SOFTWARE\Microsoft\Windows NT\CurrentVersion");
    let text = |name: windows::core::PCWSTR| -> String {
        let mut buf = [0u16; 64];
        let mut len = (buf.len() * 2) as u32;
        let ok = unsafe { RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, None, Some(buf.as_mut_ptr().cast()), Some(&mut len)) }.is_ok();
        if ok { String::from_utf16_lossy(&buf[..(len as usize / 2).saturating_sub(1)]) } else { String::new() }
    };
    let build = text(w!("CurrentBuildNumber"));
    let mut ubr = 0u32;
    let mut len = 4u32;
    let has_ubr = unsafe {
        RegGetValueW(HKEY_LOCAL_MACHINE, key, w!("UBR"), RRF_RT_REG_DWORD, None, Some((&mut ubr as *mut u32).cast()), Some(&mut len))
    }
    .is_ok();
    let name = if build.parse::<u32>().unwrap_or(0) >= 22000 { "Windows 11" } else { "Windows 10" };
    let release = text(w!("DisplayVersion"));
    let build = if has_ubr { format!("{build}.{ubr}") } else { build };
    let mut out = name.to_owned();
    if !release.is_empty() {
        out += &format!(" {release}");
    }
    if !build.is_empty() {
        out += &format!(" (build {build})");
    }
    out
}

fn encode(s: &str) -> String {
    url_encode(s.as_bytes(), false)
}

/// What Help sends: a problem (the `bug` label, the bug_report.yml form) or a suggestion (`enhancement`, feedback.yml).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IssueKind {
    Bug,
    Idea,
}

impl IssueKind {
    /// the GitHub label of the issue
    pub fn label(self) -> &'static str {
        match self {
            IssueKind::Bug => "bug",
            IssueKind::Idea => "enhancement",
        }
    }

    /// the issue form of .github/ISSUE_TEMPLATE (its label goes on the issue for anyone, not only for collaborators)
    fn template(self) -> &'static str {
        match self {
            IssueKind::Bug => "bug_report.yml",
            IssueKind::Idea => "feedback.yml",
        }
    }
}

/// What the form of Help › Report a problem / Send a suggestion collects.
#[derive(Clone, Debug, Default)]
pub struct Issue {
    pub title: String,
    /// what happened, or the suggestion
    pub text: String,
    /// the steps to reproduce, or why it would help (optional)
    pub more: String,
    pub app_version: String,
    /// the program it is sent from ("AnyCapture", "AnyCapture Editor")
    pub program: String,
    /// with the Windows version and the interface language
    pub system: bool,
}

/// The Windows version and the interface language, as the form shows them before sending.
pub fn system_info() -> String {
    format!("{}, {}", windows_version(), crate::i18n::current())
}

/// At most `max` bytes of `s`, cut on a character boundary.
fn clip(s: &str, max: usize) -> &str {
    if s.len() <= max {
        return s;
    }
    let mut n = max;
    while !s.is_char_boundary(n) {
        n -= 1;
    }
    &s[..n]
}

/// The issue's text in GitHub Markdown, with the headings of the issue form (the same issue whichever way it is sent).
pub fn issue_body(kind: IssueKind, i: &Issue) -> String {
    let bug = kind == IssueKind::Bug;
    let mut b = String::new();
    let mut section = |title: &str, text: &str| {
        if !text.trim().is_empty() {
            b += &format!("### {title}\n\n{}\n\n", text.trim());
        }
    };
    section(if bug { "What happened?" } else { "Your suggestion or feedback" }, &i.text);
    section(if bug { "Steps to reproduce" } else { "Why would it help?" }, &i.more);
    section("AnyCapture version", &i.app_version);
    if i.system {
        section("Windows version", &system_info());
    }
    let menu = if bug { "Help › Report a problem" } else { "Help › Send a suggestion or feedback" };
    b + &format!("_Sent from {} ({menu})._\n", i.program)
}

/// GitHub's "New issue" page with the issue form filled in (the field ids of .github/ISSUE_TEMPLATE); long texts are
/// cut so the address stays under the length browsers and GitHub accept.
pub fn new_issue_url(kind: IssueKind, i: &Issue) -> String {
    let (text_id, more_id) = if kind == IssueKind::Bug { ("what", "steps") } else { ("idea", "why") };
    let mut u = format!("{}/issues/new?template={}&title={}&{text_id}={}&{more_id}={}&version={}", home(), kind.template(),
                        encode(clip(i.title.trim(), 300)), encode(clip(i.text.trim(), 3000)), encode(clip(i.more.trim(), 1500)),
                        encode(&i.app_version));
    if i.system && kind == IssueKind::Bug {
        u += &format!("&windows={}", encode(&system_info()));
    }
    u
}

/// What became of the issue.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Sent {
    /// created by the GitHub CLI: the issue's address
    Created(String),
    /// the browser opened the filled-in form, which the user sends
    Browser,
    /// not even the browser opened: the address of the issues, to tell the user
    Failed(String),
}

/// Runs the GitHub CLI without a console window; None when it is not installed.
fn gh(args: &[&str]) -> Option<std::process::Output> {
    use std::os::windows::process::CommandExt;
    use windows::Win32::System::Threading::CREATE_NO_WINDOW;
    std::process::Command::new("gh").args(args).creation_flags(CREATE_NO_WINDOW.0).output().ok()
}

/// Creates the issue with the GitHub CLI: None when it is missing or not signed in.
fn create_with_gh(label: &str, title: &str, body: &str) -> Option<Result<String, String>> {
    if !gh(&["auth", "status", "--hostname", "github.com"])?.status.success() {
        return None;
    }
    let mut out = gh(&["issue", "create", "--repo", REPO, "--title", title, "--body", body, "--label", label])?;
    if !out.status.success() {
        // the label may be missing from the repository: once more without it
        out = gh(&["issue", "create", "--repo", REPO, "--title", title, "--body", body])?;
    }
    if out.status.success() {
        let text = String::from_utf8_lossy(&out.stdout);
        let url = text.lines().rev().find(|l| l.trim().starts_with("https://")).unwrap_or_default().trim().to_owned();
        Some(Ok(if url.is_empty() { format!("{}/issues", home()) } else { url }))
    } else {
        Some(Err(String::from_utf8_lossy(&out.stderr).trim().to_owned()))
    }
}

/// Opens an address in the default browser.
pub fn open_url(url: &str) -> bool {
    use windows::Win32::UI::Shell::ShellExecuteW;
    use windows::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL;
    use windows::core::{PCWSTR, w};
    let u = crate::wide::to_wide(url);
    let h = unsafe { ShellExecuteW(None, w!("open"), PCWSTR(u.as_ptr()), PCWSTR::null(), PCWSTR::null(), SW_SHOWNORMAL) };
    h.0 as isize > 32
}

/// Sends the issue (it takes a few seconds: call it from a thread of its own). With the GitHub CLI signed in on this
/// computer the issue is created at once, with its label; otherwise the browser opens GitHub's issue form with
/// everything filled in, and the user only signs in and clicks "Create" (GitHub creates issues only for an account, and
/// the program carries no key of its own).
pub fn send_issue(kind: IssueKind, i: &Issue) -> Sent {
    let title = clip(i.title.trim(), 250).to_owned();
    match create_with_gh(kind.label(), &title, clip(&issue_body(kind, i), 30000)) {
        Some(Ok(url)) => return Sent::Created(url),
        Some(Err(e)) => crate::log::write(&format!("Issue: the GitHub CLI could not create it ({e}); opening the browser")),
        None => {}
    }
    if open_url(&new_issue_url(kind, i)) { Sent::Browser } else { Sent::Failed(format!("{}/issues", home())) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_issue_has_the_form_fields() {
        let i = Issue { title: "Crash".into(), text: "It closed".into(), more: "1. Open".into(), app_version: "0.5.0".into(),
                        program: "AnyCapture".into(), system: true };
        let b = issue_body(IssueKind::Bug, &i);
        assert!(b.starts_with("### What happened?\n\nIt closed\n\n### Steps to reproduce\n\n1. Open\n\n### AnyCapture version\n\n0.5.0"), "{b}");
        assert!(b.contains("### Windows version\n\nWindows 1"), "{b}");
        assert!(b.ends_with("_Sent from AnyCapture (Help › Report a problem)._\n"), "{b}");
        let u = new_issue_url(IssueKind::Bug, &i);
        assert!(u.starts_with("https://github.com/toim22/AnyCapture/issues/new?template=bug_report.yml&title=Crash&what=It%20closed&steps=1.%20Open&version=0.5.0&windows=Windows%20"), "{u}");
        let idea = Issue { title: "Dark mode".into(), text: "Please".into(), app_version: "0.5.0".into(), program: "AnyCapture Editor".into(),
                           ..Default::default() };
        let b = issue_body(IssueKind::Idea, &idea);
        assert!(b.starts_with("### Your suggestion or feedback\n\nPlease\n\n### AnyCapture version"), "{b}");
        assert!(!b.contains("Why would it help") && !b.contains("Windows version"), "{b}");
        assert!(new_issue_url(IssueKind::Idea, &idea).ends_with("template=feedback.yml&title=Dark%20mode&idea=Please&why=&version=0.5.0"));
        assert_eq!((IssueKind::Bug.label(), IssueKind::Idea.label()), ("bug", "enhancement"));
        assert_eq!(clip("ação", 2), "a");
        assert!(windows_version().starts_with("Windows 1"));
    }
}
