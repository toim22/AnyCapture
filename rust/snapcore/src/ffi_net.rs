//! C ABI (snapcore.h) of the uploads (Upload.cpp), the uploaded links, the sign-in, the secrets (Net.cpp) and the AI
//! assistant's request (Assistant.cpp). The dialogs stay in C++ and call these.

use crate::ffi::{SnapWStr, guard, wide_arg, wide_out};
use crate::upload::{Dest, Settings, SignIn};
use crate::wide::{from_ptr, to_string_lossy};
use crate::{ai, net, upload};

/// Uploaders.xml as the C++ Settings struct has it; strings nul-terminated (null = "").
#[repr(C)]
pub struct UploadSettingsC {
    dest: i32,
    s3_endpoint: *const u16,
    s3_region: *const u16,
    s3_bucket: *const u16,
    s3_prefix: *const u16,
    s3_access_key: *const u16,
    s3_public_url: *const u16,
    s3_path_style: bool,
    s3_public_read: bool,
    dropbox_app_key: *const u16,
    google_client_id: *const u16,
    ms_client_id: *const u16,
    folder: *const u16,
    public_link: bool,
    host: *const u16,
    user: *const u16,
    key_file: *const u16,
    http_base: *const u16,
    port: i32,
    custom_json: *const u16,
    copy_link: bool,
}

/// Settings handed out by snapcore_upload_settings_load: the struct first, then the strings it points to.
#[repr(C)]
struct OwnedSettings {
    c: UploadSettingsC,
    strings: Vec<Vec<u16>>,
}

fn text(p: *const u16) -> String {
    unsafe { from_ptr(p) }.map(to_string_lossy).unwrap_or_default()
}

fn settings_in(p: *const UploadSettingsC) -> Settings {
    let Some(c) = (unsafe { p.as_ref() }) else { return Settings::default() };
    Settings {
        dest: Dest::from_i32(c.dest),
        s3_endpoint: text(c.s3_endpoint),
        s3_region: text(c.s3_region),
        s3_bucket: text(c.s3_bucket),
        s3_prefix: text(c.s3_prefix),
        s3_access_key: text(c.s3_access_key),
        s3_public_url: text(c.s3_public_url),
        s3_path_style: c.s3_path_style,
        s3_public_read: c.s3_public_read,
        dropbox_app_key: text(c.dropbox_app_key),
        google_client_id: text(c.google_client_id),
        ms_client_id: text(c.ms_client_id),
        folder: text(c.folder),
        public_link: c.public_link,
        host: text(c.host),
        user: text(c.user),
        key_file: text(c.key_file),
        http_base: text(c.http_base),
        port: c.port,
        custom_json: text(c.custom_json),
        copy_link: c.copy_link,
    }
}

fn wz(s: &str) -> Vec<u16> {
    s.encode_utf16().chain([0]).collect()
}

