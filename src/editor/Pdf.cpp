// PDF export (≙ CPaintDocument::SaveAs → PDF::PDFPage::SavePDFDocument, ds_pseudocode §10.1)
//   A4 or Letter page according to the measurement system (USSystem), orientation from the image shape,
//   margin 0, image centered and fitted (ImageLayout 0); image written as JPEG (DCTDecode)
//   Links captured with the image (UI Automation) become /Link annotations with a /URI action
//   Long images (scrolling captures) are split across pages without cutting text lines (C34, SplitLongPages)
#include "Document.h"
#include "EditorTools.h"
#include "SnapcoreBridge.h"

namespace snap::ed {

// the PDF is written by snapcore (rust/snapcore/src/pdf.rs)
bool SplitLongPages() { return snapcore_split_long_pages(); }

bool SavePdf(const std::wstring& path, const Image& img, std::wstring& err, const std::vector<PdfLink>& links) {
    std::vector<SnapcorePdfLink> ls;
    for (auto& l : links) ls.push_back({ l.r.X, l.r.Y, l.r.Width, l.r.Height, l.url.data(), l.url.size() });
    SnapcoreImageIn in = ImageIn(img);
    switch (snapcore_pdf_save(path.data(), path.size(), &in, ls.data(), ls.size())) {
    case 0: return true;
    case 1: err = Tr(L"Imagem vazia."); return false;
    case 3: err = Tr(L"Não foi possível criar o arquivo."); return false;
    case 4: err = Tr(L"Falha ao gravar o arquivo."); return false;
    default: err = Tr(L"Falha ao codificar a imagem."); return false;
    }
}

// ---------------------------------------------------------------- long images across pages (C34; also Split image)
static std::vector<int> Breaks(const Image& img, int pageH, int search, int parts) {
    SnapcoreImageIn in = ImageIn(img);
    std::vector<int> cuts(snapcore_page_breaks(&in, pageH, search, parts, nullptr, 0));
    if (!cuts.empty()) cuts.resize(snapcore_page_breaks(&in, pageH, search, parts, cuts.data(), cuts.size()));
    return cuts;
}

std::vector<int> SmartCuts(const Image& img, int parts) { return Breaks(img, 0, 0, std::max(0, parts)); }
std::vector<int> SmartBreaks(const Image& img, int pageH, int search) { return Breaks(img, pageH, search, -1); }

std::vector<Image> SplitImage(const Image& img, const std::vector<int>& cutsIn, bool vertical) {
    std::vector<Image> parts;
    std::vector<int> cuts = cutsIn;
    std::sort(cuts.begin(), cuts.end());
    int len = vertical ? img.w : img.h, prev = 0;
    cuts.push_back(len);
    for (int c : cuts) {
        c = std::clamp(c, 0, len);
        if (c - prev < 1) continue;
        parts.push_back(img.Crop(vertical ? RECT{ prev, 0, c, img.h } : RECT{ 0, prev, img.w, c }));
        prev = c;
    }
    return parts;
}

} // namespace snap::ed
