//! The update check (comp_9: Snagit's updater lives in the capture program, checks after the start, downloads the new
//! installer, checks its hash and runs it), against GitHub Releases instead of TechSmith's server.
//!
//! `latest` asks GitHub for the repository's latest release: GitHub leaves drafts and pre-releases out of that answer,
//! so only a full release is offered (none exists yet: the check then finds nothing). Its installer is the asset named
//! `AnyCapture-<version>-setup.exe`; `download` saves it to %TEMP%\AnyCapture\Update and checks the SHA-256 GitHub
//! publishes for it. AnyCapture.exe asks the user and runs it with /UPDATE (packaging/installer/AnyCapture.iss).

use std::path::PathBuf;

use crate::net::{self, Json};
use crate::project::REPO;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Release {
    /// "0.6.0" (the tag without its "v")
    pub version: String,
    /// the release notes (Markdown, as written on GitHub)
    pub notes: String,
    /// the release's page on GitHub
    pub page: String,
    /// the installer; empty when the release has none (the page is offered instead)
    pub setup_url: String,
    pub setup_name: String,
    pub size: u64,
    /// lowercase hex; empty when GitHub gave none
    pub sha256: String,
}

/// The numbers of a version ("v0.10.2-beta" → [0, 10, 2]) and whether it has a pre-release part.
fn parts(v: &str) -> (Vec<u64>, bool) {
    let v = v.trim().trim_start_matches(['v', 'V']);
    let core = v.split(['-', '+']).next().unwrap_or("");
    let nums = core.split('.').map(|p| p.trim().parse::<u64>().unwrap_or(0)).collect();
    (nums, v.contains('-'))
}

/// True when version `a` comes after version `b` (numbers compared one by one, 0.10 after 0.9; a version without a
/// pre-release part after the same numbers with one).
pub fn is_newer(a: &str, b: &str) -> bool {
    let ((mut x, xp), (mut y, yp)) = (parts(a), parts(b));
    let n = x.len().max(y.len());
    x.resize(n, 0);
    y.resize(n, 0);
    match x.cmp(&y) {
        std::cmp::Ordering::Greater => true,
        std::cmp::Ordering::Less => false,
        std::cmp::Ordering::Equal => yp && !xp,
    }
}

fn headers(version: &str) -> Vec<String> {
    vec![
        "Accept: application/vnd.github+json".into(),
        "X-GitHub-Api-Version: 2022-11-28".into(),
        format!("User-Agent: AnyCapture/{version}"),
    ]
}

/// Reads a release from GitHub's answer.
pub fn parse_release(body: &[u8]) -> Option<Release> {
    let j = Json::parse(body)?;
    let tag = j.get("tag_name")?.text();
    let mut r = Release {
        version: tag.trim_start_matches(['v', 'V']).to_owned(),
        notes: j.get("body").map(|b| b.text()).unwrap_or_default(),
        page: j.get("html_url").map(|b| b.text()).unwrap_or_default(),
        ..Default::default()
    };
    if let Some(Json::Array(assets)) = j.get("assets") {
        let setup = assets.iter().find(|a| {
            let n = a.get("name").map(|n| n.text().to_lowercase()).unwrap_or_default();
            n.starts_with("anycapture") && n.ends_with("-setup.exe")
        });
        if let Some(a) = setup {
            r.setup_name = a.get("name").map(|n| n.text()).unwrap_or_default();
            r.setup_url = a.get("browser_download_url").map(|n| n.text()).unwrap_or_default();
            r.size = a.get("size").and_then(|s| s.text().parse::<f64>().ok()).unwrap_or(0.0) as u64;
            r.sha256 = a.get("digest").map(|d| d.text()).and_then(|d| d.strip_prefix("sha256:").map(str::to_lowercase)).unwrap_or_default();
        }
    }
    (!r.version.is_empty()).then_some(r)
}

