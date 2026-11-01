// Grab Text (OCR) — the original uses ABBYY FineReader Engine (FREngine.dll, ds_arch §13); here the engine is the
// native Windows OCR (Windows.Media.Ocr), called on a dedicated MTA thread. Used by the Editor and by the
// capture window's "Capture text" preset.
#include "Ocr.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Media.FaceAnalysis.h>
#include <winrt/Windows.Storage.Streams.h>

namespace snap {

// the image as the engine wants it (opaque, enlarged when small) and the factor back to the original pixels
static Image PrepareOcr(const Image& imgIn, double& k) {
    Image img(imgIn.w, imgIn.h, 0xFFFFFFFF);
    img.Blit(imgIn, 0, 0, true);
    k = 1;
    if (img.h < 500 && img.w < 3000) { img = ScaleImage(img, img.w * 2, img.h * 2); k = 2; }
    return img;
}

static winrt::Windows::Graphics::Imaging::SoftwareBitmap ToSoftwareBitmap(const Image& work) {
    using namespace winrt::Windows::Graphics::Imaging;
    SoftwareBitmap sb(BitmapPixelFormat::Bgra8, work.w, work.h, BitmapAlphaMode::Premultiplied);
    winrt::Windows::Storage::Streams::Buffer buf((uint32_t)work.px.size() * 4);
    memcpy(buf.data(), work.px.data(), work.px.size() * 4);
    buf.Length((uint32_t)work.px.size() * 4);
    sb.CopyFromBuffer(buf);
    return sb;
}

// Second pass for text the engine leaves out. Windows OCR drops a whole line when it holds a run like "1111" (the
// test card 4111 1111 1111 1111, for one), whatever the scale. Bands of the image that look like a line of text and
// that no recognized line covers are cut into words and read again word by word, and a word that still comes back
// empty is read two characters at a time (the engine reads "11" and "41" on their own). 'img' is the prepared image;
// 'f' maps its pixels back to the original. Bounded in bands and time: it runs whenever text is recognized.
static void RecoverMissedLines(winrt::Windows::Media::Ocr::OcrEngine const& engine, const Image& img, double f, std::vector<OcrLine>& lines) {
    if (img.w < 16 || img.h < 16 || (int64_t)img.w * img.h > 40'000'000) return;
    auto luma = [&](int x, int y) { uint32_t p = img.at(x, y); return (CR(p) * 30 + CG(p) * 59 + CB(p) * 11) / 100; };
    int hist[256] = {};
    for (int y = 0; y < img.h; y += 2) for (int x = 0; x < img.w; x += 2) hist[luma(x, y)]++;
    int bg = (int)(std::max_element(hist, hist + 256) - hist);              // the most common shade: the background
    auto ink = [&](int x, int y) { return std::abs(luma(x, y) - bg) > 80; };
    std::vector<char> covered(img.h, 0);
    for (auto& l : lines)
        for (int y = std::max(0, (int)(l.box.top / f)); y < std::min(img.h, (int)std::ceil(l.box.bottom / f)); y++) covered[y] = 1;
    std::vector<int> rowInk(img.h, 0);
    for (int y = 0; y < img.h; y++) if (!covered[y]) for (int x = 0; x < img.w; x++) rowInk[y] += ink(x, y);
    ULONGLONG start = GetTickCount64();
    int bands = 0;
    auto read = [&](const Image& piece) {
        std::wstring t;
        for (auto const& line : engine.RecognizeAsync(ToSoftwareBitmap(piece)).get().Lines()) {
            for (auto const& w : line.Words()) t += w.Text().c_str();
        }
        return t;
    };
    auto padded = [&](int x0, int y0, int x1, int y1) {                     // the piece on a margin of background
        int pad = std::max(8, (y1 - y0) / 2);
        Image out(x1 - x0 + 2 * pad, y1 - y0 + 2 * pad, 0xFF000000 | (uint32_t)(bg << 16 | bg << 8 | bg));
        out.Blit(img.Crop({ x0, y0, x1, y1 }), pad, pad, false);
        // a small piece is enlarged: the engine reads two loose characters only when they are about 48 px tall
        if (y1 - y0 < 48) { double s = 48.0 / (y1 - y0); out = ScaleImage(out, (int)(out.w * s), (int)(out.h * s)); }
        return out;
    };
    for (int y = 0; y < img.h && bands < 8 && GetTickCount64() - start < 1500;) {
        if (rowInk[y] == 0) { y++; continue; }
        int y0 = y;
        while (y < img.h && rowInk[y] > 0) y++;
        int y1 = y, h = y1 - y0;
        if (h < 10 || h > 120) continue;
        // glyphs: runs of columns with ink; words: glyphs closer than about 0.4 of the line height
        std::vector<std::pair<int, int>> glyphs;
        for (int x = 0; x < img.w;) {
            bool any = false;
            for (int yy = y0; yy < y1 && !any; yy++) any = ink(x, yy);
            if (!any) { x++; continue; }
            int gx = x;
            for (; x < img.w; x++) {
                bool col = false;
                for (int yy = y0; yy < y1 && !col; yy++) col = ink(x, yy);
                if (!col) break;
            }
            glyphs.push_back({ gx, x });
        }
        if (glyphs.size() < 4 || glyphs.size() > 200) continue;
        bool textLike = true;                                               // glyphs no wider than the line is tall
        for (auto& g : glyphs) textLike = textLike && g.second - g.first <= h * 1.5;
        if (!textLike) continue;
        bands++;
        std::vector<std::vector<std::pair<int, int>>> words(1);
        for (size_t i = 0; i < glyphs.size(); i++) {
            if (i && glyphs[i].first - glyphs[i - 1].second > h * 0.4) words.emplace_back();
            words.back().push_back(glyphs[i]);
        }
        std::vector<std::wstring> texts;
        bool anyWord = false;
        for (auto& wg : words) {
            texts.push_back(read(padded(wg.front().first, y0, wg.back().second, y1)));
            anyWord = anyWord || !texts.back().empty();
        }
        if (!anyWord) continue;                    // nothing readable at all: an icon row or a picture, not a line of text
        // consecutive unread words are read together, two glyphs at a time (a narrow "1" between wide gaps can come out
        // as a word of its own, and a single "1" is not read either)
        std::vector<std::vector<std::pair<int, int>>> groups;
        std::vector<std::wstring> groupText;
        for (size_t n = 0; n < words.size(); n++) {
            if (!texts[n].empty() || n == 0 || !texts[n - 1].empty()) { groups.push_back(words[n]); groupText.push_back(texts[n]); }
            else groups.back().insert(groups.back().end(), words[n].begin(), words[n].end());
        }
        OcrLine ol;
        RECT u{ LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
        double prevUsual = 0;
        int prevEnd = -1;
        for (size_t n = 0; n < groups.size(); n++) {
            auto& wg = groups[n];
            std::wstring t = groupText[n];
            // word break after glyph a: a gap clearly wider than the usual one between glyphs ("1"s stand far apart)
            std::vector<int> gaps;
            for (size_t a = 0; a + 1 < wg.size(); a++) gaps.push_back(wg[a + 1].first - wg[a].second);
            std::sort(gaps.begin(), gaps.end());
            double usual = gaps.empty() ? 0 : gaps[gaps.size() / 2];
            auto gap = [&](size_t a) { int g = wg[a + 1].first - wg[a].second; return g > h * 0.4 && g > usual * 1.6; };
            if (t.empty() && wg.size() <= 40)
                for (size_t i = 0; i < wg.size(); i += 2) {
                    size_t j = std::min(i + 1, wg.size() - 1);
                    std::wstring s = read(padded(wg[i].first, y0, wg[j].second, y1));
                    s.erase(std::remove(s.begin(), s.end(), L' '), s.end());
                    if (j > i && s.size() == 2 && gap(i)) s.insert(1, L" ");      // the pair spans two words
                    t += s;
                    if (!s.empty() && j + 1 < wg.size() && gap(j)) t += L' ';
                }
            while (!t.empty() && t.back() == L' ') t.pop_back();
            if (t.empty()) continue;
            size_t digits = std::count_if(t.begin(), t.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
            if (digits * 2 >= t.size())                                     // digits read as look-alike letters
                for (wchar_t& c : t) c = wcschr(L"Il|i!", c) ? L'1' : wcschr(L"Oo", c) ? L'0' : c;
            OcrWord ow;
            ow.text = t;
            ow.box = { (LONG)std::floor(wg.front().first * f), (LONG)std::floor(y0 * f), (LONG)std::ceil(wg.back().second * f), (LONG)std::ceil(y1 * f) };
            u.left = std::min(u.left, ow.box.left); u.top = std::min(u.top, ow.box.top);
            u.right = std::max(u.right, ow.box.right); u.bottom = std::max(u.bottom, ow.box.bottom);
            // pieces of one word read apart ("41" + "11") are joined without a space
            bool sameWord = prevEnd >= 0 && wg.front().first - prevEnd <= std::max(prevUsual, usual) * 1.6;
            ol.text += (ol.text.empty() || sameWord ? L"" : L" ") + t;
            ol.words.push_back(std::move(ow));
            prevUsual = usual; prevEnd = wg.back().second;
        }
        if (ol.words.empty()) continue;
        ol.box = u;
        auto at = std::find_if(lines.begin(), lines.end(), [&](const OcrLine& l) { return l.box.top > u.top; });
        lines.insert(at, std::move(ol));
    }
}

bool RunOcrLines(const Image& imgIn, std::vector<OcrLine>& lines, std::wstring& err) {
    lines.clear();
    if (imgIn.empty()) { err = Tr(L"Imagem vazia."); return false; }
    double k;
    Image img = PrepareOcr(imgIn, k);
    std::thread t([&] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::Ocr;
            uint32_t maxDim = OcrEngine::MaxImageDimension();
            Image work = img;
            double s = 1;
            if ((uint32_t)work.w > maxDim || (uint32_t)work.h > maxDim) {
                s = std::min((double)maxDim / work.w, (double)maxDim / work.h);
                work = ScaleImage(work, (int)(work.w * s), (int)(work.h * s));
            }
            SoftwareBitmap sb = ToSoftwareBitmap(work);
            OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) { err = Tr(L"Nenhum idioma de OCR instalado no Windows (Configurações › Hora e idioma › Idioma)."); return; }
            OcrResult res = engine.RecognizeAsync(sb).get();
            const double f = 1.0 / (k * s);
            for (auto const& line : res.Lines()) {
                OcrLine ol;
                ol.text = line.Text().c_str();
                RECT u{ LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
                for (auto const& w : line.Words()) {
                    auto r = w.BoundingRect();
                    OcrWord ow;
                    ow.text = w.Text().c_str();
                    ow.box = { (LONG)std::floor(r.X * f), (LONG)std::floor(r.Y * f), (LONG)std::ceil((r.X + r.Width) * f), (LONG)std::ceil((r.Y + r.Height) * f) };
                    u.left = std::min(u.left, ow.box.left); u.top = std::min(u.top, ow.box.top);
                    u.right = std::max(u.right, ow.box.right); u.bottom = std::max(u.bottom, ow.box.bottom);
                    ol.words.push_back(ow);
                }
                if (ol.words.empty()) continue;
                ol.box = u;
                lines.push_back(std::move(ol));
            }
            RecoverMissedLines(engine, work, f, lines);
        } catch (winrt::hresult_error const& e) {
            err = e.message().c_str();
        } catch (...) {
            err = Tr(L"Falha desconhecida no OCR.");
        }
        winrt::uninit_apartment();
    });
    t.join();
    return err.empty();
}

bool DetectFaces(const Image& imgIn, std::vector<RECT>& faces, std::wstring& err) {
    faces.clear();
    if (imgIn.empty()) return false;
    Image img(imgIn.w, imgIn.h, 0xFFFFFFFF);
    img.Blit(imgIn, 0, 0, true);
    double s = 1;
    if (img.w > 2000 || img.h > 2000) { s = std::min(2000.0 / img.w, 2000.0 / img.h); img = ScaleImage(img, (int)(img.w * s), (int)(img.h * s)); }
    std::thread t([&] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::FaceAnalysis;
            if (!FaceDetector::IsSupported()) { err = Tr(L"A detecção de rostos não está disponível neste Windows."); return; }
            SoftwareBitmap sb(BitmapPixelFormat::Bgra8, img.w, img.h, BitmapAlphaMode::Premultiplied);
            winrt::Windows::Storage::Streams::Buffer buf((uint32_t)img.px.size() * 4);
            memcpy(buf.data(), img.px.data(), img.px.size() * 4);
            buf.Length((uint32_t)img.px.size() * 4);
            sb.CopyFromBuffer(buf);
            SoftwareBitmap gray = SoftwareBitmap::Convert(sb, BitmapPixelFormat::Gray8);
            FaceDetector det = FaceDetector::CreateAsync().get();
            for (auto const& f : det.DetectFacesAsync(gray).get()) {
                auto b = f.FaceBox();
                faces.push_back({ (LONG)(b.X / s), (LONG)(b.Y / s), (LONG)((b.X + b.Width) / s), (LONG)((b.Y + b.Height) / s) });
            }
        } catch (winrt::hresult_error const& e) {
            err = e.message().c_str();
        } catch (...) {
            err = Tr(L"Falha na detecção de rostos.");
        }
        winrt::uninit_apartment();
    });
    t.join();
    return err.empty();
}

