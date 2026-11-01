// PDF export (≙ CPaintDocument::SaveAs → PDF::PDFPage::SavePDFDocument, ds_pseudocode §10.1)
//   A4 or Letter page according to the measurement system (USSystem), orientation from the image shape,
//   margin 0, image centered and fitted (ImageLayout 0); image written as JPEG (DCTDecode)
//   Links captured with the image (UI Automation) become /Link annotations with a /URI action
//   Long images (scrolling captures) are split across pages without cutting text lines (C34, SplitLongPages)
#include "Document.h"
#include "EditorTools.h"

namespace snap::ed {

// URI as a PDF literal string: ASCII, with the characters outside it percent-encoded and ( ) \ escaped
static std::string PdfUri(const std::wstring& url) {
    std::string u = WToUtf8(url), out;
    for (unsigned char c : u) {
        if (c < 0x21 || c > 0x7E) out += FormatA("%%%02X", c);
        else if (c == '(' || c == ')' || c == '\\') { out += '\\'; out += (char)c; }
        else out += (char)c;
    }
    return out;
}

bool SplitLongPages() { return RegGetDword(L"SplitLongPages", 1) != 0; }

bool SavePdf(const std::wstring& path, const Image& imgIn, std::wstring& err, const std::vector<PdfLink>& links) {
    if (imgIn.empty()) { err = Tr(L"Imagem vazia."); return false; }
    DWORD measure = 0;
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER, (LPWSTR)&measure, sizeof(measure) / 2);
    bool us = measure == 1;
    const double pw0 = us ? 612 : 595.28, ph0 = us ? 792 : 841.89;    // Letter : A4 (points), portrait
    // pages: a long image (scrolling capture) is split across portrait pages at the width of the page, with the breaks
    // moved to quiet rows so no text line is cut (C34); anything else is one page fitted and centered
    struct Page { RECT src; double pw, ph, s, ix, iy; std::vector<PdfLink> links; };
    std::vector<Page> pages;
    if (SplitLongPages() && imgIn.h > imgIn.w * (ph0 / pw0) * 1.25) {
        double s = pw0 / imgIn.w;
        std::vector<int> cuts = SmartBreaks(imgIn, std::max(64, (int)(ph0 / s)));
        cuts.push_back(imgIn.h);
        int prev = 0;
        for (int c : cuts) {
            if (c <= prev) continue;
            pages.push_back({ { 0, prev, imgIn.w, c }, pw0, ph0, s, 0, ph0 - (c - prev) * s, {} });
            prev = c;
        }
    } else {
        double pw = pw0, ph = ph0;
        if (imgIn.w > imgIn.h) std::swap(pw, ph);                     // landscape
        double s = std::min(pw / imgIn.w, ph / imgIn.h);
        pages.push_back({ { 0, 0, imgIn.w, imgIn.h }, pw, ph, s, (pw - imgIn.w * s) / 2, (ph - imgIn.h * s) / 2, {} });
    }
    // link annotations: only the ones with a web or e-mail address, cut to each page's part of the image
    for (auto& l : links) {
        bool web = l.url.rfind(L"http://", 0) == 0 || l.url.rfind(L"https://", 0) == 0 || l.url.rfind(L"mailto:", 0) == 0;
        if (!web) continue;
        for (auto& p : pages) {
            Gdiplus::RectF r = l.r, part((float)p.src.left, (float)p.src.top, (float)(p.src.right - p.src.left), (float)(p.src.bottom - p.src.top));
            Gdiplus::RectF::Intersect(r, r, part);
            if (r.Width >= 1 && r.Height >= 1) { r.Y -= (float)p.src.top; p.links.push_back({ r, l.url }); }
        }
    }
    // object numbers: 1 catalog, 2 page tree, then page / image / contents for each page, then the annotations, the info
    const size_t n = pages.size();
    size_t annotNo = 3 + 3 * n;
    std::vector<std::vector<size_t>> annotIds(n);
    for (size_t i = 0; i < n; i++) for (size_t k = 0; k < pages[i].links.size(); k++) annotIds[i].push_back(annotNo++);

    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offs;
    auto obj = [&](const std::string& body) { offs.push_back(out.size()); out += FormatA("%zu 0 obj\n", offs.size()) + body + "\nendobj\n"; };
    obj("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (size_t i = 0; i < n; i++) kids += FormatA("%s%zu 0 R", i ? " " : "", 3 + 3 * i);
    obj(FormatA("<< /Type /Pages /Kids [%s] /Count %zu >>", kids.c_str(), n));
    for (size_t i = 0; i < n; i++) {
        const Page& p = pages[i];
        Image part = n == 1 ? imgIn : imgIn.Crop(p.src);
        std::vector<BYTE> jpg;
        if (!EncodeToMemory(part, ImageFormat::JPEG, jpg, 92)) { err = Tr(L"Falha ao codificar a imagem."); return false; }
        std::string annots;
        for (size_t k = 0; k < annotIds[i].size(); k++) annots += FormatA("%s%zu 0 R", k ? " " : "", annotIds[i][k]);
        size_t img = 4 + 3 * i, contents = 5 + 3 * i;
        obj(FormatA("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Resources << /XObject << /Im0 %zu 0 R >> >> /Contents %zu 0 R%s >>",
                    p.pw, p.ph, img, contents, annots.empty() ? "" : (" /Annots [" + annots + "]").c_str()));
        offs.push_back(out.size());
        out += FormatA("%zu 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length %zu >>\nstream\n",
                       img, part.w, part.h, jpg.size());
        out.append((const char*)jpg.data(), jpg.size());
        out += "\nendstream\nendobj\n";
        std::string content = FormatA("q\n%.3f 0 0 %.3f %.3f %.3f cm\n/Im0 Do\nQ\n", part.w * p.s, part.h * p.s, p.ix, p.iy);
        obj(FormatA("<< /Length %zu >>\nstream\n", content.size()) + content + "endstream");
    }
    for (auto& p : pages) {
        int partH = p.src.bottom - p.src.top;
        for (auto& l : p.links) {               // image pixels → page points (origin at the bottom left)
            double x0 = p.ix + l.r.X * p.s, x1 = p.ix + l.r.GetRight() * p.s;
            double y1 = p.iy + (partH - l.r.Y) * p.s, y0 = p.iy + (partH - l.r.GetBottom()) * p.s;
            obj(FormatA("<< /Type /Annot /Subtype /Link /Rect [%.2f %.2f %.2f %.2f] /Border [0 0 0] /A << /S /URI /URI (%s) >> >>", x0, y0, x1, y1, PdfUri(l.url).c_str()));
        }
    }
    std::string title = WToUtf8(FileNameOf(path));
    for (auto& c : title) if (c == '(' || c == ')' || c == '\\') c = '_';
    obj("<< /Producer (Snapnab) /Title (" + title + ") >>");
    size_t xref = out.size();
    out += FormatA("xref\n0 %zu\n0000000000 65535 f \n", offs.size() + 1);
    for (size_t o : offs) out += FormatA("%010zu 00000 n \n", o);
    out += FormatA("trailer\n<< /Size %zu /Root 1 0 R /Info %zu 0 R >>\nstartxref\n%zu\n%%%%EOF\n", offs.size() + 1, offs.size(), xref);

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = Tr(L"Não foi possível criar o arquivo."); return false; }
    DWORD wr = 0;
    BOOL ok = WriteFile(h, out.data(), (DWORD)out.size(), &wr, nullptr);
    CloseHandle(h);
    if (!ok || wr != out.size()) { err = Tr(L"Falha ao gravar o arquivo."); return false; }
    return true;
}

