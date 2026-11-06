// Grab Text (OCR) and face detection: the work is done by snapcore (rust/snapcore/src/ocr.rs) with the native Windows
// OCR (Windows.Media.Ocr) and Windows.Media.FaceAnalysis; these are the C++ entry points. The original uses ABBYY
// FineReader Engine (FREngine.dll, ds_arch §13).
#include "Ocr.h"
#include "SnapcoreBridge.h"

namespace snap {

namespace {
RECT R(SnapcoreRect r) { return { r.left, r.top, r.right, r.bottom }; }

// the message of a result that is not 0
std::wstring OcrMessage(int32_t code, SnapcoreWStr& msg) {
    std::wstring m = msg.len ? std::wstring(msg.data, msg.len) : std::wstring();
    if (msg.data) snapcore_wstr_free(msg);
    switch (code) {
    case 1: return Tr(L"Imagem vazia.");
    case 2: return Tr(L"Nenhum idioma de OCR instalado no Windows (Configurações › Hora e idioma › Idioma).");
    case 3: return Tr(L"Falha desconhecida no OCR.");
    case 4: return Tr(L"A detecção de rostos não está disponível neste Windows.");
    case 5: return Tr(L"Falha na detecção de rostos.");
    default: return m;
    }
}
} // namespace

bool RunOcr(const Image& img, std::wstring& text, std::wstring& err) {
    SnapcoreImageIn in = ImageIn(img);
    SnapcoreWStr t{}, msg{};
    int32_t code = snapcore_ocr_text(&in, &t, &msg);
    if (code != 0) { err = OcrMessage(code, msg); return false; }
    text = t.len ? std::wstring(t.data, t.len) : std::wstring();
    if (t.data) snapcore_wstr_free(t);
    return true;
}

bool RunOcrLines(const Image& img, std::vector<OcrLine>& lines, std::wstring& err) {
    lines.clear();
    SnapcoreImageIn in = ImageIn(img);
    SnapcoreWStr msg{};
    auto sink = [](void* ctx, const wchar_t* text, size_t len, SnapcoreRect r, bool word) {
        auto& ls = *(std::vector<OcrLine>*)ctx;
        if (word && !ls.empty()) ls.back().words.push_back({ std::wstring(text, len), R(r) });
        else if (!word) ls.push_back({ std::wstring(text, len), R(r), {} });
    };
    int32_t code = snapcore_ocr_lines(&in, sink, &lines, &msg);
    if (code != 0) { err = OcrMessage(code, msg); return false; }
    return true;
}

bool DetectFaces(const Image& img, std::vector<RECT>& faces, std::wstring& err) {
    faces.clear();
    if (img.empty()) return false;
    SnapcoreImageIn in = ImageIn(img);
    SnapcoreWStr msg{};
    auto sink = [](void* ctx, SnapcoreRect r) { ((std::vector<RECT>*)ctx)->push_back(R(r)); };
    int32_t code = snapcore_detect_faces(&in, sink, &faces, &msg);
    if (code != 0) { err = OcrMessage(code, msg); return false; }
    return true;
}

} // namespace snap
