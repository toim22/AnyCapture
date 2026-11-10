//! Where AnyCapture lives on GitHub: the issues sent from Help › Report a problem and Send a suggestion or feedback
//! (posted to the issue relay, services/issue-relay, which needs no GitHub account; GitHub's form in the browser
//! when the relay cannot be reached),
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

    /// the report's kind for the relay
    fn relay_kind(self) -> &'static str {
        match self {
            IssueKind::Bug => "bug",
            IssueKind::Idea => "idea",
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

/// At most `max` bytes of `s`, cut on a character boundary (so also at most `max` UTF-16 units, as the relay counts).
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

/// GitHub's "New issue" page with the issue form filled in (the field ids of .github/ISSUE_TEMPLATE); long texts are
/// cut so the address stays under the length browsers and GitHub accept. It needs a GitHub account: the way out when
/// the relay cannot be reached.
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

/// The relay that files the issues for people without a GitHub account: services/issue-relay, a Cloudflare Worker
/// that creates them as the AnyCapture Reports GitHub App. Empty until it is deployed (then the browser form is
/// used); the environment variable ANYCAPTURE_ISSUE_RELAY points elsewhere (a local `cf dev`, a test).
pub const RELAY_URL: &str = "";

fn relay_url() -> String {
    std::env::var("ANYCAPTURE_ISSUE_RELAY").ok().filter(|s| !s.trim().is_empty()).unwrap_or_else(|| RELAY_URL.to_owned())
}

/// The report as the relay reads it (services/issue-relay/src/report.ts), cut to its limits.
pub fn relay_body(kind: IssueKind, i: &Issue) -> String {
    use crate::net::Json;
    Json::obj()
        .set("kind", Json::str(kind.relay_kind()))
        .set("title", Json::str(clip(i.title.trim(), 200)))
        .set("text", Json::str(clip(i.text.trim(), 8000)))
        .set("more", Json::str(clip(i.more.trim(), 4000)))
        .set("version", Json::str(clip(&i.app_version, 32)))
        .set("program", Json::str(clip(&i.program, 40)))
        .set("system", Json::str(if i.system { clip(&system_info(), 160).to_owned() } else { String::new() }))
        .dump()
}

/// What became of the issue.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Sent {
    /// created on GitHub: the issue's address
    Created(String),
    /// the relay was not reachable, so the browser opened GitHub's filled-in form, which the user sends
    Browser,
    /// the relay asks to wait (several reports in a minute from this address)
    Later,
    /// neither the relay nor the browser: the address of the issues, to tell the user
    Failed(String),
}

/// Posts the report to the relay: the issue's address, or why not (for the log).
fn send_to_relay(base: &str, kind: IssueKind, i: &Issue) -> Result<String, (i32, String)> {
    let r = crate::net::request("POST", &format!("{}/issues", base.trim_end_matches('/')), &["Content-Type: application/json".to_owned()],
                                relay_body(kind, i).as_bytes(), 20000);
    if r.ok() {
        let url = crate::net::json_field(&r.body, "url");
        return Ok(if url.starts_with("https://github.com/") { url } else { format!("{}/issues", home()) });
    }
    Err((r.status, if r.status == 0 { r.error } else { r.text() }))
}

/// Sends the issue (it takes a few seconds: call it from a thread of its own). Through the relay, the issue is created
/// at once, with its label, and nobody needs a GitHub account; when the relay cannot be reached, the browser opens
/// GitHub's issue form filled in, as the last way out.
pub fn send_issue(kind: IssueKind, i: &Issue) -> Sent {
    let relay = relay_url();
    if !relay.is_empty() {
        match send_to_relay(&relay, kind, i) {
            Ok(url) => return Sent::Created(url),
            Err((429, _)) => return Sent::Later,
            Err((status, e)) => crate::log::write(&format!("Issue: the relay did not take it ({status} {e}); opening the browser")),
        }
    }
    if open_url(&new_issue_url(kind, i)) { Sent::Browser } else { Sent::Failed(format!("{}/issues", home())) }
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_report_goes_out_as_the_relay_and_the_form_read_it() {
        let i = Issue { title: "Crash".into(), text: "It \"closed\"\n".into(), more: "1. Open".into(), app_version: "0.5.0".into(),
                        program: "AnyCapture".into(), system: true };
        let b = relay_body(IssueKind::Bug, &i);
        assert!(b.starts_with(r#"{"kind":"bug","title":"Crash","text":"It \"closed\"","more":"1. Open","version":"0.5.0","program":"AnyCapture","system":"Windows 1"#), "{b}");
        let idea = Issue { title: "Dark mode".into(), text: "Please".into(), app_version: "0.5.0".into(), program: "AnyCapture Editor".into(),
                           ..Default::default() };
        assert!(relay_body(IssueKind::Idea, &idea).ends_with(r#""program":"AnyCapture Editor","system":""}"#));
        let u = new_issue_url(IssueKind::Bug, &i);
        assert!(u.starts_with("https://github.com/toim22/AnyCapture/issues/new?template=bug_report.yml&title=Crash&what=It%20%22closed%22&steps=1.%20Open&version=0.5.0&windows=Windows%20"), "{u}");
        assert!(new_issue_url(IssueKind::Idea, &idea).ends_with("template=feedback.yml&title=Dark%20mode&idea=Please&why=&version=0.5.0"));
        assert_eq!((IssueKind::Bug.label(), IssueKind::Idea.label()), ("bug", "enhancement"));
        assert_eq!(clip("ação", 2), "a");
        assert!(windows_version().starts_with("Windows 1"));
    }
}