// ---------------------------------------------------------------- long images across pages (C34; also Split image)
// how busy each row is: pixels that differ from the row's first pixel (a text line makes it busy)
static std::vector<int> RowBusyness(const Image& img) {
    std::vector<int> busy(img.h, 0);
    for (int y = 0; y < img.h; y++) {
        const uint32_t* r = img.row(y);
        uint32_t ref = r[0];
        int n = 0;
        for (int x = 0; x < img.w; x++)
            if (std::abs(CR(r[x]) - CR(ref)) + std::abs(CG(r[x]) - CG(ref)) + std::abs(CB(r[x]) - CB(ref)) > 40) n++;
        busy[y] = n;
    }
    return busy;
}

std::vector<int> SmartCuts(const Image& img, int parts) {
    std::vector<int> cuts;
    if (img.empty() || parts < 2) return cuts;
    std::vector<int> busy = RowBusyness(img);
    const int search = std::max(4, img.h / (parts * 4));
    for (int i = 1; i < parts; i++) {
        int target = (int)((long long)img.h * i / parts), best = target, bestScore = INT_MAX;
        for (int y = std::max(1, target - search); y <= std::min(img.h - 2, target + search); y++) {
            int s = busy[y] + busy[y - 1] + busy[y + 1];
            int score = s * 64 + std::abs(target - y);
            if (score < bestScore) { bestScore = score; best = y; }
        }
        if (cuts.empty() || best > cuts.back()) cuts.push_back(best);
    }
    return cuts;
}

std::vector<int> SmartBreaks(const Image& img, int pageH, int search) {
    std::vector<int> cuts;
    if (img.empty() || pageH < 16 || img.h <= pageH) return cuts;
    if (search < 0) search = pageH / 4;
    std::vector<int> busy = RowBusyness(img);
    int pos = 0;
    while (img.h - pos > pageH) {
        int target = pos + pageH, best = target, bestScore = INT_MAX;
        for (int y = target; y > std::max(pos + 16, target - search); y--) {
            // a quiet row with quiet neighbors is a gap between lines; nearer the page end is better
            int s = busy[y] + (y > 0 ? busy[y - 1] : 0) + (y + 1 < img.h ? busy[y + 1] : 0);
            int score = s * 64 + (target - y);
            if (score < bestScore) { bestScore = score; best = y; }
            if (s == 0 && target - y > 0 && bestScore <= target - y) break;
        }
        cuts.push_back(best);
        pos = best;
    }
    return cuts;
}

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
