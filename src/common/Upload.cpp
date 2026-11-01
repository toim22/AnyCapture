#include <winsock2.h>               // before windows.h (Common.h)
#include <ws2tcpip.h>
#include "Upload.h"
#include "Net.h"
#include "Ui.h"
#include "Xml.h"
#include <wininet.h>
#include <regex>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")

namespace snap::upload {

using net::Json;

const wchar_t* DestName(Dest d) {
    switch (d) {
    case Dest::S3: return Tr(L"Armazenamento S3 (AWS e compatíveis)");
    case Dest::Dropbox: return L"Dropbox";
    case Dest::GoogleDrive: return L"Google Drive";
    case Dest::OneDrive: return L"OneDrive";
    case Dest::Ftp: return L"FTP";
    case Dest::Sftp: return L"SFTP";
    case Dest::Custom: return Tr(L"Enviador HTTP personalizado (.sxcu)");
    default: return Tr(L"Nenhum (desligado)");
    }
}

// ---------------------------------------------------------------- settings
static std::wstring SettingsFile() { return AppDataDir() + L"\\Uploaders.xml"; }

Settings Settings::Load() {
    Settings s;
    XmlNode n;
    if (!XmlLoadFile(SettingsFile(), n)) return s;
    s.dest = (Dest)std::clamp(n.geti("Destination"), 0, (int)Dest::Count - 1);
    s.s3Endpoint = n.getW("S3Endpoint", s.s3Endpoint); s.s3Region = n.getW("S3Region", s.s3Region); s.s3Bucket = n.getW("S3Bucket");
    s.s3Prefix = n.getW("S3Prefix", s.s3Prefix); s.s3AccessKey = n.getW("S3AccessKey"); s.s3PublicUrl = n.getW("S3PublicUrl");
    s.s3PathStyle = n.getb("S3PathStyle"); s.s3PublicRead = n.getb("S3PublicRead", true);
    s.dropboxAppKey = n.getW("DropboxAppKey"); s.googleClientId = n.getW("GoogleClientId"); s.msClientId = n.getW("MicrosoftClientId");
    s.folder = n.getW("Folder", s.folder); s.publicLink = n.getb("PublicLink", true);
    s.host = n.getW("Host"); s.user = n.getW("User"); s.keyFile = n.getW("KeyFile"); s.httpBase = n.getW("HttpBase"); s.port = n.geti("Port");
    if (const XmlNode* c = n.child("Custom")) s.customJson = Utf8ToW(c->text);
    s.copyLink = n.getb("CopyLink", true);
    return s;
}

void Settings::Save() const {
    XmlNode n("Uploaders");
    n.seti("Destination", (int)dest);
    n.setW("S3Endpoint", s3Endpoint).setW("S3Region", s3Region).setW("S3Bucket", s3Bucket).setW("S3Prefix", s3Prefix)
     .setW("S3AccessKey", s3AccessKey).setW("S3PublicUrl", s3PublicUrl).seti("S3PathStyle", s3PathStyle).seti("S3PublicRead", s3PublicRead);
    n.setW("DropboxAppKey", dropboxAppKey).setW("GoogleClientId", googleClientId).setW("MicrosoftClientId", msClientId);
    n.setW("Folder", folder).seti("PublicLink", publicLink);
    n.setW("Host", host).setW("User", user).setW("KeyFile", keyFile).setW("HttpBase", httpBase).seti("Port", port);
    n.seti("CopyLink", copyLink);
    if (!customJson.empty()) n.add("Custom").text = WToUtf8(customJson);
    XmlSaveFile(SettingsFile(), n);
}

bool Settings::Configured() const {
    switch (dest) {
    case Dest::S3: return !s3Bucket.empty() && !s3AccessKey.empty() && !net::LoadSecret(L"S3SecretKey").empty();
    case Dest::Dropbox: return !dropboxAppKey.empty() && !net::LoadSecret(L"DropboxRefresh").empty();
    case Dest::GoogleDrive: return !googleClientId.empty() && !net::LoadSecret(L"GoogleRefresh").empty();
    case Dest::OneDrive: return !msClientId.empty() && !net::LoadSecret(L"MicrosoftRefresh").empty();
    case Dest::Ftp: case Dest::Sftp: return !host.empty() && !user.empty();
    case Dest::Custom: return !customJson.empty();
    default: return false;
    }
}

// ---------------------------------------------------------------- helpers
namespace {
std::string ReadFileStr(const std::wstring& path) {
    std::string d;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return d;
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(h, &sz) && sz.QuadPart < 2LL * 1024 * 1024 * 1024) {
        d.resize((size_t)sz.QuadPart);
        DWORD rd = 0;
        if (!ReadFile(h, d.data(), (DWORD)d.size(), &rd, nullptr) || rd != d.size()) d.clear();
    }
    CloseHandle(h);
    return d;
}

std::string MimeOf(const std::wstring& name) {
    std::wstring e = FileExt(name);
    if (e == L".png") return "image/png";
    if (e == L".jpg" || e == L".jpeg") return "image/jpeg";
    if (e == L".gif") return "image/gif";
    if (e == L".webp") return "image/webp";
    if (e == L".mp4") return "video/mp4";
    if (e == L".pdf") return "application/pdf";
    return "application/octet-stream";
}

std::wstring Err(const net::Response& r, const wchar_t* what) {
    if (!r.status) return std::wstring(what) + L": " + r.error;
    std::string b = r.body.size() > 300 ? r.body.substr(0, 300) + "…" : r.body;
    return Format(L"%s: HTTP %d %s", what, r.status, Utf8ToW(b).c_str());
}

// JSON with only ASCII (HTTP headers such as Dropbox-API-Arg cannot carry UTF-8)
std::string AsciiJson(const std::string& utf8) {
    std::wstring w = Utf8ToW(utf8);
    std::string o;
    for (wchar_t c : w) {
        if (c == L'"') o += "\\\""; else if (c == L'\\') o += "\\\\";
        else if (c < 0x20 || c > 0x7E) o += FormatA("\\u%04x", (unsigned)c);
        else o += (char)c;
    }
    return o;
}

std::string Form(std::initializer_list<std::pair<std::string, std::string>> kv) {
    std::string s;
    for (auto& [k, v] : kv) { if (!s.empty()) s += '&'; s += net::UrlEncode(k) + "=" + net::UrlEncode(v); }
    return s;
}

std::string JsonField(const std::string& body, const char* path) {
    Json j;
    if (!Json::Parse(body, j)) return "";
    const Json* v = j.Path(path);
    return v ? v->Text() : "";
}

// ---- PKCE (RFC 7636, S256)
struct Pkce { std::string verifier, challenge; };
Pkce MakePkce() {
    Pkce p;
    p.verifier = net::Base64Url(net::RandomBytes(32));
    p.challenge = net::Base64Url(net::Sha256(p.verifier.data(), p.verifier.size()));
    return p;
}

// ---- loopback redirect (RFC 8252): 127.0.0.1 on a free port, one request, the "code" parameter
struct Loopback {
    SOCKET s = INVALID_SOCKET;
    int port = 0;
    std::string code, error, state;
    bool Open() {
        WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return false;
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 1) != 0) return false;
        int len = sizeof a;
        getsockname(s, (sockaddr*)&a, &len);
        port = ntohs(a.sin_port);
        u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
        return true;
    }
    // true when the browser came back (with the code or an error)
    bool Poll() {
        SOCKET c = accept(s, nullptr, nullptr);
        if (c == INVALID_SOCKET) return false;
        u_long nb = 0; ioctlsocket(c, FIONBIO, &nb);
        char buf[8192]; int n = recv(c, buf, sizeof buf - 1, 0);
        std::string req = n > 0 ? std::string(buf, n) : "";
        auto param = [&](const char* k) -> std::string {
            std::string key = std::string(k) + "=";
            size_t q = req.find('?'), e = req.find(' ', q == std::string::npos ? 0 : q);
            if (q == std::string::npos || e == std::string::npos) return "";
            std::string qs = "&" + req.substr(q + 1, e - q - 1);
            size_t p = qs.find("&" + key);
            if (p == std::string::npos) return "";
            p += key.size() + 1;
            std::string v = qs.substr(p, qs.find('&', p) - p), o;
            for (size_t i = 0; i < v.size(); i++) {
                if (v[i] == '%' && i + 2 < v.size()) { o += (char)strtol(v.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
                else o += v[i] == '+' ? ' ' : v[i];
            }
            return o;
        };
        code = param("code"); error = param("error"); state = param("state");
        std::string page = WToUtf8(Tr(L"Snapnab: login concluído. Você já pode fechar esta janela."));
        std::string html = "<html><head><meta charset=\"utf-8\"></head><body style=\"font-family:Segoe UI;margin:40px\"><h2>" + page + "</h2></body></html>";
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " + std::to_string(html.size()) + "\r\n\r\n" + html;
        send(c, resp.data(), (int)resp.size(), 0);
        closesocket(c);
        return !code.empty() || !error.empty();
    }
    ~Loopback() { if (s != INVALID_SOCKET) closesocket(s); WSACleanup(); }
};

// waits for the browser with a dialog the user can cancel (TaskDialog timer polls the socket)
bool WaitForBrowser(HWND owner, Loopback& lb) {
    struct Ctx { Loopback* lb; bool got = false; } ctx{ &lb };
    TASKDIALOGCONFIG tc{ sizeof tc };
    tc.hwndParent = owner;
    tc.dwFlags = TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SHOW_MARQUEE_PROGRESS_BAR;
    tc.pszWindowTitle = L"Snapnab";
    tc.pszMainInstruction = Tr(L"Conclua o login no navegador");
    tc.pszContent = Tr(L"Autorize o Snapnab na página que abriu. Esta janela fecha sozinha quando o navegador voltar.");
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tc.lpCallbackData = (LONG_PTR)&ctx;
    tc.pfCallback = [](HWND h, UINT n, WPARAM w, LPARAM, LONG_PTR data) -> HRESULT {
        auto* c = (Ctx*)data;
        if (n == TDN_CREATED) SendMessageW(h, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 0);
        if (n == TDN_TIMER && c->lb->Poll()) { c->got = true; SendMessageW(h, TDM_CLICK_BUTTON, IDOK, 0); }
        (void)w;
        return S_OK;
    };
    int btn = 0;
    TaskDialogIndirect(&tc, &btn, nullptr, nullptr);
    return ctx.got && !lb.code.empty();
}

// refreshes an access token from the stored refresh token (Google keeps the old refresh token: dossier C14)
std::string AccessToken(const std::wstring& tokenUrl, const std::string& form, const std::wstring& refreshSecret, std::wstring& err) {
    net::Response r = net::Request(L"POST", tokenUrl, { L"Content-Type: application/x-www-form-urlencoded" }, form);
    std::string at = JsonField(r.body, "access_token");
    if (!r.ok() || at.empty()) { err = Err(r, Tr(L"Falha ao renovar o acesso (faça login de novo nas opções de upload)")); return ""; }
    std::string rt = JsonField(r.body, "refresh_token");
    if (!rt.empty()) net::SaveSecret(refreshSecret, Utf8ToW(rt));       // rotated refresh tokens (Microsoft, Dropbox)
    return at;
}

std::string Refresh(const Settings& s, Dest d, std::wstring& err) {
    switch (d) {
    case Dest::Dropbox:
        return AccessToken(L"https://api.dropboxapi.com/oauth2/token",
                           Form({ { "grant_type", "refresh_token" }, { "refresh_token", WToUtf8(net::LoadSecret(L"DropboxRefresh")) }, { "client_id", WToUtf8(s.dropboxAppKey) } }),
                           L"DropboxRefresh", err);
    case Dest::GoogleDrive:
        return AccessToken(L"https://oauth2.googleapis.com/token",
                           Form({ { "grant_type", "refresh_token" }, { "refresh_token", WToUtf8(net::LoadSecret(L"GoogleRefresh")) }, { "client_id", WToUtf8(s.googleClientId) },
                                  { "client_secret", WToUtf8(net::LoadSecret(L"GoogleClientSecret")) } }),
                           L"GoogleRefresh", err);
    case Dest::OneDrive:
        return AccessToken(L"https://login.microsoftonline.com/common/oauth2/v2.0/token",
                           Form({ { "grant_type", "refresh_token" }, { "refresh_token", WToUtf8(net::LoadSecret(L"MicrosoftRefresh")) }, { "client_id", WToUtf8(s.msClientId) },
                                  { "scope", "offline_access Files.ReadWrite" } }),
                           L"MicrosoftRefresh", err);
    default: return "";
    }
}

// ---------------------------------------------------------------- destinations
Result UploadS3(const Settings& s, const std::string& data, const std::wstring& name) {
    Result r;
    std::string secret = WToUtf8(net::LoadSecret(L"S3SecretKey"));
    if (s.s3Bucket.empty() || s.s3AccessKey.empty() || secret.empty()) { r.error = Tr(L"Configure o bucket e as chaves do S3."); return r; }
    std::string bucket = WToUtf8(s.s3Bucket), endpoint = WToUtf8(s.s3Endpoint), region = WToUtf8(s.s3Region.empty() ? L"us-east-1" : s.s3Region);
    if (endpoint.rfind("https://", 0) == 0) endpoint = endpoint.substr(8);
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    std::string key = WToUtf8(s.s3Prefix + name);
    std::string host = s.s3PathStyle ? endpoint : bucket + "." + endpoint;
    std::string uri = "/" + (s.s3PathStyle ? bucket + "/" : std::string()) + net::UrlEncode(key, true);
    SYSTEMTIME st; GetSystemTime(&st);
    std::string amzDate = FormatA("%04d%02d%02dT%02d%02d%02dZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::string hash = net::Hex(net::Sha256(data.data(), data.size()));
    std::map<std::string, std::string> h = { { "host", host }, { "x-amz-content-sha256", hash }, { "x-amz-date", amzDate },
                                             { "content-type", MimeOf(name) } };
    if (s.s3PublicRead) h["x-amz-acl"] = "public-read";
    std::string auth = SigV4Authorization("PUT", uri, "", h, hash, amzDate, region, "s3", WToUtf8(s.s3AccessKey), secret);
    std::vector<std::wstring> hdr = { L"Authorization: " + Utf8ToW(auth) };
    for (auto& [k, v] : h) if (k != "host") hdr.push_back(Utf8ToW(k + ": " + v));
    net::Response resp = net::Request(L"PUT", Utf8ToW("https://" + host + uri), hdr, data, 300000);
    if (!resp.ok()) { r.error = Err(resp, Tr(L"Falha no upload para o S3")); return r; }
    std::wstring base = s.s3PublicUrl.empty() ? Utf8ToW("https://" + host + (s.s3PathStyle ? "/" + bucket : std::string())) : s.s3PublicUrl;
    while (!base.empty() && base.back() == L'/') base.pop_back();
    r.ok = true;
    r.url = base + L"/" + Utf8ToW(net::UrlEncode(key, true));
    return r;
}

Result UploadDropbox(const Settings& s, const std::string& data, const std::wstring& name) {
    Result r;
    if (data.size() >= 150000000) { r.error = Tr(L"O Dropbox aceita até 150 MB por envio pela API."); return r; }
    std::string at = Refresh(s, Dest::Dropbox, r.error);
    if (at.empty()) return r;
    std::string path = "/" + WToUtf8(s.folder.empty() ? L"" : s.folder + L"/") + WToUtf8(name);
    std::wstring bearer = L"Authorization: Bearer " + Utf8ToW(at);
    net::Response up = net::Request(L"POST", L"https://content.dropboxapi.com/2/files/upload",
                                    { bearer, L"Content-Type: application/octet-stream",
                                      Utf8ToW("Dropbox-API-Arg: {\"path\":\"" + AsciiJson(path) + "\",\"mode\":\"add\",\"autorename\":true}") }, data, 300000);
    if (!up.ok()) { r.error = Err(up, Tr(L"Falha no upload para o Dropbox")); return r; }
    std::string saved = JsonField(up.body, "path_display");
    if (saved.empty()) saved = path;
    net::Response ln = net::Request(L"POST", L"https://api.dropboxapi.com/2/sharing/create_shared_link_with_settings",
                                    { bearer, L"Content-Type: application/json" }, "{\"path\":\"" + net::JsonEscape(saved) + "\"}");
    std::string url = JsonField(ln.body, "url");
    if (url.empty() && ln.body.find("shared_link_already_exists") != std::string::npos) {
        net::Response ls = net::Request(L"POST", L"https://api.dropboxapi.com/2/sharing/list_shared_links",
                                        { bearer, L"Content-Type: application/json" }, "{\"path\":\"" + net::JsonEscape(saved) + "\",\"direct_only\":true}");
        url = JsonField(ls.body, "links[0].url");
    }
    if (url.empty()) { r.error = Err(ln, Tr(L"O arquivo foi enviado, mas o link não pôde ser criado")); return r; }
    r.ok = true; r.url = Utf8ToW(url);
    return r;
}

Result UploadDrive(const Settings& s, const std::string& data, const std::wstring& name) {
    Result r;
    std::string at = Refresh(s, Dest::GoogleDrive, r.error);
    if (at.empty()) return r;
    std::wstring bearer = L"Authorization: Bearer " + Utf8ToW(at);
    // the folder (created once, then remembered by its id)
    std::wstring folderId = RegGetString(L"GoogleDriveFolderId", L"");
    if (!s.folder.empty() && folderId.empty()) {
        std::string meta = "{\"name\":\"" + net::JsonEscape(WToUtf8(s.folder)) + "\",\"mimeType\":\"application/vnd.google-apps.folder\"}";
        net::Response f = net::Request(L"POST", L"https://www.googleapis.com/drive/v3/files?fields=id", { bearer, L"Content-Type: application/json" }, meta);
        folderId = Utf8ToW(JsonField(f.body, "id"));
        if (!folderId.empty()) RegSetString(L"GoogleDriveFolderId", folderId);
    }
    std::string meta = "{\"name\":\"" + net::JsonEscape(WToUtf8(name)) + "\"" +
                       (folderId.empty() ? std::string() : ",\"parents\":[\"" + WToUtf8(folderId) + "\"]") + "}";
    std::string boundary = "snapnab" + net::Hex(net::RandomBytes(8));
    std::string body = "--" + boundary + "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + meta + "\r\n--" + boundary +
                       "\r\nContent-Type: " + MimeOf(name) + "\r\n\r\n" + data + "\r\n--" + boundary + "--\r\n";
    net::Response up = net::Request(L"POST", L"https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart&fields=id,webViewLink",
                                    { bearer, Utf8ToW("Content-Type: multipart/related; boundary=" + boundary) }, body, 300000);
    std::string id = JsonField(up.body, "id"), link = JsonField(up.body, "webViewLink");
    if (!up.ok() || id.empty()) {
        if (up.status == 404 && !folderId.empty()) RegSetString(L"GoogleDriveFolderId", L"");     // the folder was deleted
        r.error = Err(up, Tr(L"Falha no upload para o Google Drive"));
        return r;
    }
    if (s.publicLink)
        net::Request(L"POST", Utf8ToW("https://www.googleapis.com/drive/v3/files/" + id + "/permissions"), { bearer, L"Content-Type: application/json" },
                     "{\"role\":\"reader\",\"type\":\"anyone\"}");
    r.ok = true; r.url = Utf8ToW(link.empty() ? "https://drive.google.com/file/d/" + id + "/view" : link);
    return r;
}

Result UploadOneDrive(const Settings& s, const std::string& data, const std::wstring& name) {
    Result r;
    std::string at = Refresh(s, Dest::OneDrive, r.error);
    if (at.empty()) return r;
    std::wstring bearer = L"Authorization: Bearer " + Utf8ToW(at);
    std::string path = net::UrlEncode(WToUtf8((s.folder.empty() ? L"" : s.folder + L"/") + name), true);
    net::Response up = net::Request(L"PUT", Utf8ToW("https://graph.microsoft.com/v1.0/me/drive/root:/" + path + ":/content?@microsoft.graph.conflictBehavior=rename"),
                                    { bearer, Utf8ToW("Content-Type: " + MimeOf(name)) }, data, 300000);
    std::string id = JsonField(up.body, "id");
    if (!up.ok() || id.empty()) { r.error = Err(up, Tr(L"Falha no upload para o OneDrive")); return r; }
    std::string kind = s.publicLink ? "{\"type\":\"view\",\"scope\":\"anonymous\"}" : "{\"type\":\"view\",\"scope\":\"organization\"}";
    net::Response ln = net::Request(L"POST", Utf8ToW("https://graph.microsoft.com/v1.0/me/drive/items/" + id + "/createLink"),
                                    { bearer, L"Content-Type: application/json" }, kind);
    std::string url = JsonField(ln.body, "link.webUrl");
    if (url.empty()) url = JsonField(up.body, "webUrl");
    r.ok = !url.empty();
    r.url = Utf8ToW(url);
    if (!r.ok) r.error = Err(ln, Tr(L"O arquivo foi enviado, mas o link não pôde ser criado"));
    return r;
}

std::wstring PublicUrl(const Settings& s, const std::wstring& name, const wchar_t* scheme) {
    std::wstring base = s.httpBase;
    if (base.empty()) base = std::wstring(scheme) + s.host + (s.folder.empty() ? L"" : L"/" + s.folder);
    while (!base.empty() && base.back() == L'/') base.pop_back();
    return base + L"/" + Utf8ToW(net::UrlEncode(WToUtf8(name)));
}

Result UploadFtp(const Settings& s, const std::wstring& local, const std::wstring& name) {
    Result r;
    std::wstring pass = net::LoadSecret(L"FtpPassword");
    HINTERNET in = InternetOpenW(L"Snapnab", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    HINTERNET c = in ? InternetConnectW(in, s.host.c_str(), (INTERNET_PORT)(s.port ? s.port : 21), s.user.c_str(), pass.c_str(),
                                        INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0) : nullptr;
    if (!c) { r.error = Format(Tr(L"Não foi possível conectar ao FTP (%u)."), GetLastError()); if (in) InternetCloseHandle(in); return r; }
    std::wstring dir;
    if (!s.folder.empty()) {                           // creates each level of the folder (existing ones fail quietly)
        for (size_t i = 0; i <= s.folder.size(); i++)
            if (i == s.folder.size() || s.folder[i] == L'/') { dir = s.folder.substr(0, i); if (!dir.empty()) FtpCreateDirectoryW(c, dir.c_str()); }
    }
    std::wstring remote = (s.folder.empty() ? L"" : s.folder + L"/") + name;
    if (FtpPutFileW(c, local.c_str(), remote.c_str(), FTP_TRANSFER_TYPE_BINARY, 0)) { r.ok = true; r.url = PublicUrl(s, name, L"ftp://"); }
    else r.error = Format(Tr(L"O servidor FTP recusou o arquivo (%u)."), GetLastError());
    InternetCloseHandle(c); InternetCloseHandle(in);
    return r;
}

Result UploadSftp(const Settings& s, const std::wstring& local, const std::wstring& name) {
    Result r;
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring exe = std::wstring(sys) + L"\\OpenSSH\\sftp.exe";
    if (!FileExists(exe)) { r.error = Tr(L"O cliente OpenSSH do Windows não está instalado (Configurações › Sistema › Recursos opcionais)."); return r; }
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring batch = std::wstring(tmp) + L"snapnab_sftp_" + TimeStamp() + L".txt";
    std::wstring remote = (s.folder.empty() ? L"" : s.folder + L"/") + name;
    std::string cmds = (s.folder.empty() ? "" : "-mkdir \"" + WToUtf8(s.folder) + "\"\n") + "put \"" + WToUtf8(local) + "\" \"" + WToUtf8(remote) + "\"\n";
    HANDLE f = CreateFileW(batch.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) { r.error = L"Temp file"; return r; }
    DWORD wr; WriteFile(f, cmds.data(), (DWORD)cmds.size(), &wr, nullptr); CloseHandle(f);
    std::wstring cmd = L"\"" + exe + L"\" -b \"" + batch + L"\" -o BatchMode=yes -o StrictHostKeyChecking=accept-new -P " +
                       std::to_wstring(s.port ? s.port : 22) + (s.keyFile.empty() ? L"" : L" -i \"" + s.keyFile + L"\"") + L" " + s.user + L"@" + s.host;
    STARTUPINFOW si{ sizeof si }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        DeleteFileW(batch.c_str()); r.error = Tr(L"Não foi possível iniciar o sftp."); return r;
    }
    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, 300000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    DeleteFileW(batch.c_str());
    if (code != 0) { r.error = Format(Tr(L"O SFTP falhou (código %u). Use autenticação por chave (sem senha) e confira host, usuário e pasta."), code); return r; }
    r.ok = true; r.url = PublicUrl(s, name, L"sftp://");
    return r;
}

Result UploadCustom(const Settings& s, const std::string& data, const std::wstring& name) {
    Result r;
    Json u;
    if (!Json::Parse(WToUtf8(s.customJson), u)) { r.error = Tr(L"O enviador personalizado não é um JSON válido."); return r; }
    auto str = [&](const char* k) { const Json* v = u.Get(k); return v ? v->Text() : std::string(); };
    std::string fname = WToUtf8(name);
    auto expand = [&](const std::string& t) { return ExpandSyntax(t, fname, "", {}); };
    std::string method = str("RequestMethod"); if (method.empty()) method = "POST";
    std::string url = expand(str("RequestURL"));
    if (url.empty()) { r.error = Tr(L"O enviador personalizado não tem RequestURL."); return r; }
    if (url.find("://") == std::string::npos) url = "https://" + url;
    if (const Json* p = u.Get("Parameters"); p && p->type == Json::Object) {
        for (auto& [k, v] : p->obj) url += std::string(url.find('?') == std::string::npos ? "?" : "&") + net::UrlEncode(k) + "=" + net::UrlEncode(expand(v.Text()));
    }
    std::vector<std::wstring> hdr;
    if (const Json* h = u.Get("Headers"); h && h->type == Json::Object) for (auto& [k, v] : h->obj) hdr.push_back(Utf8ToW(k + ": " + expand(v.Text())));
    std::string bodyKind = str("Body"), body;
    if (bodyKind.empty() || bodyKind == "MultipartFormData") {
        net::Multipart mp;
        if (const Json* a = u.Get("Arguments"); a && a->type == Json::Object) for (auto& [k, v] : a->obj) mp.Field(k, expand(v.Text()));
        std::string field = str("FileFormName"); if (field.empty()) field = "file";
        mp.File(field, fname, MimeOf(name), data);
        body = mp.Finish();
        hdr.push_back(mp.ContentType());
    } else if (bodyKind == "Binary") {
        body = data;
        hdr.push_back(Utf8ToW("Content-Type: " + MimeOf(name)));
    } else if (bodyKind == "FormURLEncoded") {
        if (const Json* a = u.Get("Arguments"); a && a->type == Json::Object)
            for (auto& [k, v] : a->obj) { if (!body.empty()) body += '&'; body += net::UrlEncode(k) + "=" + net::UrlEncode(expand(v.Text())); }
        hdr.push_back(L"Content-Type: application/x-www-form-urlencoded");
    } else {                                          // JSON / XML: the Data template
        body = expand(str("Data"));
        hdr.push_back(bodyKind == "XML" ? L"Content-Type: application/xml" : L"Content-Type: application/json");
    }
    net::Response resp = net::Request(Utf8ToW(method), Utf8ToW(url), hdr, body, 300000);
    if (!resp.ok()) {
        std::string em = str("ErrorMessage");
        r.error = em.empty() ? Err(resp, Tr(L"Falha no envio")) : Utf8ToW(ExpandSyntax(em, fname, resp.body, resp.headers));
        return r;
    }
    std::string link = str("URL");
    r.url = Utf8ToW(link.empty() ? resp.body : ExpandSyntax(link, fname, resp.body, resp.headers));
    r.thumbnailUrl = Utf8ToW(ExpandSyntax(str("ThumbnailURL"), fname, resp.body, resp.headers));
    r.deleteUrl = Utf8ToW(ExpandSyntax(str("DeletionURL"), fname, resp.body, resp.headers));
    r.url = Trim(r.url);
    r.ok = !r.url.empty();
    if (!r.ok) r.error = Tr(L"O servidor respondeu, mas o link não foi encontrado na resposta.");
    return r;
}
} // namespace

// ---------------------------------------------------------------- AWS Signature V4
std::string SigV4Authorization(const std::string& method, const std::string& uri, const std::string& query,
                               const std::map<std::string, std::string>& headers, const std::string& payloadHash,
                               const std::string& amzDate, const std::string& region, const std::string& service,
                               const std::string& accessKey, const std::string& secretKey) {
    std::string canonHeaders, signedHeaders;
    for (auto& [k, v] : headers) {                    // std::map: already sorted by the lowercase name
        canonHeaders += k + ":" + v + "\n";
        signedHeaders += (signedHeaders.empty() ? "" : ";") + k;
    }
    std::string canonical = method + "\n" + uri + "\n" + query + "\n" + canonHeaders + "\n" + signedHeaders + "\n" + payloadHash;
    std::string date = amzDate.substr(0, 8);
    std::string scope = date + "/" + region + "/" + service + "/aws4_request";
    std::string toSign = "AWS4-HMAC-SHA256\n" + amzDate + "\n" + scope + "\n" + net::Hex(net::Sha256(canonical.data(), canonical.size()));
    std::string k0 = "AWS4" + secretKey;
    auto k = net::HmacSha256(std::vector<BYTE>(k0.begin(), k0.end()), date);
    k = net::HmacSha256(k, region);
    k = net::HmacSha256(k, service);
    k = net::HmacSha256(k, "aws4_request");
    std::string sig = net::Hex(net::HmacSha256(k, toSign));
    return "AWS4-HMAC-SHA256 Credential=" + accessKey + "/" + scope + ",SignedHeaders=" + signedHeaders + ",Signature=" + sig;
}

// ---------------------------------------------------------------- custom syntax (dossier C21: '{' '}' ':' '|' '\')
std::string ExpandSyntax(const std::string& text, const std::string& fileName, const std::string& response,
                         const std::map<std::wstring, std::wstring>& headers) {
    std::function<std::string(size_t&, int)> parse = [&](size_t& i, int depth) -> std::string {
        std::string out;
        while (i < text.size()) {
            char c = text[i];
            if (c == '\\' && i + 1 < text.size()) { out += text[i + 1]; i += 2; continue; }
            if (c == '}' && depth > 0) return out;
            if (c == '|' && depth > 0) return out;
            if (c != '{') { out += c; i++; continue; }
            i++;
            // function name up to ':' or '}'
            std::string name;
            while (i < text.size() && text[i] != ':' && text[i] != '}') name += text[i++];
            std::vector<std::string> args;
            if (i < text.size() && text[i] == ':') {
                i++;
                for (;;) {
                    args.push_back(parse(i, depth + 1));
                    if (i < text.size() && text[i] == '|') { i++; continue; }
                    break;
                }
            }
            if (i < text.size() && text[i] == '}') i++;
            std::string n = name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if (n == "filename") out += fileName;
            else if (n == "input") out += "";
            else if (n == "response") out += response;
            else if (n == "base64" && !args.empty()) out += Base64Encode(std::vector<BYTE>(args[0].begin(), args[0].end()));
            else if (n == "header" && !args.empty()) { auto it = headers.find(ToLower(Utf8ToW(args[0]))); if (it != headers.end()) out += WToUtf8(it->second); }
            else if (n == "json" && !args.empty()) {
                Json j; std::string src = args.size() > 1 ? args[0] : response, path = args.size() > 1 ? args[1] : args[0];
                if (Json::Parse(src, j)) if (const Json* v = j.Path(path)) out += v->Text();
            } else if (n == "regex" && !args.empty()) {
                try {
                    std::smatch m;
                    std::regex re(args[0]);
                    if (std::regex_search(response, m, re)) {
                        size_t g = args.size() > 1 ? (size_t)atoi(args[1].c_str()) : (m.size() > 1 ? 1 : 0);
                        if (g < m.size()) out += m[g].str();
                    }
                } catch (...) {}
            } else if (n == "xml" && !args.empty()) {     // simple element path: the text of the last element named
                std::string tag = args[0].substr(args[0].find_last_of('/') + 1);
                size_t a = response.find("<" + tag);
                if (a != std::string::npos) { a = response.find('>', a); size_t b = response.find("</" + tag, a); if (a != std::string::npos && b != std::string::npos) out += response.substr(a + 1, b - a - 1); }
            } else if (n == "random" && !args.empty()) out += args[net::RandomBytes(1)[0] % args.size()];
            else out += "{" + name + "}";
        }
        return out;
    };
    size_t i = 0;
    return parse(i, 0);
}

// ---------------------------------------------------------------- upload entry points
Result UploadFile(const Settings& s, const std::wstring& path, const std::wstring& remoteName) {
    Result r;
    if (s.dest == Dest::None) { r.error = Tr(L"Nenhum destino de upload configurado."); return r; }
    std::wstring name = remoteName.empty() ? FileNameOf(path) : remoteName;
    for (auto& c : name) if (wcschr(L"\\/:*?\"<>|#%", c)) c = L'_';
    Log(L"Upload: %s → %s", path.c_str(), DestName(s.dest));
    switch (s.dest) {
    case Dest::Ftp: r = UploadFtp(s, path, name); break;
    case Dest::Sftp: r = UploadSftp(s, path, name); break;
    default: {
        std::string data = ReadFileStr(path);
        if (data.empty()) { r.error = Tr(L"Não foi possível ler o arquivo."); return r; }
        switch (s.dest) {
        case Dest::S3: r = UploadS3(s, data, name); break;
        case Dest::Dropbox: r = UploadDropbox(s, data, name); break;
        case Dest::GoogleDrive: r = UploadDrive(s, data, name); break;
        case Dest::OneDrive: r = UploadOneDrive(s, data, name); break;
        case Dest::Custom: r = UploadCustom(s, data, name); break;
        default: break;
        }
    }
    }
    Log(L"Upload: %s", r.ok ? r.url.c_str() : r.error.c_str());
    return r;
}

namespace {
// results of the worker threads go back to the UI thread through a message-only window
constexpr UINT WM_UPLOAD_DONE = WM_APP + 0x71;
struct Pending { std::function<void(const Result&)> done; Result r; };
HWND Marshal() {
    static HWND h = nullptr;
    if (h) return h;
    WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SnapnabUploadMarshal";
    wc.lpfnWndProc = [](HWND w, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
        if (m == WM_UPLOAD_DONE) { std::unique_ptr<Pending> p((Pending*)lp); if (p->done) p->done(p->r); return 0; }
        return DefWindowProcW(w, m, wp, lp);
    };
    RegisterClassW(&wc);
    h = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    return h;
}
}

void UploadFileAsync(const std::wstring& path, std::function<void(const Result&)> done) {
    HWND m = Marshal();
    Settings s = Settings::Load();
    std::thread([m, s, path, done] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto* p = new Pending{ done, UploadFile(s, path, L"") };
        CoUninitialize();
        PostMessageW(m, WM_UPLOAD_DONE, 0, (LPARAM)p);
    }).detach();
}

