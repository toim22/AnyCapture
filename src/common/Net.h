// Network helpers for the optional upload (C21) and the AI assistant (C22): HTTPS through WinHTTP, SHA-256/HMAC
// (BCrypt), a small JSON reader/writer and secrets in the Windows Credential Manager. Nothing here runs unless the user
// turns on a feature that needs the internet.
#pragma once
#include "Common.h"

namespace snap::net {

// ---- crypto
std::vector<BYTE> Sha256(const void* data, size_t len);
std::vector<BYTE> HmacSha256(const std::vector<BYTE>& key, const std::string& msg);
std::string Hex(const std::vector<BYTE>& d);                   // lowercase
std::string Base64Url(const std::vector<BYTE>& d);             // RFC 4648 §5, no padding (PKCE)
std::vector<BYTE> RandomBytes(size_t n);
std::string UrlEncode(const std::string& s, bool keepSlash = false);    // RFC 3986 unreserved kept

// ---- HTTP
struct Response {
    int status = 0;                  // 0 = no answer (network error)
    std::string body;
    std::map<std::wstring, std::wstring> headers;   // lowercase names
    std::wstring error;              // WinHTTP failure text
    std::wstring url;                // final URL
    bool ok() const { return status >= 200 && status < 300; }
};
// 'headers' as "Name: value" lines; 'body' sent as is
Response Request(const std::wstring& method, const std::wstring& url, const std::vector<std::wstring>& headers,
                 const std::string& body = "", int timeoutMs = 60000);

// multipart/form-data body: text fields and one file part
struct Multipart {
    std::string boundary;
    std::string data;
    Multipart();
    void Field(const std::string& name, const std::string& value);
    void File(const std::string& name, const std::string& fileName, const std::string& contentType, const std::string& bytes);
    std::string Finish();            // closes the body and returns it
    std::wstring ContentType() const;
};

// ---- JSON (enough for API answers and requests)
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;                 // UTF-8
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    static bool Parse(const std::string& text, Json& out);
    std::string Dump() const;
    const Json* Get(const std::string& key) const;          // object member
    // path like "choices[0].message.content" or "$.data.link" (JSONPath-style subset)
    const Json* Path(const std::string& path) const;
    std::string Text() const;        // string value, or number/bool as text, "" otherwise
    // builders
    static Json Str(const std::string& s) { Json j; j.type = String; j.str = s; return j; }
    static Json Num(double v) { Json j; j.type = Number; j.num = v; return j; }
    static Json Boolean(bool v) { Json j; j.type = Bool; j.b = v; return j; }
    static Json Obj() { Json j; j.type = Object; return j; }
    static Json Arr() { Json j; j.type = Array; return j; }
    Json& Set(const std::string& k, Json v) { type = Object; obj.push_back({ k, std::move(v) }); return *this; }
    Json& Push(Json v) { type = Array; arr.push_back(std::move(v)); return *this; }
};
std::string JsonEscape(const std::string& s);

// ---- secrets (Windows Credential Manager, generic credential "Snapnab/<name>")
bool SaveSecret(const std::wstring& name, const std::wstring& value);
std::wstring LoadSecret(const std::wstring& name);
void DeleteSecret(const std::wstring& name);

} // namespace snap::net
