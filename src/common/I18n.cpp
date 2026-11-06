// The interface language lives in rust/snapcore/src/i18n.rs (catalog, .po reader, language choice); this keeps the C++
// API of I18n.h for the code that has not moved yet.
#include "Common.h"
#include "snapcore.h"

namespace snap {

const wchar_t* Tr(const wchar_t* pt) { return pt ? snapcore_tr(pt) : pt; }

std::wstring Tr(const std::wstring& pt) { return snapcore_tr(pt.c_str()); }

namespace i18n {

const std::vector<Language>& Languages() {
    static const std::vector<Language> list = [] {
        std::vector<Language> v;
        Language l{};
        for (size_t i = 0; snapcore_i18n_language(i, &l.code, &l.name); i++) v.push_back(l);
        return v;
    }();
    return list;
}

bool Available(const std::wstring& code) { return snapcore_i18n_available(code.c_str()); }
std::wstring Configured() { return snapcore_i18n_configured(); }
void SetConfigured(const std::wstring& code) { snapcore_i18n_set_configured(code.c_str()); }
std::wstring Resolve() { return snapcore_i18n_resolve(); }
static std::wstring g_current = L"pt";

void Init() {
    snapcore_i18n_init();
    g_current = snapcore_i18n_current();
}

const std::wstring& Current() { return g_current; }

bool Rtl() { return snapcore_i18n_rtl(); }
const wchar_t* UiFontFace() { return snapcore_i18n_ui_font_face(); }
wchar_t AltKey(const std::wstring& name, wchar_t fallback) { return snapcore_i18n_alt_key(name.c_str(), fallback); }

bool LoadPo(const std::wstring& path, std::vector<std::pair<std::wstring, std::wstring>>& out) {
    return snapcore_po_load(path.c_str(), [](void* ctx, const wchar_t* id, const wchar_t* str) {
        static_cast<std::vector<std::pair<std::wstring, std::wstring>>*>(ctx)->push_back({ id, str });
    }, &out);
}

} // namespace i18n
} // namespace snap