void UploadImageAsync(const Image& img, const std::wstring& nameIn, std::function<void(const Result&)> done) {
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"Snapnab";
    EnsureDir(dir);
    std::wstring name = nameIn.empty() ? L"Snapnab_" + TimeStamp() : nameIn;
    for (auto& c : name) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    std::wstring path = dir + L"\\" + name + L".png";
    SaveOptions o = SaveOptions::FromSettings();
    o.stripMetadata = RegGetDword(L"StripMetadataOnUpload", 1) != 0;       // C29: on by default for uploads
    if (!SaveImageFileEx(path, img, o)) { Result r; r.error = Tr(L"Não foi possível preparar a imagem."); done(r); return; }
    UploadFileAsync(path, [path, done](const Result& r) { DeleteFileW(path.c_str()); done(r); });
}

// ---------------------------------------------------------------- uploaded links
namespace {
std::wstring UploadsFile() { return LibraryDir() + L"\\Uploads.xml"; }
}
std::wstring UploadedUrl(const std::wstring& item) {
    XmlNode n;
    if (!XmlLoadFile(UploadsFile(), n)) return L"";
    std::wstring key = ToLower(FileNameOf(item));
    for (auto& c : n.children) if (ToLower(c.getW("File")) == key) return c.getW("Url");
    return L"";
}
void SetUploadedUrl(const std::wstring& item, const std::wstring& url) {
    XmlNode n("Uploads");
    XmlLoadFile(UploadsFile(), n);
    std::wstring key = FileNameOf(item);
    n.children.erase(std::remove_if(n.children.begin(), n.children.end(), [&](const XmlNode& c) { return ToLower(c.getW("File")) == ToLower(key); }), n.children.end());
    if (!url.empty()) n.add("Upload").setW("File", key).setW("Url", url);
    while (n.children.size() > 2000) n.children.erase(n.children.begin());
    XmlSaveFile(UploadsFile(), n);
}

