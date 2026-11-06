// The C++ XML reader and writer as they were before they moved to Rust (rust/snapcore/src/xml.rs), kept only to check
// that the Rust one gives the same trees and the same bytes (tests.cpp, [17] "XML: Rust and the former C++").
#include "Common.h"
#include "Xml.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace snap::xmlref {

static void Escape(std::string& out, const std::string& s) {
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        case '\n': out += "&#10;"; break;
        case '\r': out += "&#13;"; break;
        case '\t': out += "&#9;"; break;
        default: out += c;
        }
    }
}

static void Write(std::string& out, const XmlNode& n, int depth) {
    out.append(depth * 2, ' ');
    out += '<'; out += n.name;
    for (auto& a : n.attrs) { out += ' '; out += a.first; out += "=\""; Escape(out, a.second); out += '"'; }
    if (n.children.empty() && n.text.empty()) { out += "/>\n"; return; }
    out += '>';
    if (!n.text.empty()) Escape(out, n.text);
    if (!n.children.empty()) {
        out += '\n';
        for (auto& c : n.children) Write(out, c, depth + 1);
        out.append(depth * 2, ' ');
    }
    out += "</"; out += n.name; out += ">\n";
}

std::string Serialize(const XmlNode& root, bool decl) {
    std::string out;
    if (decl) out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    Write(out, root, 0);
    return out;
}

namespace {
struct Parser {
    const std::string& s; size_t i = 0;
    explicit Parser(const std::string& t) : s(t) {}
    bool eof() const { return i >= s.size(); }
    void ws() { while (!eof() && (unsigned char)s[i] <= ' ') i++; }
    bool starts(const char* p) const { return s.compare(i, strlen(p), p) == 0; }
    std::string unescape(const std::string& v) {
        std::string r;
        for (size_t k = 0; k < v.size(); k++) {
            if (v[k] != '&') { r += v[k]; continue; }
            size_t e = v.find(';', k);
            if (e == std::string::npos) { r += v[k]; continue; }
            std::string ent = v.substr(k + 1, e - k - 1);
            if (ent == "amp") r += '&'; else if (ent == "lt") r += '<'; else if (ent == "gt") r += '>';
            else if (ent == "quot") r += '"'; else if (ent == "apos") r += '\'';
            else if (!ent.empty() && ent[0] == '#') {
                unsigned long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? strtoul(ent.c_str() + 2, nullptr, 16)
                                                                                     : strtoul(ent.c_str() + 1, nullptr, 10);
                std::wstring w; if (cp < 0x10000) w += (wchar_t)cp; else { cp -= 0x10000; w += (wchar_t)(0xD800 + (cp >> 10)); w += (wchar_t)(0xDC00 + (cp & 0x3FF)); }
                r += WToUtf8(w);
            } else { r += '&'; r += ent; r += ';'; }
            k = e;
        }
        return r;
    }
    void skipMisc() {
        for (;;) {
            ws();
            if (starts("<?")) { size_t e = s.find("?>", i); i = e == std::string::npos ? s.size() : e + 2; }
            else if (starts("<!--")) { size_t e = s.find("-->", i); i = e == std::string::npos ? s.size() : e + 3; }
            else if (starts("<!")) { size_t e = s.find('>', i); i = e == std::string::npos ? s.size() : e + 1; }
            else break;
        }
    }
    std::string name() {
        size_t b = i;
        while (!eof() && !strchr(" \t\r\n/>=", s[i])) i++;
        return s.substr(b, i - b);
    }
    bool element(XmlNode& n) {
        skipMisc();
        if (eof() || s[i] != '<') return false;
        i++;
        n.name = name();
        for (;;) {
            ws();
            if (eof()) return false;
            if (s[i] == '/') { i += 2; return true; }
            if (s[i] == '>') { i++; break; }
            std::string k = name();
            ws(); if (eof() || s[i] != '=') return false; i++; ws();
            char q = s[i++];
            size_t e = s.find(q, i);
            if (e == std::string::npos) return false;
            n.attrs.emplace_back(k, unescape(s.substr(i, e - i)));
            i = e + 1;
        }
        for (;;) {
            size_t b = i;
            while (!eof() && s[i] != '<') i++;
            std::string t = s.substr(b, i - b);
            if (t.find_first_not_of(" \t\r\n") != std::string::npos) n.text += unescape(t);
            if (eof()) return false;
            if (starts("</")) { size_t e = s.find('>', i); i = e == std::string::npos ? s.size() : e + 1; return true; }
            if (starts("<!--") || starts("<?")) { skipMisc(); continue; }
            if (starts("<![CDATA[")) { size_t e = s.find("]]>", i); n.text += s.substr(i + 9, e - i - 9); i = e + 3; continue; }
            XmlNode c;
            if (!element(c)) return false;
            n.children.push_back(std::move(c));
        }
    }
};
}

bool Parse(const std::string& text, XmlNode& root) {
    Parser p(text);
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF) p.i = 3;   // BOM
    root = XmlNode();
    return p.element(root);
}


} // namespace snap::xmlref
