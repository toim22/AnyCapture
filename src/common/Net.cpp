#include "Net.h"
#include <bcrypt.h>
#include <winhttp.h>
#include <wincred.h>
#include <cmath>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

namespace snap::net {

// ---------------------------------------------------------------- crypto
static std::vector<BYTE> Hash(const void* data, size_t len, const std::vector<BYTE>* key) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::vector<BYTE> out(32);
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, key ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0) != 0) return {};
    if (BCryptCreateHash(alg, &h, nullptr, 0, key ? (PUCHAR)key->data() : nullptr, key ? (ULONG)key->size() : 0, 0) == 0) {
        BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0);
        BCryptFinishHash(h, out.data(), 32, 0);
        BCryptDestroyHash(h);
    } else out.clear();
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

std::vector<BYTE> Sha256(const void* data, size_t len) { return Hash(data, len, nullptr); }
std::vector<BYTE> HmacSha256(const std::vector<BYTE>& key, const std::string& msg) { return Hash(msg.data(), msg.size(), &key); }

std::string Hex(const std::vector<BYTE>& d) {
    static const char* x = "0123456789abcdef";
    std::string s;
    for (BYTE b : d) { s += x[b >> 4]; s += x[b & 15]; }
    return s;
}

std::string Base64Url(const std::vector<BYTE>& d) {
    std::string s = Base64Encode(d);
    for (auto& c : s) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
    while (!s.empty() && s.back() == '=') s.pop_back();
    return s;
}

std::vector<BYTE> RandomBytes(size_t n) {
    std::vector<BYTE> v(n);
    BCryptGenRandom(nullptr, v.data(), (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return v;
}

std::string UrlEncode(const std::string& s, bool keepSlash) {
    static const char* x = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || (keepSlash && c == '/')) o += (char)c;
        else { o += '%'; o += x[c >> 4]; o += x[c & 15]; }
    }
    return o;
}

// ---------------------------------------------------------------- HTTP
Response Request(const std::wstring& method, const std::wstring& url, const std::vector<std::wstring>& headers, const std::string& body, int timeoutMs) {
    Response r;
    r.url = url;
    URL_COMPONENTS uc{ sizeof uc };
    wchar_t host[512] = L"", path[4096] = L"";
    uc.lpszHostName = host; uc.dwHostNameLength = 512;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    wchar_t extra[8192] = L"";
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 8192;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { r.error = L"Invalid URL"; return r; }
    HINTERNET s = WinHttpOpen((L"Snapnab/" + std::wstring(kAppVersion)).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { r.error = L"WinHttpOpen failed"; return r; }
    WinHttpSetTimeouts(s, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    std::wstring target = std::wstring(path) + extra;
    HINTERNET q = c ? WinHttpOpenRequest(c, method.c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (!q) { r.error = Format(L"Connection failed (%u)", GetLastError()); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s); return r; }
    std::wstring hs;
    for (auto& h : headers) hs += h + L"\r\n";
    BOOL sent = WinHttpSendRequest(q, hs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hs.c_str(), hs.empty() ? 0 : (DWORD)-1L,
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (sent && WinHttpReceiveResponse(q, nullptr)) {
        DWORD code = 0, sz = sizeof code;
        WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        r.status = (int)code;
        DWORD hl = 0;
        WinHttpQueryHeaders(q, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &hl, WINHTTP_NO_HEADER_INDEX);
        if (hl) {
            std::wstring raw(hl / 2, L'\0');
            if (WinHttpQueryHeaders(q, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &hl, WINHTTP_NO_HEADER_INDEX)) {
                size_t i = 0;
                while (i < raw.size()) {
                    size_t e = raw.find(L"\r\n", i);
                    std::wstring line = raw.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
                    size_t colon = line.find(L':');
                    if (colon != std::wstring::npos) r.headers[ToLower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
                    if (e == std::wstring::npos) break;
                    i = e + 2;
                }
            }
        }
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(q, &avail) || !avail) break;
            std::string buf(avail, '\0');
            DWORD rd = 0;
            if (!WinHttpReadData(q, buf.data(), avail, &rd) || !rd) break;
            r.body.append(buf.data(), rd);
        }
    } else r.error = Format(L"Request failed (%u)", GetLastError());
    WinHttpCloseHandle(q); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return r;
}

Multipart::Multipart() { boundary = "----SnapnabBoundary" + Hex(RandomBytes(12)); }
void Multipart::Field(const std::string& name, const std::string& value) {
    data += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
}
void Multipart::File(const std::string& name, const std::string& fileName, const std::string& ct, const std::string& bytes) {
    data += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"; filename=\"" + fileName + "\"\r\nContent-Type: " + ct + "\r\n\r\n";
    data += bytes;
    data += "\r\n";
}
std::string Multipart::Finish() { return data + "--" + boundary + "--\r\n"; }
std::wstring Multipart::ContentType() const { return L"Content-Type: multipart/form-data; boundary=" + Utf8ToW(boundary); }

// ---------------------------------------------------------------- JSON
namespace {
struct Parser {
    const std::string& s; size_t i = 0;
    void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
    bool lit(const char* t) { size_t n = strlen(t); if (s.compare(i, n, t) == 0) { i += n; return true; } return false; }
    bool str(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
            case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > s.size()) return false;
                unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16); i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
                else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 63)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                break;
            }
            default: out += e;
            }
        }
        if (i >= s.size()) return false;
        i++;
        return true;
    }
    bool val(Json& v, int depth = 0) {
        if (depth > 200) return false;
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') {
            i++; v.type = Json::Object; ws();
            if (i < s.size() && s[i] == '}') { i++; return true; }
            for (;;) {
                ws(); std::string k;
                if (!str(k)) return false;
                ws(); if (i >= s.size() || s[i] != ':') return false; i++;
                Json m; if (!val(m, depth + 1)) return false;
                v.obj.push_back({ k, std::move(m) });
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; return true; }
                return false;
            }
        }
        if (c == '[') {
            i++; v.type = Json::Array; ws();
            if (i < s.size() && s[i] == ']') { i++; return true; }
            for (;;) {
                Json m; if (!val(m, depth + 1)) return false;
                v.arr.push_back(std::move(m));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; return true; }
                return false;
            }
        }
        if (c == '"') { v.type = Json::String; return str(v.str); }
        if (lit("true")) { v.type = Json::Bool; v.b = true; return true; }
        if (lit("false")) { v.type = Json::Bool; v.b = false; return true; }
        if (lit("null")) { v.type = Json::Null; return true; }
        char* end = nullptr;
        v.num = strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        i = end - s.c_str();
        v.type = Json::Number;
        return true;
    }
};
}