fn ws(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

/// Uploaders.xml (defaults when it is missing); free with snapcore_upload_settings_free.
#[unsafe(no_mangle)]
pub extern "C" fn snapcore_upload_settings_load() -> *mut UploadSettingsC {
    guard(std::ptr::null_mut(), || {
        let s = Settings::load();
        let strings: Vec<Vec<u16>> = [
            &s.s3_endpoint, &s.s3_region, &s.s3_bucket, &s.s3_prefix, &s.s3_access_key, &s.s3_public_url, &s.dropbox_app_key,
            &s.google_client_id, &s.ms_client_id, &s.folder, &s.host, &s.user, &s.key_file, &s.http_base, &s.custom_json,
        ]
        .iter()
        .map(|t| wz(t))
        .collect();
        let p = |i: usize| strings[i].as_ptr();
        let c = UploadSettingsC {
            dest: s.dest as i32,
            s3_endpoint: p(0),
            s3_region: p(1),
            s3_bucket: p(2),
            s3_prefix: p(3),
            s3_access_key: p(4),
            s3_public_url: p(5),
            s3_path_style: s.s3_path_style,
            s3_public_read: s.s3_public_read,
            dropbox_app_key: p(6),
            google_client_id: p(7),
            ms_client_id: p(8),
            folder: p(9),
            public_link: s.public_link,
            host: p(10),
            user: p(11),
            key_file: p(12),
            http_base: p(13),
            port: s.port,
            custom_json: p(14),
            copy_link: s.copy_link,
        };
        Box::into_raw(Box::new(OwnedSettings { c, strings })) as *mut UploadSettingsC
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_upload_settings_free(p: *mut UploadSettingsC) {
    if !p.is_null() {
        drop(unsafe { Box::from_raw(p as *mut OwnedSettings) });
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_upload_settings_save(s: *const UploadSettingsC) -> bool {
    guard(false, || settings_in(s).save())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_upload_configured(s: *const UploadSettingsC) -> bool {
    guard(false, || settings_in(s).configured())
}

/// An upload's result; each string allocated by snapcore (snapcore_wstr_free).
#[repr(C)]
pub struct UploadResultC {
    ok: bool,
    url: SnapWStr,
    delete_url: SnapWStr,
    thumbnail_url: SnapWStr,
    error: SnapWStr,
}

/// Uploads one file (blocking: call from a worker thread).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_upload_file(s: *const UploadSettingsC, path: *const u16, path_len: usize, remote: *const u16,
                                              remote_len: usize, out: *mut UploadResultC) -> bool {
    guard(false, || {
        let Some(out) = (unsafe { out.as_mut() }) else { return false };
        let r = upload::upload_file(&settings_in(s), &unsafe { wide_arg(path, path_len) },
                                    &to_string_lossy(&unsafe { wide_arg(remote, remote_len) }));
        out.ok = r.ok;
        wide_out(&mut out.url, &ws(&r.url));
        wide_out(&mut out.delete_url, &ws(&r.delete_url));
        wide_out(&mut out.thumbnail_url, &ws(&r.thumbnail_url));
        wide_out(&mut out.error, &ws(&r.error));
        true
    })
}

/// A .sxcu file's text (UTF-8, without a BOM): JSON with a RequestURL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_upload_custom_valid(text: *const u8, len: usize) -> bool {
    guard(false, || !text.is_null() && upload::custom_valid(unsafe { std::slice::from_raw_parts(text, len) }))
}

/// The uploaded link of a library item ("" if none).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_upload_url_get(item: *const u16, len: usize, out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &ws(&upload::uploaded_url(&unsafe { wide_arg(item, len) }))))
}

/// Remembers (or with "" forgets) the uploaded link of a library item.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_upload_url_set(item: *const u16, len: usize, url: *const u16, url_len: usize) {
    guard((), || upload::set_uploaded_url(&unsafe { wide_arg(item, len) }, &to_string_lossy(&unsafe { wide_arg(url, url_len) })))
}

/// Starts a sign-in: the page to open in `out`, or null with the error in `out`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_signin_begin(s: *const UploadSettingsC, out: *mut SnapWStr) -> *mut SignIn {
    guard(std::ptr::null_mut(), || match SignIn::begin(&settings_in(s)) {
        Ok(si) => {
            wide_out(out, &ws(&si.url));
            Box::into_raw(Box::new(si))
        }
        Err(e) => {
            wide_out(out, &ws(&e));
            std::ptr::null_mut()
        }
    })
}

/// True when the browser came back to the loopback address.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_signin_poll(si: *mut SignIn) -> bool {
    guard(false, || unsafe { si.as_mut() }.is_some_and(SignIn::poll))
}

/// Exchanges the code (Dropbox: the one the user pasted) for the refresh token; false with the error in `err`
/// ("" when there is nothing to say).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_signin_finish(si: *mut SignIn, code: *const u16, len: usize, err: *mut SnapWStr) -> bool {
    guard(false, || {
        let Some(si) = (unsafe { si.as_mut() }) else { return false };
        match si.finish(&to_string_lossy(&unsafe { wide_arg(code, len) })) {
            Ok(()) => true,
            Err(e) => {
                wide_out(err, &ws(&e));
                false
            }
        }
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_signin_free(si: *mut SignIn) {
    if !si.is_null() {
        drop(unsafe { Box::from_raw(si) });
    }
}

// ---------------------------------------------------------------- secrets (Credential Manager, "Snapnab/<name>")

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_secret_save(name: *const u16, value: *const u16) -> bool {
    guard(false, || net::save_secret(&text(name), &text(value)))
}

/// The secret ("" if there is none).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_secret_load(name: *const u16, out: *mut SnapWStr) -> bool {
    guard(false, || wide_out(out, &ws(&net::load_secret(&text(name)))))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_secret_delete(name: *const u16) {
    guard((), || net::delete_secret(&text(name)))
}

// ---------------------------------------------------------------- the AI assistant (C22)

/// One question about a PNG image (blocking): true with the answer in `out`, false with the error.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn snapcore_ai_ask(provider: i32, endpoint: *const u16, model: *const u16, key: *const u16, png: *const u8,
                                         png_len: usize, question: *const u16, out: *mut SnapWStr) -> bool {
    guard(false, || {
        let png = if png.is_null() { &[][..] } else { unsafe { std::slice::from_raw_parts(png, png_len) } };
        let (ok, t) = match ai::ask(provider, &text(endpoint), &text(model), &text(key), png, &text(question)) {
            Ok(a) => (true, a),
            Err(e) => (false, e),
        };
        wide_out(out, &ws(&t));
        ok
    })
}
