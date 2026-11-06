// Optional upload and share link (C21; ShareX dossier 01 · C12 uploader framework, C13 destinations, C14 OAuth, C21
// custom uploader .sxcu). Off by default: nothing is sent until the user picks a destination. Destinations: S3-compatible
// storage (AWS Signature V4), Dropbox, Google Drive and OneDrive (OAuth 2.0 with PKCE, with the user's own app key /
// client id — "bring your own app"), FTP, SFTP (the OpenSSH client of Windows, key authentication) and a custom HTTP
// uploader (request template compatible with ShareX .sxcu). Settings in Uploaders.xml; passwords, secrets and tokens
// in the Windows Credential Manager ("Snapnab/…"), never in files. Images are uploaded without metadata unless the
// user turns that off (StripMetadataOnUpload, C29). The work is done by snapcore (rust/snapcore/src/upload.rs).
#pragma once
#include "Image.h"

namespace snap::upload {

enum class Dest { None = 0, S3, Dropbox, GoogleDrive, OneDrive, Ftp, Sftp, Custom, Count };
const wchar_t* DestName(Dest d);

struct Settings {
    Dest dest = Dest::None;
    // S3 and compatible (Wasabi, Backblaze B2 S3, DigitalOcean Spaces, MinIO, Cloudflare R2…)
    std::wstring s3Endpoint = L"s3.amazonaws.com", s3Region = L"us-east-1", s3Bucket, s3Prefix = L"snapnab/", s3AccessKey, s3PublicUrl;
    bool s3PathStyle = false, s3PublicRead = true;
    // OAuth apps of the user
    std::wstring dropboxAppKey, googleClientId, msClientId;
    std::wstring folder = L"Snapnab";            // remote folder (Dropbox, Drive, OneDrive, FTP, SFTP)
    bool publicLink = true;                       // make the file readable by anyone with the link
    // FTP / SFTP
    std::wstring host, user, keyFile, httpBase;  // httpBase: public web address of the folder (for the link)
    int port = 0;                                 // 0 = 21 (FTP) / 22 (SFTP)
    // custom uploader (.sxcu JSON)
    std::wstring customJson;
    bool copyLink = true;                         // the link replaces the image on the clipboard
    static Settings Load();
    void Save() const;
    bool Configured() const;
};

struct Result {
    bool ok = false;
    std::wstring url, deleteUrl, thumbnailUrl, error;
};

// uploads one file (blocking: call from a worker thread); 'remoteName' = name in the destination
Result UploadFile(const Settings& s, const std::wstring& path, const std::wstring& remoteName);
// encodes the image (PNG, without metadata when StripMetadataOnUpload) and uploads it in the background; 'done' runs
// on the calling (UI) thread
void UploadImageAsync(const Image& img, const std::wstring& name, std::function<void(const Result&)> done);
void UploadFileAsync(const std::wstring& path, std::function<void(const Result&)> done);

// settings dialogs: the destination, its fields and the sign-in (OAuth) when needed
bool ConfigureDialog(HWND owner);

// links of uploaded captures (Copy as › Markdown / link): Library\Uploads.xml, by file name
std::wstring UploadedUrl(const std::wstring& itemPath);
void SetUploadedUrl(const std::wstring& itemPath, const std::wstring& url);

} // namespace snap::upload