bool Json::Parse(const std::string& text, Json& out) {
    Parser p{ text };
    out = Json();
    if (!p.val(out)) return false;
    p.ws();
    return p.i == text.size();
}

std::string JsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
        default: if (c < 0x20) o += FormatA("\\u%04x", c); else o += (char)c;
        }
    }
    return o;
}

std::string Json::Dump() const {
    switch (type) {
    case Bool: return b ? "true" : "false";
    case Number: return std::fabs(num - std::round(num)) < 1e-9 && std::fabs(num) < 1e15 ? FormatA("%lld", (long long)std::llround(num)) : FormatA("%.17g", num);
    case String: return "\"" + JsonEscape(str) + "\"";
    case Array: { std::string o = "["; for (size_t k = 0; k < arr.size(); k++) o += (k ? "," : "") + arr[k].Dump(); return o + "]"; }
    case Object: { std::string o = "{"; for (size_t k = 0; k < obj.size(); k++) o += (k ? ",\"" : "\"") + JsonEscape(obj[k].first) + "\":" + obj[k].second.Dump(); return o + "}"; }
    default: return "null";
    }
}

const Json* Json::Get(const std::string& key) const {
    if (type != Object) return nullptr;
    for (auto& [k, v] : obj) if (k == key) return &v;
    return nullptr;
}

const Json* Json::Path(const std::string& pathIn) const {
    std::string p = pathIn;
    if (p.rfind("$.", 0) == 0) p = p.substr(2); else if (p == "$") return this;
    const Json* cur = this;
    size_t i = 0;
    while (cur && i < p.size()) {
        if (p[i] == '.') { i++; continue; }
        if (p[i] == '[') {
            size_t e = p.find(']', i);
            if (e == std::string::npos) return nullptr;
            std::string idx = p.substr(i + 1, e - i - 1);
            if (!idx.empty() && (idx[0] == '\'' || idx[0] == '"')) cur = cur->Get(idx.substr(1, idx.size() - 2));
            else { size_t n = (size_t)atoi(idx.c_str()); cur = cur->type == Array && n < cur->arr.size() ? &cur->arr[n] : nullptr; }
            i = e + 1;
            continue;
        }
        size_t e = p.find_first_of(".[", i);
        cur = cur->Get(p.substr(i, e == std::string::npos ? std::string::npos : e - i));
        i = e == std::string::npos ? p.size() : e;
    }
    return cur;
}

std::string Json::Text() const {
    switch (type) {
    case String: return str;
    case Number: return Dump();
    case Bool: return b ? "true" : "false";
    default: return "";
    }
}

// ---------------------------------------------------------------- secrets
bool SaveSecret(const std::wstring& name, const std::wstring& value) {
    std::wstring target = L"Snapnab/" + name;
    std::string blob = WToUtf8(value);
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = (LPWSTR)target.c_str();
    c.CredentialBlobSize = (DWORD)blob.size();
    c.CredentialBlob = (LPBYTE)blob.data();
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.UserName = (LPWSTR)L"Snapnab";
    return CredWriteW(&c, 0) != FALSE;
}

std::wstring LoadSecret(const std::wstring& name) {
    PCREDENTIALW c = nullptr;
    std::wstring target = L"Snapnab/" + name;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &c) || !c) return L"";
    std::wstring v = Utf8ToW(std::string((const char*)c->CredentialBlob, c->CredentialBlobSize));
    CredFree(c);
    return v;
}

void DeleteSecret(const std::wstring& name) { CredDeleteW((L"Snapnab/" + name).c_str(), CRED_TYPE_GENERIC, 0); }

} // namespace snap::net