bool RunOcr(const Image& imgIn, std::wstring& text, std::wstring& err) {
    if (imgIn.empty()) { err = Tr(L"Imagem vazia."); return false; }
    // opaque background and scaling: small text is enlarged, huge images are reduced
    Image img(imgIn.w, imgIn.h, 0xFFFFFFFF);
    img.Blit(imgIn, 0, 0, true);
    if (img.h < 500 && img.w < 3000) img = ScaleImage(img, img.w * 2, img.h * 2);
    std::thread t([&] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::Ocr;
            uint32_t maxDim = OcrEngine::MaxImageDimension();
            Image work = img;
            if ((uint32_t)work.w > maxDim || (uint32_t)work.h > maxDim) {
                double s = std::min((double)maxDim / work.w, (double)maxDim / work.h);
                work = ScaleImage(work, (int)(work.w * s), (int)(work.h * s));
            }
            SoftwareBitmap sb(BitmapPixelFormat::Bgra8, work.w, work.h, BitmapAlphaMode::Premultiplied);
            winrt::Windows::Storage::Streams::Buffer buf((uint32_t)work.px.size() * 4);
            memcpy(buf.data(), work.px.data(), work.px.size() * 4);
            buf.Length((uint32_t)work.px.size() * 4);
            sb.CopyFromBuffer(buf);
            OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) { err = Tr(L"Nenhum idioma de OCR instalado no Windows (Configurações › Hora e idioma › Idioma)."); return; }
            OcrResult res = engine.RecognizeAsync(sb).get();
            std::wstring out;
            for (auto const& line : res.Lines()) { out += line.Text().c_str(); out += L"\n"; }
            text = out;
        } catch (winrt::hresult_error const& e) {
            err = e.message().c_str();
        } catch (...) {
            err = Tr(L"Falha desconhecida no OCR.");
        }
        winrt::uninit_apartment();
    });
    t.join();
    return err.empty();
}

} // namespace snap
