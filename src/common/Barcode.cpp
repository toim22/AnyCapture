#include "Barcode.h"
#include "Clipboard.h"
#include "SnapcoreBridge.h"

namespace snap::barcode {

// the codes are read by snapcore (rust/snapcore/src/barcode.rs, with rxing)
std::vector<Result> Read(const Image& img) {
    std::vector<Result> out;
    SnapcoreImageIn in = ImageIn(img);
    auto sink = [](void* ctx, const char* text, size_t len, const char* format, SnapcoreRect r, bool url) {
        ((std::vector<Result>*)ctx)->push_back({ Utf8ToW(std::string(text, len)), Utf8ToW(format), { r.left, r.top, r.right, r.bottom }, url });
    };
    snapcore_barcode_read(&in, sink, &out);
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
