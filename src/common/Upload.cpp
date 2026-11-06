// Optional upload (C21): the destinations, the settings file, the sign-in exchange and the custom uploader run in
// snapcore (rust/snapcore/src/upload.rs); here are the C++ entry points, the background thread and the settings dialogs.
#include "Upload.h"
#include "Net.h"
#include "Ui.h"
#include "SnapcoreBridge.h"

namespace snap::upload {

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

namespace {
// the settings as snapcore takes them (pointers into s, valid while s lives)
SnapcoreUploadSettings ToSnapcore(const Settings& s) {
    SnapcoreUploadSettings c{};
    c.dest = (int32_t)s.dest;
    c.s3Endpoint = s.s3Endpoint.c_str();
    c.s3Region = s.s3Region.c_str();
    c.s3Bucket = s.s3Bucket.c_str();
    c.s3Prefix = s.s3Prefix.c_str();
    c.s3AccessKey = s.s3AccessKey.c_str();
    c.s3PublicUrl = s.s3PublicUrl.c_str();
    c.dropboxAppKey = s.dropboxAppKey.c_str();
    c.googleClientId = s.googleClientId.c_str();
    c.msClientId = s.msClientId.c_str();
    c.folder = s.folder.c_str();
    c.host = s.host.c_str();
    c.user = s.user.c_str();
    c.keyFile = s.keyFile.c_str();
    c.httpBase = s.httpBase.c_str();
    c.customJson = s.customJson.c_str();
    c.s3PathStyle = s.s3PathStyle;
    c.s3PublicRead = s.s3PublicRead;
    c.publicLink = s.publicLink;
    c.copyLink = s.copyLink;
    c.port = s.port;
    return c;
}

std::wstring Str(const wchar_t* p) { return p ? p : L""; }
} // namespace

// ---------------------------------------------------------------- settings (Uploaders.xml)
Settings Settings::Load() {
    Settings s;
    SnapcoreUploadSettings* c = snapcore_upload_settings_load();
    if (!c) return s;
    s.dest = (Dest)std::clamp((int)c->dest, 0, (int)Dest::Count - 1);
    s.s3Endpoint = Str(c->s3Endpoint);
    s.s3Region = Str(c->s3Region);
    s.s3Bucket = Str(c->s3Bucket);
    s.s3Prefix = Str(c->s3Prefix);
    s.s3AccessKey = Str(c->s3AccessKey);
    s.s3PublicUrl = Str(c->s3PublicUrl);
    s.dropboxAppKey = Str(c->dropboxAppKey);
    s.googleClientId = Str(c->googleClientId);
    s.msClientId = Str(c->msClientId);
    s.folder = Str(c->folder);
    s.host = Str(c->host);
    s.user = Str(c->user);
    s.keyFile = Str(c->keyFile);
    s.httpBase = Str(c->httpBase);
    s.customJson = Str(c->customJson);
    s.s3PathStyle = c->s3PathStyle;
    s.s3PublicRead = c->s3PublicRead;
    s.publicLink = c->publicLink;
    s.copyLink = c->copyLink;
    s.port = c->port;
    snapcore_upload_settings_free(c);
    return s;
}

void Settings::Save() const {
    SnapcoreUploadSettings c = ToSnapcore(*this);
    snapcore_upload_settings_save(&c);
}

bool Settings::Configured() const {
    SnapcoreUploadSettings c = ToSnapcore(*this);
    return snapcore_upload_configured(&c);
}

// ---------------------------------------------------------------- upload entry points
Result UploadFile(const Settings& s, const std::wstring& path, const std::wstring& remoteName) {
    SnapcoreUploadSettings c = ToSnapcore(s);
    SnapcoreUploadResult o{};
    Result r;
    if (!snapcore_upload_file(&c, path.data(), path.size(), remoteName.data(), remoteName.size(), &o)) {
        r.error = Tr(L"Falha no envio");
        return r;
    }
    r.ok = o.ok;
    r.url = TakeWStr(o.url);
    r.deleteUrl = TakeWStr(o.deleteUrl);
    r.thumbnailUrl = TakeWStr(o.thumbnailUrl);
    r.error = TakeWStr(o.error);
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

// ---------------------------------------------------------------- uploaded links (Library\Uploads.xml, by file name)
std::wstring UploadedUrl(const std::wstring& item) {
    SnapcoreWStr s{};
    return snapcore_upload_url_get(item.data(), item.size(), &s) ? TakeWStr(s) : std::wstring();
}
void SetUploadedUrl(const std::wstring& item, const std::wstring& url) { snapcore_upload_url_set(item.data(), item.size(), url.data(), url.size()); }

// ---------------------------------------------------------------- settings dialogs and sign-in
namespace {
// waits for the browser with a dialog the user can cancel (TaskDialog timer polls the loopback socket)
bool WaitForBrowser(HWND owner, SnapcoreSignIn* si) {
    struct Ctx { SnapcoreSignIn* si; bool got = false; } ctx{ si };
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
        if (n == TDN_TIMER && snapcore_signin_poll(c->si)) { c->got = true; SendMessageW(h, TDM_CLICK_BUTTON, IDOK, 0); }
        (void)w;
        return S_OK;
    };
    int btn = 0;
    TaskDialogIndirect(&tc, &btn, nullptr, nullptr);
    return ctx.got;
}

// opens the sign-in page, then waits for the browser (Google, Microsoft) or asks for the code (Dropbox); snapcore
// exchanges the code for the refresh token and stores it
bool SignIn(HWND owner, Settings& s, std::wstring& err) {
    SnapcoreUploadSettings c = ToSnapcore(s);
    SnapcoreWStr urlOrError{};
    SnapcoreSignIn* si = snapcore_signin_begin(&c, &urlOrError);
    std::wstring url = TakeWStr(urlOrError);
    if (!si) { err = url; return false; }
    ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    std::wstring code;
    bool go = true;
    if (s.dest == Dest::Dropbox) {
        // Dropbox: no redirect — the page shows a code to paste here
        std::vector<ui::Field> f = { ui::Field::MakeLabel(Tr(L"Autorize no navegador e cole aqui o código que o Dropbox mostrar.")),
                                     ui::Field::MakeText(Tr(L"Código"), L"") };
        go = ui::RunForm(owner, Tr(L"Login no Dropbox"), f) && !Trim(f[1].text).empty();
        code = f[1].text;
    } else WaitForBrowser(owner, si);
    SnapcoreWStr e{};
    bool ok = go && snapcore_signin_finish(si, code.data(), code.size(), &e);
    err = TakeWStr(e);
    snapcore_signin_free(si);
    return ok;
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
            if (!snapcore_upload_custom_valid(d.data(), d.size())) { MessageBoxW(owner, Tr(L"Arquivo .sxcu inválido."), L"Snapnab", MB_ICONWARNING); return false; }
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