// ---------------------------------------------------------------- settings dialogs and sign-in
namespace {
bool SignIn(HWND owner, Settings& s, std::wstring& err) {
    Pkce pk = MakePkce();
    if (s.dest == Dest::Dropbox) {
        // Dropbox: no redirect — the page shows a code to paste here
        std::wstring url = L"https://www.dropbox.com/oauth2/authorize?response_type=code&token_access_type=offline&code_challenge_method=S256&client_id=" +
                           s.dropboxAppKey + L"&code_challenge=" + Utf8ToW(pk.challenge);
        ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        std::vector<ui::Field> f = { ui::Field::MakeLabel(Tr(L"Autorize no navegador e cole aqui o código que o Dropbox mostrar.")),
                                     ui::Field::MakeText(Tr(L"Código"), L"") };
        if (!ui::RunForm(owner, Tr(L"Login no Dropbox"), f) || Trim(f[1].text).empty()) return false;
        net::Response r = net::Request(L"POST", L"https://api.dropboxapi.com/oauth2/token", { L"Content-Type: application/x-www-form-urlencoded" },
                                       Form({ { "code", WToUtf8(Trim(f[1].text)) }, { "grant_type", "authorization_code" },
                                              { "code_verifier", pk.verifier }, { "client_id", WToUtf8(s.dropboxAppKey) } }));
        std::string rt = JsonField(r.body, "refresh_token");
        if (rt.empty()) { err = Err(r, Tr(L"O Dropbox recusou o código")); return false; }
        net::SaveSecret(L"DropboxRefresh", Utf8ToW(rt));
        return true;
    }
    Loopback lb;
    if (!lb.Open()) { err = Tr(L"Não foi possível abrir a porta local para o retorno do navegador."); return false; }
    bool google = s.dest == Dest::GoogleDrive;
    std::string redirect = (google ? "http://127.0.0.1:" : "http://localhost:") + std::to_string(lb.port);
    std::string state = net::Hex(net::RandomBytes(8));
    std::string url = google
        ? "https://accounts.google.com/o/oauth2/v2/auth?response_type=code&access_type=offline&prompt=consent&scope=" +
          net::UrlEncode("https://www.googleapis.com/auth/drive.file") + "&client_id=" + net::UrlEncode(WToUtf8(s.googleClientId))
        : "https://login.microsoftonline.com/common/oauth2/v2.0/authorize?response_type=code&scope=" + net::UrlEncode("offline_access Files.ReadWrite") +
          "&client_id=" + net::UrlEncode(WToUtf8(s.msClientId));
    url += "&redirect_uri=" + net::UrlEncode(redirect) + "&code_challenge_method=S256&code_challenge=" + pk.challenge + "&state=" + state;
    ShellExecuteW(owner, L"open", Utf8ToW(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (!WaitForBrowser(owner, lb)) { err = lb.error.empty() ? std::wstring() : Utf8ToW(lb.error); return false; }
    if (lb.state != state) { err = Tr(L"Resposta de login inválida."); return false; }
    net::Response r = google
        ? net::Request(L"POST", L"https://oauth2.googleapis.com/token", { L"Content-Type: application/x-www-form-urlencoded" },
                       Form({ { "code", lb.code }, { "grant_type", "authorization_code" }, { "redirect_uri", redirect }, { "code_verifier", pk.verifier },
                              { "client_id", WToUtf8(s.googleClientId) }, { "client_secret", WToUtf8(net::LoadSecret(L"GoogleClientSecret")) } }))
        : net::Request(L"POST", L"https://login.microsoftonline.com/common/oauth2/v2.0/token", { L"Content-Type: application/x-www-form-urlencoded" },
                       Form({ { "code", lb.code }, { "grant_type", "authorization_code" }, { "redirect_uri", redirect }, { "code_verifier", pk.verifier },
                              { "client_id", WToUtf8(s.msClientId) }, { "scope", "offline_access Files.ReadWrite" } }));
    std::string rt = JsonField(r.body, "refresh_token");
    if (rt.empty()) { err = Err(r, Tr(L"O login foi recusado")); return false; }
    net::SaveSecret(google ? L"GoogleRefresh" : L"MicrosoftRefresh", Utf8ToW(rt));
    if (google) RegSetString(L"GoogleDriveFolderId", L"");
    return true;
}
}

bool ConfigureDialog(HWND owner) {
    Settings s = Settings::Load();
    std::vector<std::wstring> names;
    for (int d = 0; d < (int)Dest::Count; d++) names.push_back(DestName((Dest)d));
    std::vector<ui::Field> f0 = {
        ui::Field::MakeLabel(Tr(L"Desligado por padrão: nada é enviado até você escolher um destino.")),
        ui::Field::MakeCombo(Tr(L"Destino"), names, (int)s.dest),
        ui::Field::MakeCheck(Tr(L"Copiar o link para a área de transferência depois do envio"), s.copyLink),
        ui::Field::MakeCheck(Tr(L"Remover metadados das imagens enviadas"), RegGetDword(L"StripMetadataOnUpload", 1) != 0),
    };
    if (!ui::RunForm(owner, Tr(L"Upload — destino"), f0)) return false;
    s.dest = (Dest)f0[1].value;
    s.copyLink = f0[2].value != 0;
    RegSetDword(L"StripMetadataOnUpload", f0[3].value);
    using F = ui::Field;
    std::vector<F> f;
    switch (s.dest) {
    case Dest::S3:
        f = { F::MakeText(Tr(L"Endpoint (host)"), s.s3Endpoint), F::MakeText(Tr(L"Região"), s.s3Region), F::MakeText(L"Bucket", s.s3Bucket),
              F::MakeText(Tr(L"Prefixo (pasta)"), s.s3Prefix), F::MakeText(L"Access key ID", s.s3AccessKey),
              F::MakeText(L"Secret access key", net::LoadSecret(L"S3SecretKey").empty() ? L"" : L"********"),
              F::MakeText(Tr(L"Endereço público (opcional)"), s.s3PublicUrl), F::MakeCheck(Tr(L"Endereço no estilo de caminho (MinIO e alguns serviços)"), s.s3PathStyle),
              F::MakeCheck(Tr(L"Leitura pública (x-amz-acl: public-read)"), s.s3PublicRead),
              F::MakeLabel(Tr(L"A chave secreta fica no Gerenciador de Credenciais do Windows.")) };
        break;
    case Dest::Dropbox:
        f = { F::MakeText(Tr(L"App key do seu app Dropbox"), s.dropboxAppKey), F::MakeText(Tr(L"Pasta"), s.folder),
              F::MakeLabel(Tr(L"Crie um app em dropbox.com/developers (acesso \"App folder\" ou \"Full\", permissões files.content.write e sharing.write).")) };
        break;
    case Dest::GoogleDrive:
        f = { F::MakeText(Tr(L"Client ID (app para computador)"), s.googleClientId),
              F::MakeText(L"Client secret", net::LoadSecret(L"GoogleClientSecret").empty() ? L"" : L"********"),
              F::MakeText(Tr(L"Pasta"), s.folder), F::MakeCheck(Tr(L"Qualquer pessoa com o link pode ver"), s.publicLink),
              F::MakeLabel(Tr(L"Crie credenciais OAuth do tipo \"App para computador\" no Google Cloud Console (API do Drive ativada).")) };
        break;
    case Dest::OneDrive:
        f = { F::MakeText(Tr(L"Client ID (registro de app do Azure)"), s.msClientId), F::MakeText(Tr(L"Pasta"), s.folder),
              F::MakeCheck(Tr(L"Link anônimo (qualquer pessoa com o link)"), s.publicLink),
              F::MakeLabel(Tr(L"Registre um app (\"Aplicativos móveis e de desktop\", redirecionamento http://localhost) com a permissão Files.ReadWrite.")) };
        break;
    case Dest::Ftp: case Dest::Sftp:
        f = { F::MakeText(Tr(L"Servidor"), s.host), F::MakeInt(Tr(L"Porta (0 = padrão)"), s.port, 0, 65535), F::MakeText(Tr(L"Usuário"), s.user),
              s.dest == Dest::Ftp ? F::MakeText(Tr(L"Senha"), net::LoadSecret(L"FtpPassword").empty() ? L"" : L"********")
                                  : F::MakeText(Tr(L"Arquivo da chave privada"), s.keyFile),
              F::MakeText(Tr(L"Pasta remota"), s.folder), F::MakeText(Tr(L"Endereço web da pasta (para o link)"), s.httpBase),
              F::MakeLabel(s.dest == Dest::Sftp ? Tr(L"Usa o cliente OpenSSH do Windows com autenticação por chave.")
                                                : Tr(L"FTP sem criptografia: prefira SFTP para dados sensíveis.")) };
        break;
    case Dest::Custom: {
        std::wstring path;
        std::wstring filter = Tr(L"Enviador personalizado (*.sxcu;*.json)|*.sxcu;*.json|");
        std::replace(filter.begin(), filter.end(), L'|', L'\0');
        if (ui::OpenFileDialog(owner, filter, path, Tr(L"Importar enviador (.sxcu)"))) {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            std::string d;
            if (h != INVALID_HANDLE_VALUE) { DWORD n = GetFileSize(h, nullptr), rd = 0; d.resize(n); ReadFile(h, d.data(), n, &rd, nullptr); CloseHandle(h); }
            if (d.size() >= 3 && (BYTE)d[0] == 0xEF) d = d.substr(3);
            Json j;
            if (!Json::Parse(d, j) || !j.Get("RequestURL")) { MessageBoxW(owner, Tr(L"Arquivo .sxcu inválido."), L"Snapnab", MB_ICONWARNING); return false; }
            s.customJson = Utf8ToW(d);
        } else if (s.customJson.empty()) return false;
        s.Save();
        return true;
    }
    default: s.Save(); return true;
    }
    if (!ui::RunForm(owner, std::wstring(Tr(L"Upload — ")) + DestName(s.dest), f)) return false;
    auto secret = [](const std::wstring& name, const std::wstring& v) { if (v != L"********") { if (v.empty()) net::DeleteSecret(name); else net::SaveSecret(name, v); } };
    bool needLogin = false;
    switch (s.dest) {
    case Dest::S3:
        s.s3Endpoint = Trim(f[0].text); s.s3Region = Trim(f[1].text); s.s3Bucket = Trim(f[2].text); s.s3Prefix = f[3].text;
        s.s3AccessKey = Trim(f[4].text); secret(L"S3SecretKey", Trim(f[5].text)); s.s3PublicUrl = Trim(f[6].text);
        s.s3PathStyle = f[7].value != 0; s.s3PublicRead = f[8].value != 0;
        break;
    case Dest::Dropbox:
        needLogin = Trim(f[0].text) != s.dropboxAppKey || net::LoadSecret(L"DropboxRefresh").empty();
        s.dropboxAppKey = Trim(f[0].text); s.folder = Trim(f[1].text);
        break;
    case Dest::GoogleDrive:
        needLogin = Trim(f[0].text) != s.googleClientId || net::LoadSecret(L"GoogleRefresh").empty();
        s.googleClientId = Trim(f[0].text); secret(L"GoogleClientSecret", Trim(f[1].text)); s.folder = Trim(f[2].text); s.publicLink = f[3].value != 0;
        break;
    case Dest::OneDrive:
        needLogin = Trim(f[0].text) != s.msClientId || net::LoadSecret(L"MicrosoftRefresh").empty();
        s.msClientId = Trim(f[0].text); s.folder = Trim(f[1].text); s.publicLink = f[2].value != 0;
        break;
    case Dest::Ftp: case Dest::Sftp:
        s.host = Trim(f[0].text); s.port = f[1].value; s.user = Trim(f[2].text);
        if (s.dest == Dest::Ftp) secret(L"FtpPassword", f[3].text); else s.keyFile = Trim(f[3].text);
        s.folder = Trim(f[4].text); s.httpBase = Trim(f[5].text);
        break;
    default: break;
    }
    s.Save();
    bool oauth = s.dest == Dest::Dropbox || s.dest == Dest::GoogleDrive || s.dest == Dest::OneDrive;
    if (oauth && (needLogin || MessageBoxW(owner, Tr(L"Fazer login de novo agora?"), L"Snapnab", MB_YESNO | MB_ICONQUESTION) == IDYES)) {
        std::wstring err;
        if (SignIn(owner, s, err)) MessageBoxW(owner, Tr(L"Login concluído. As capturas podem ser enviadas."), L"Snapnab", MB_ICONINFORMATION);
        else if (!err.empty()) MessageBoxW(owner, err.c_str(), L"Snapnab", MB_ICONWARNING);
    }
    return true;
}

} // namespace snap::upload
