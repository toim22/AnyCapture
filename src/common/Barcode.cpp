#include "Barcode.h"
#include "Clipboard.h"
#include "ReadBarcode.h"

namespace snap::barcode {

std::vector<Result> Read(const Image& imgIn) {
    std::vector<Result> out;
    if (imgIn.empty()) return out;
    // transparent pixels read as white (codes are dark on light)
    Image img = imgIn;
    for (auto& p : img.px) if (CA(p) < 255) p = BlendOver(0xFFFFFFFF, p);
    auto read = [&](const Image& im, double k) {
        ZXing::ImageView view((const uint8_t*)im.px.data(), im.w, im.h, ZXing::ImageFormat::BGRX, im.w * 4);
        ZXing::ReaderOptions opt;
        opt.setTryHarder(true); opt.setTryRotate(true); opt.setTryInvert(true); opt.setTryDownscale(true);
        for (auto& r : ZXing::ReadBarcodes(view, opt)) {
            if (!r.isValid()) continue;
            Result x;
            x.text = Utf8ToW(r.text());
            x.format = Utf8ToW(ZXing::ToString(r.format()));
            auto& p = r.position();
            int x0 = std::min({ p.topLeft().x, p.topRight().x, p.bottomLeft().x, p.bottomRight().x });
            int y0 = std::min({ p.topLeft().y, p.topRight().y, p.bottomLeft().y, p.bottomRight().y });
            int x1 = std::max({ p.topLeft().x, p.topRight().x, p.bottomLeft().x, p.bottomRight().x });
            int y1 = std::max({ p.topLeft().y, p.topRight().y, p.bottomLeft().y, p.bottomRight().y });
            x.box = { (LONG)(x0 / k), (LONG)(y0 / k), (LONG)(x1 / k) + 1, (LONG)(y1 / k) + 1 };
            std::wstring low = ToLower(x.text);
            x.url = low.rfind(L"http://", 0) == 0 || low.rfind(L"https://", 0) == 0;
            bool dup = std::any_of(out.begin(), out.end(), [&](const Result& o) { return o.text == x.text && o.format == x.format; });
            if (!dup) out.push_back(x);
        }
    };
    read(img, 1.0);
    // small codes on a screen capture (a few pixels per module) read better enlarged
    if (out.empty() && img.w * img.h < 1500 * 1500) read(ScaleImage(img, img.w * 2, img.h * 2, false), 2.0);
    return out;
}

void ShowResults(HWND owner, const std::vector<Result>& results) {
    if (results.empty()) {
        MessageBoxW(owner, Tr(L"Nenhum código QR ou de barras encontrado na imagem."), Tr(L"Ler código QR"), MB_ICONINFORMATION);
        return;
    }
    for (;;) {
        std::vector<std::wstring> labels;
        std::vector<TASKDIALOG_BUTTON> btns;
        std::vector<std::pair<int, size_t>> action;   // (0 copy, 1 open) of each button
        std::wstring content;
        for (size_t i = 0; i < results.size() && i < 8; i++) {
            const auto& r = results[i];
            std::wstring shown = r.text.size() > 120 ? r.text.substr(0, 117) + L"…" : r.text;
            content += Format(L"%zu. [%s] %s\n", i + 1, r.format.c_str(), shown.c_str());
            labels.push_back(Format(Tr(L"Copiar %zu\n%s"), i + 1, shown.c_str()));
            action.push_back({ 0, i });
            if (r.url) { labels.push_back(Format(Tr(L"Abrir o link %zu\n%s"), i + 1, shown.c_str())); action.push_back({ 1, i }); }
        }
        if (results.size() > 1) { labels.push_back(Tr(L"Copiar tudo")); action.push_back({ 2, 0 }); }
        for (size_t i = 0; i < labels.size(); i++) btns.push_back({ (int)(100 + i), labels[i].c_str() });
        std::wstring title = results.size() == 1 ? Tr(L"1 código encontrado") : Format(Tr(L"%zu códigos encontrados"), results.size());
        TASKDIALOGCONFIG tc{ sizeof tc };
        tc.hwndParent = owner;
        tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        tc.pszWindowTitle = Tr(L"Ler código QR");
        tc.pszMainInstruction = title.c_str();
        tc.pszContent = content.c_str();
        tc.cButtons = (UINT)btns.size(); tc.pButtons = btns.data();
        tc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        int btn = 0;
        if (FAILED(TaskDialogIndirect(&tc, &btn, nullptr, nullptr)) || btn < 100 || btn >= 100 + (int)action.size()) return;
        auto [kind, idx] = action[btn - 100];
        if (kind == 0) { clip::CopyText(owner, results[idx].text); return; }
        if (kind == 1) { ShellExecuteW(owner, L"open", results[idx].text.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return; }
        std::wstring all;
        for (auto& r : results) all += (all.empty() ? L"" : L"\r\n\r\n") + r.text;
        clip::CopyText(owner, all);
        return;
    }
}

} // namespace snap::barcode
