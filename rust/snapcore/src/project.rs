//! Where AnyCapture lives on GitHub: the issue forms for reporting a problem or suggesting something (opened in the
//! browser, filled in with the program's version and Windows'), and the releases the update check reads (`update.rs`).

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

/// The "Report a problem" form (.github/ISSUE_TEMPLATE/bug_report.yml) with the version fields filled in.
pub fn report_bug_url(app_version: &str) -> String {
    format!("{}/issues/new?template=bug_report.yml&version={}&windows={}", home(), encode(app_version), encode(&windows_version()))
}

/// The "Suggest an improvement or send feedback" form (.github/ISSUE_TEMPLATE/feedback.yml).
pub fn feedback_url(app_version: &str) -> String {
    format!("{}/issues/new?template=feedback.yml&version={}", home(), encode(app_version))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_forms_carry_the_versions() {
        let u = report_bug_url("0.5.0");
        assert!(u.starts_with("https://github.com/toim22/AnyCapture/issues/new?template=bug_report.yml&version=0.5.0&windows=Windows%20"), "{u}");
        assert!(feedback_url("0.5.0").ends_with("template=feedback.yml&version=0.5.0"));
        assert!(windows_version().starts_with("Windows 1"));
    }
}