/// The latest full release when it is newer than `current`; None when this version is the latest (or no full release
/// exists yet). Err with the reason when GitHub could not be asked.
pub fn latest(current: &str) -> Result<Option<Release>, String> {
    let url = format!("https://api.github.com/repos/{REPO}/releases/latest");
    let r = net::request("GET", &url, &headers(current), &[], 20_000);
    if r.status == 404 {
        return Ok(None); // no full release yet
    }
    if !r.ok() {
        return Err(if r.status == 0 { r.error } else { format!("HTTP {}", r.status) });
    }
    let rel = parse_release(&r.body).ok_or_else(|| "unexpected answer from GitHub".to_owned())?;
    Ok(is_newer(&rel.version, current).then_some(rel))
}

/// Downloads the release's installer to %TEMP%\AnyCapture\Update and checks its size and SHA-256.
pub fn download(rel: &Release, current: &str) -> Result<PathBuf, String> {
    if rel.setup_url.is_empty() {
        return Err("the release has no installer".into());
    }
    let r = net::request("GET", &rel.setup_url, &[format!("User-Agent: AnyCapture/{current}")], &[], 300_000);
    if !r.ok() {
        return Err(if r.status == 0 { r.error } else { format!("HTTP {}", r.status) });
    }
    if rel.size > 0 && r.body.len() as u64 != rel.size {
        return Err(format!("the download is incomplete ({} of {} bytes)", r.body.len(), rel.size));
    }
    if !rel.sha256.is_empty() && net::hex(&net::sha256(&r.body)) != rel.sha256 {
        return Err("the downloaded installer does not match its SHA-256".into());
    }
    let dir = std::env::temp_dir().join("AnyCapture").join("Update");
    let _ = std::fs::create_dir_all(&dir);
    let name = if rel.setup_name.is_empty() { format!("AnyCapture-{}-setup.exe", rel.version) } else { rel.setup_name.clone() };
    let path = dir.join(name);
    std::fs::write(&path, &r.body).map_err(|e| e.to_string())?;
    Ok(path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn compares_versions() {
        assert!(is_newer("0.6.0", "0.5.0"));
        assert!(is_newer("v0.10.0", "0.9.9"));
        assert!(is_newer("1.0", "0.99.1"));
        assert!(is_newer("0.5.0", "0.5.0-beta"));
        assert!(!is_newer("0.5.0", "0.5.0"));
        assert!(!is_newer("0.5.0-beta", "0.5.0"));
        assert!(!is_newer("0.4.9", "0.5.0"));
        assert!(!is_newer("0.5", "0.5.0"));
    }

    #[test]
    fn reads_a_release() {
        let body = br#"{"tag_name":"v0.6.0","html_url":"https://github.com/toim22/AnyCapture/releases/tag/v0.6.0",
            "prerelease":false,"body":"- Faster\n- Smaller",
            "assets":[{"name":"AnyCapture-0.6.0-win-x64.zip","browser_download_url":"https://x/zip","size":10},
                      {"name":"AnyCapture-0.6.0-setup.exe","browser_download_url":"https://x/setup","size":12600174,
                       "digest":"sha256:E4D770DD80A5E50B6718881A5EFA16CFC7D7C82B7A2B311A04F1046FB0183210"}]}"#;
        let r = parse_release(body).unwrap();
        assert_eq!(r.version, "0.6.0");
        assert_eq!((r.setup_name.as_str(), r.setup_url.as_str(), r.size), ("AnyCapture-0.6.0-setup.exe", "https://x/setup", 12600174));
        assert_eq!(r.sha256, "e4d770dd80a5e50b6718881a5efa16cfc7d7c82b7a2b311a04f1046fb0183210");
        assert_eq!(r.notes, "- Faster\n- Smaller");
        // no installer attached: the page is offered instead
        let r = parse_release(br#"{"tag_name":"0.7.0","html_url":"p","assets":[]}"#).unwrap();
        assert!(r.setup_url.is_empty() && r.page == "p");
    }
}
