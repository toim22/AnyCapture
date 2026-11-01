// Text recognition (Grab Text) with the native Windows OCR
#pragma once
#include "Image.h"

namespace snap {

bool RunOcr(const Image& img, std::wstring& text, std::wstring& err);

// words with their boxes (image pixels): automatic redaction (C11), smart highlighter and text-only blur (C12)
struct OcrWord { std::wstring text; RECT box{}; };
struct OcrLine { std::wstring text; RECT box{}; std::vector<OcrWord> words; };
bool RunOcrLines(const Image& img, std::vector<OcrLine>& lines, std::wstring& err);

// faces found by Windows (Windows.Media.FaceAnalysis), for the automatic redaction (C11)
bool DetectFaces(const Image& img, std::vector<RECT>& faces, std::wstring& err);

} // namespace snap
