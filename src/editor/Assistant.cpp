// AI assistant on a capture (C22; ShareX dossier 01 · F16 "Analyze image" with the user's own OpenAI/Gemini key):
// opt-in and off by default. The user picks a provider (OpenAI, Google Gemini, Anthropic, an OpenAI-compatible endpoint
// or a local model through Ollama's OpenAI-compatible API) and a model; the key stays in the Windows Credential
// Manager ("Snapnab/AI/<provider>"), the rest in AiSettings.xml. Each question sends the image (PNG, at most 2000 px)
// to that provider only — the window says so before anything is sent. Answers can be copied or added as a Text object.
#include "EditorTools.h"
#include "Clipboard.h"
#include "Net.h"
#include "Ui.h"
#include "Xml.h"
#include <thread>

namespace snap::ed {

namespace {

enum Provider { OpenAI = 0, Gemini, Anthropic, Compatible, Local, ProviderCount };

const wchar_t* ProviderName(int p) {
    static const wchar_t* n[] = { L"OpenAI", L"Google Gemini", L"Anthropic", TR_NOOP(L"Compatível com OpenAI"), TR_NOOP(L"Modelo local (Ollama)") };
    return Tr(n[std::clamp(p, 0, ProviderCount - 1)]);
}

const wchar_t* DefaultModel(int p) {
    switch (p) {
    case OpenAI: return L"gpt-4o-mini";
    case Gemini: return L"gemini-2.5-flash";
    case Anthropic: return L"claude-opus-5-5";
    case Local: return L"llava";
    default: return L"";
    }
}

const wchar_t* DefaultEndpoint(int p) {
    switch (p) {
    case OpenAI: return L"https://api.openai.com/v1";
    case Gemini: return L"https://generativelanguage.googleapis.com/v1beta";
    case Anthropic: return L"https://api.anthropic.com/v1";
    case Local: return L"http://localhost:11434/v1";
    default: return L"";
    }
}

struct AiSettings {
    bool enabled = false;
    int provider = OpenAI;
    std::wstring model, endpoint;
    std::wstring File() const { return AppDataDir() + L"\\AiSettings.xml"; }
    static AiSettings Load() {
        AiSettings s;
        XmlNode n;
        if (XmlLoadFile(s.File(), n)) {
            s.enabled = n.getb("Enabled");
            s.provider = std::clamp(n.geti("Provider", 0), 0, ProviderCount - 1);
            s.model = n.getW("Model");
            s.endpoint = n.getW("Endpoint");
        }
        if (s.model.empty()) s.model = DefaultModel(s.provider);
        if (s.endpoint.empty()) s.endpoint = DefaultEndpoint(s.provider);
        return s;
    }
    void Save() const {
        XmlNode n("AiSettings");
        n.seti("Enabled", enabled).seti("Provider", provider).setW("Model", model).setW("Endpoint", endpoint);
        XmlSaveFile(File(), n);
    }
    std::wstring SecretName() const { return L"AI/" + std::to_wstring(provider); }
    bool NeedsKey() const { return provider == OpenAI || provider == Gemini || provider == Anthropic; }
};

std::wstring HostOf(const std::wstring& url) {
    size_t a = url.find(L"://");
    a = a == std::wstring::npos ? 0 : a + 3;
    size_t b = url.find_first_of(L"/:?", a);
    return url.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
}

std::wstring Trimmed(std::wstring s) { while (!s.empty() && s.back() == L'/') s.pop_back(); return s; }

// one request; returns the answer text or throws the error text through 'err'
std::wstring Ask(const AiSettings& s, const std::wstring& key, const Image& img, const std::wstring& question, std::wstring& err) {
    Image im = img;
    int longSide = std::max(im.w, im.h);
    if (longSide > 2000) im = ScaleImage(im, std::max(1, im.w * 2000 / longSide), std::max(1, im.h * 2000 / longSide));
    std::vector<BYTE> png;
    if (!EncodeToMemory(im, ImageFormat::PNG, png)) { err = Tr(L"Não foi possível preparar a imagem."); return L""; }
    std::string b64 = Base64Encode(png);
    // answers in the interface language
    std::wstring langName;
    for (auto& l : i18n::Languages()) if (i18n::Current() == l.code) langName = l.name;
    std::string q = WToUtf8(question + (langName.empty() ? L"" : L"\n\n(Answer in " + langName + L".)"));
    using net::Json;
    std::wstring url;
    std::vector<std::wstring> headers = { L"Content-Type: application/json" };
    Json body = Json::Obj();
    if (s.provider == Gemini) {
        url = Trimmed(s.endpoint) + L"/models/" + s.model + L":generateContent";
        headers.push_back(L"x-goog-api-key: " + key);
        Json parts = Json::Arr();
        parts.Push(Json::Obj().Set("text", Json::Str(q)));
        parts.Push(Json::Obj().Set("inline_data", Json::Obj().Set("mime_type", Json::Str("image/png")).Set("data", Json::Str(b64))));
        body.Set("contents", Json::Arr().Push(Json::Obj().Set("parts", parts)));
    } else if (s.provider == Anthropic) {
        url = Trimmed(s.endpoint) + L"/messages";
        headers.push_back(L"x-api-key: " + key);
        headers.push_back(L"anthropic-version: 2023-06-01");
        Json content = Json::Arr();
        content.Push(Json::Obj().Set("type", Json::Str("image")).Set("source", Json::Obj().Set("type", Json::Str("base64"))
                                   .Set("media_type", Json::Str("image/png")).Set("data", Json::Str(b64))));
        content.Push(Json::Obj().Set("type", Json::Str("text")).Set("text", Json::Str(q)));
        body.Set("model", Json::Str(WToUtf8(s.model))).Set("max_tokens", Json::Num(2048))
            .Set("messages", Json::Arr().Push(Json::Obj().Set("role", Json::Str("user")).Set("content", content)));
    } else {                                    // OpenAI and compatible (also Ollama's /v1)
        url = Trimmed(s.endpoint) + L"/chat/completions";
        if (!key.empty()) headers.push_back(L"Authorization: Bearer " + key);
        Json content = Json::Arr();
        content.Push(Json::Obj().Set("type", Json::Str("text")).Set("text", Json::Str(q)));
        content.Push(Json::Obj().Set("type", Json::Str("image_url")).Set("image_url", Json::Obj().Set("url", Json::Str("data:image/png;base64," + b64))));
        body.Set("model", Json::Str(WToUtf8(s.model)))
            .Set("messages", Json::Arr().Push(Json::Obj().Set("role", Json::Str("user")).Set("content", content)));
    }
    net::Response r = net::Request(L"POST", url, headers, body.Dump(), 120000);
    if (r.status == 0) { err = Tr(L"Sem resposta do provedor: ") + r.error; return L""; }
    Json j;
    bool parsed = Json::Parse(r.body, j);
    if (!r.ok()) {
        std::string msg;
        if (parsed) {
            for (const char* p : { "error.message", "error.error.message", "message", "error" })
                if (const Json* e = j.Path(p)) { msg = e->Text(); if (!msg.empty()) break; }
        }
        err = Format(Tr(L"O provedor respondeu %d: "), r.status) + Utf8ToW(msg.empty() ? r.body.substr(0, 300) : msg);
        return L"";
    }
    if (!parsed) { err = Tr(L"Resposta inválida do provedor."); return L""; }
    const char* path = s.provider == Gemini ? "candidates[0].content.parts[0].text" : s.provider == Anthropic ? "content[0].text" : "choices[0].message.content";
    const Json* t = j.Path(path);
    std::string text = t ? t->Text() : "";
    if (text.empty()) { err = Tr(L"O provedor não devolveu texto."); return L""; }
    std::wstring w = Utf8ToW(text), out;
    for (wchar_t c : w) { if (c == L'\n' && (out.empty() || out.back() != L'\r')) out += L'\r'; out += c; }
    return out;
}

// ---------------------------------------------------------------- window
enum { IDC_Q = 10, IDC_ASK, IDC_ANSWER, IDC_COPY, IDC_ADD, IDC_SETTINGS, IDC_CLOSE, IDC_PRESET = 40 };
constexpr UINT WM_AI_DONE = WM_APP + 0x71;

struct AssistantState {
    Image img;
    AiSettings s;
    std::function<void(const std::wstring&)> addAsText;
    HWND h = nullptr, q = nullptr, answer = nullptr, notice = nullptr, status = nullptr;
    int dpi = 96;
    bool busy = false, done = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

struct AiResult { std::wstring text, err; };

const wchar_t* kPresets[] = {
    TR_NOOP(L"Explicar"), TR_NOOP(L"Descrever"), TR_NOOP(L"Extrair os passos"), TR_NOOP(L"Extrair o texto"),
};
const wchar_t* kPresetPrompts[] = {
    L"Explain what this screenshot shows and what the user is probably trying to do.",
    L"Describe this image in detail, suitable as alternative text.",
    L"List the steps shown or implied in this screenshot as a numbered list.",
    L"Transcribe all the text visible in this image, keeping the layout as much as possible.",
};

std::wstring NoticeText(const AiSettings& s) {
    return Format(Tr(L"A imagem e a pergunta serão enviadas para %s (%s). Nada é enviado até você perguntar."),
                  ProviderName(s.provider), HostOf(s.endpoint).c_str());
}

void Start(AssistantState* st, const std::wstring& question) {
    if (st->busy || Trim(question).empty()) return;
    std::wstring key = st->s.NeedsKey() || st->s.provider == Compatible ? net::LoadSecret(st->s.SecretName()) : L"";
    if (st->s.NeedsKey() && key.empty()) { SetWindowTextW(st->status, Tr(L"Configure a chave do provedor primeiro.")); return; }
    st->busy = true;
    SetWindowTextW(st->status, Tr(L"Perguntando…"));
    EnableWindow(GetDlgItem(st->h, IDC_ASK), FALSE);
    HWND h = st->h;
    AiSettings s = st->s;
    Image img = st->img;
    auto alive = st->alive;
    std::thread([h, s, key, img, question, alive] {
        auto* r = new AiResult;
        r->text = Ask(s, key, img, question, r->err);
        if (!*alive || !PostMessageW(h, WM_AI_DONE, 0, (LPARAM)r)) delete r;
    }).detach();
}

LRESULT CALLBACK AssistantProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = (AssistantState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    auto S = [&](int v) { return ui::Scale(v, st ? st->dpi : 96); };
    switch (m) {
    case WM_CREATE: {
        st = (AssistantState*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        st->h = h;
        HFONT f = ui::UiFont(st->dpi);
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
            HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
            return c;
        };
        st->notice = mk(L"STATIC", NoticeText(st->s).c_str(), SS_LEFT, 0);
        for (int i = 0; i < 4; i++) mk(L"BUTTON", Tr(kPresets[i]), BS_PUSHBUTTON | WS_TABSTOP, IDC_PRESET + i);
        st->q = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IDC_Q, WS_EX_CLIENTEDGE);
        SendMessageW(st->q, EM_SETCUEBANNER, TRUE, (LPARAM)Tr(L"Faça uma pergunta sobre a imagem…"));
        mk(L"BUTTON", Tr(L"Perguntar"), BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_ASK);
        st->answer = mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, IDC_ANSWER, WS_EX_CLIENTEDGE);
        st->status = mk(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0);
        mk(L"BUTTON", Tr(L"Copiar"), BS_PUSHBUTTON | WS_TABSTOP, IDC_COPY);
        mk(L"BUTTON", Tr(L"Adicionar como texto"), BS_PUSHBUTTON | WS_TABSTOP, IDC_ADD);
        mk(L"BUTTON", Tr(L"Configurações…"), BS_PUSHBUTTON | WS_TABSTOP, IDC_SETTINGS);
        mk(L"BUTTON", Tr(L"Fechar"), BS_PUSHBUTTON | WS_TABSTOP, IDC_CLOSE);
        return 0;
    }
    case WM_SIZE: {
        RECT cr; GetClientRect(h, &cr);
        int pad = S(12), bh = S(28), y = pad, W = cr.right - 2 * pad;
        MoveWindow(st->notice, pad, y, W, S(36), TRUE); y += S(40);
        int pw = (W - 3 * S(6)) / 4;
        for (int i = 0; i < 4; i++) MoveWindow(GetDlgItem(h, IDC_PRESET + i), pad + i * (pw + S(6)), y, pw, bh, TRUE);
        y += bh + S(8);
        MoveWindow(st->q, pad, y, W - S(116), bh, TRUE);
        MoveWindow(GetDlgItem(h, IDC_ASK), cr.right - pad - S(108), y, S(108), bh, TRUE);
        y += bh + S(8);
        int by = cr.bottom - pad - bh;
        MoveWindow(st->answer, pad, y, W, by - S(30) - y, TRUE);
        MoveWindow(st->status, pad, by - S(24), W, S(20), TRUE);
        int x = pad;
        for (int id : { IDC_COPY, IDC_ADD, IDC_SETTINGS }) {
            int bw = id == IDC_ADD ? S(160) : S(120);
            MoveWindow(GetDlgItem(h, id), x, by, bw, bh, TRUE); x += bw + S(6);
        }
        MoveWindow(GetDlgItem(h, IDC_CLOSE), cr.right - pad - S(90), by, S(90), bh, TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize = { 560, 420 }; return 0;
    case WM_CTLCOLORSTATIC:
        if ((HWND)l == st->answer) break;
        SetBkMode((HDC)w, TRANSPARENT);
        if ((HWND)l == st->notice) SetTextColor((HDC)w, RGB(160, 80, 0));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_AI_DONE: {
        std::unique_ptr<AiResult> r((AiResult*)l);
        st->busy = false;
        EnableWindow(GetDlgItem(h, IDC_ASK), TRUE);
        if (!r->err.empty()) { SetWindowTextW(st->status, r->err.c_str()); return 0; }
        SetWindowTextW(st->answer, r->text.c_str());
        SetWindowTextW(st->status, Format(Tr(L"Resposta de %s (%s)."), ProviderName(st->s.provider), st->s.model.c_str()).c_str());
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id >= IDC_PRESET && id < IDC_PRESET + 4) { Start(st, kPresetPrompts[id - IDC_PRESET]); return 0; }
        switch (id) {
        case IDC_ASK: case IDOK: {
            int n = GetWindowTextLengthW(st->q);
            std::wstring t(n, L'\0'); GetWindowTextW(st->q, t.data(), n + 1);
            Start(st, t);
            break;
        }
        case IDC_COPY: case IDC_ADD: {
            int n = GetWindowTextLengthW(st->answer);
            std::wstring t(n, L'\0'); GetWindowTextW(st->answer, t.data(), n + 1);
            if (Trim(t).empty()) break;
            std::wstring plain;
            for (wchar_t c : t) if (c != L'\r') plain += c;
            if (id == IDC_COPY) {
                clip::CopyText(h, plain);
                SetWindowTextW(st->status, Tr(L"Resposta copiada."));
            } else if (st->addAsText) { st->addAsText(plain); st->done = true; }
            break;
        }
        case IDC_SETTINGS:
            if (AssistantSettingsDialog(h)) { st->s = AiSettings::Load(); SetWindowTextW(st->notice, NoticeText(st->s).c_str()); }
            break;
        case IDC_CLOSE: case IDCANCEL: st->done = true; break;
        }
        return 0;
    }
    case WM_CLOSE: st->done = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

bool AssistantSettingsDialog(HWND owner) {
    AiSettings s = AiSettings::Load();
    using F = ui::Field;
    std::vector<std::wstring> provs;
    for (int i = 0; i < ProviderCount; i++) provs.push_back(ProviderName(i));
    bool hasKey = !net::LoadSecret(s.SecretName()).empty();
    const std::wstring keep = Tr(L"(guardada — deixe assim para manter)");
    std::vector<F> f = {
        F::MakeCheck(Tr(L"Ativar o assistente de IA"), s.enabled),
        F::MakeLabel(Tr(L"Ao perguntar, a imagem é enviada ao provedor escolhido. Use a sua própria chave (cobrança na sua conta).")),
        F::MakeCombo(Tr(L"Provedor"), provs, s.provider),
        F::MakeText(Tr(L"Modelo"), s.model),
        F::MakeText(Tr(L"Endereço da API"), s.endpoint),
        F::MakeText(Tr(L"Chave da API"), hasKey ? keep : L""),
        F::MakeLabel(Tr(L"A chave fica no Gerenciador de Credenciais do Windows, nunca em arquivos. Modelo local: Ollama em http://localhost:11434/v1 (sem chave).")),
    };
    int prevProvider = s.provider;
    auto validate = [&](std::vector<F>& v) -> std::wstring {
        // changing the provider fills its defaults (unless the user typed something else)
        if (v[2].value != prevProvider) {
            if (Trim(v[3].text).empty() || v[3].text == DefaultModel(prevProvider)) v[3].text = DefaultModel(v[2].value);
            if (Trim(v[4].text).empty() || v[4].text == DefaultEndpoint(prevProvider)) v[4].text = DefaultEndpoint(v[2].value);
        }
        if (v[0].value && Trim(v[4].text).empty()) return Tr(L"Informe o endereço da API.");
        if (v[0].value && Trim(v[3].text).empty() && v[2].value != Compatible) return Tr(L"Informe o modelo.");
        return L"";
    };
    if (!ui::RunForm(owner, Tr(L"Assistente de IA"), f, validate, 520)) return false;
    validate(f);
    s.enabled = f[0].value != 0;
    s.provider = f[2].value;
    s.model = Trim(f[3].text);
    s.endpoint = Trim(f[4].text);
    if (s.model.empty()) s.model = DefaultModel(s.provider);
    if (s.endpoint.empty()) s.endpoint = DefaultEndpoint(s.provider);
    std::wstring key = Trim(f[5].text);
    if (key != keep) {
        if (key.empty()) net::DeleteSecret(s.SecretName()); else net::SaveSecret(s.SecretName(), key);
    } else if (s.provider != prevProvider) {
        // the "kept" key belonged to the old provider: move it
        std::wstring old = net::LoadSecret(AiSettings{ false, prevProvider }.SecretName());
        if (!old.empty()) net::SaveSecret(s.SecretName(), old);
    }
    s.Save();
    return true;
}

void ShowAssistant(HWND owner, const Image& img, std::function<void(const std::wstring&)> addAsText) {
    AiSettings s = AiSettings::Load();
    if (!s.enabled) {
        if (MessageBoxW(owner, Tr(L"O assistente de IA está desligado. Ele envia a imagem para um provedor de IA escolhido por você, com a sua chave.\n\nConfigurar agora?"),
                        Tr(L"Assistente de IA"), MB_YESNO | MB_ICONINFORMATION) != IDYES) return;
        if (!AssistantSettingsDialog(owner)) return;
        s = AiSettings::Load();
        if (!s.enabled) return;
    }
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = AssistantProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"SnapnabAssistant";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        RegisterClassW(&wc);
        reg = true;
    }
    AssistantState st;
    st.img = img; st.s = s; st.addAsText = addAsText;
    st.dpi = owner ? (int)GetDpiForWindow(owner) : 96;
    RECT orc; GetWindowRect(owner ? owner : GetDesktopWindow(), &orc);
    int W = ui::Scale(640, st.dpi), H = ui::Scale(520, st.dpi);
    HWND h = CreateWindowExW(0, L"SnapnabAssistant", Tr(L"Assistente de IA"), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             (orc.left + orc.right - W) / 2, (orc.top + orc.bottom - H) / 2, W, H, owner, nullptr, GetModuleHandleW(nullptr), &st);
    if (!h) return;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    SetFocus(st.q);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { st.done = true; continue; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && msg.hwnd == st.q) { SendMessageW(h, WM_COMMAND, IDC_ASK, 0); continue; }
        if (IsDialogMessageW(h, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    *st.alive = false;                 // a request still running drops its answer
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    DestroyWindow(h);
}

} // namespace snap::ed
