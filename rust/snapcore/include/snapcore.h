// snapcore — Snapnab's shared Rust layer (rust/snapcore, C ABI in src/ffi.rs), linked into both programs while the C++
// code moves to Rust. Strings cross as nul-terminated UTF-16 unless a length is given; the const wchar_t* snapcore
// returns live as long as the process. Stream paths of the .snap container are UTF-8 with '/' between storages
// ("Frame0/ImageStorage/Image_0.png").
#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {

// ---- the .snap container (SnapFile.cpp; the XML and PNG inside the streams stay in C++)
struct SnapcoreReader;
struct SnapcoreWriter;
struct SnapcoreBuf { uint8_t* data; size_t len; };       // allocated by snapcore: free with snapcore_buf_free

bool snapcore_is_snap(const wchar_t* path, size_t len);

SnapcoreReader* snapcore_open(const wchar_t* path, size_t len);                         // null: not a readable .snap
bool snapcore_read(SnapcoreReader* r, const char* stream, size_t streamLen, SnapcoreBuf* out);
void snapcore_close(SnapcoreReader* r);
void snapcore_buf_free(SnapcoreBuf b);

SnapcoreWriter* snapcore_create(const wchar_t* path, size_t len);                       // creates or truncates the file
bool snapcore_write(SnapcoreWriter* w, const char* stream, size_t streamLen, const uint8_t* data, size_t len);
bool snapcore_finish(SnapcoreWriter* w);                // writes everything out, closes and frees the writer
void snapcore_abort(SnapcoreWriter* w);                 // closes without finishing and frees the writer

struct SnapcoreWStr { wchar_t* data; size_t len; };     // allocated by snapcore, no nul: free with snapcore_wstr_free
void snapcore_wstr_free(SnapcoreWStr s);

// ---- log (Util.cpp: LogInit / Log)
void snapcore_log_init(const wchar_t* component, const wchar_t* version);
void snapcore_log(const wchar_t* msg);                  // already formatted; the time and thread id are added

// ---- registry, HKCU\Software\Snapnab\1[\sub] (Util.cpp: RegGet* / RegSet*); sub may be null
uint32_t snapcore_reg_get_dword(const wchar_t* sub, const wchar_t* name, uint32_t def);
void     snapcore_reg_set_dword(const wchar_t* sub, const wchar_t* name, uint32_t v);
bool     snapcore_reg_get_string(const wchar_t* sub, const wchar_t* name, SnapcoreWStr* out);   // false: missing
void     snapcore_reg_set_string(const wchar_t* sub, const wchar_t* name, const wchar_t* value);

// ---- interface language (I18n.cpp)
void           snapcore_i18n_init();
const wchar_t* snapcore_tr(const wchar_t* pt);          // the translation, or pt itself
bool           snapcore_i18n_language(size_t index, const wchar_t** code, const wchar_t** name);   // false past the 12
bool           snapcore_i18n_available(const wchar_t* code);
const wchar_t* snapcore_i18n_configured();              // "" = the Windows language
void           snapcore_i18n_set_configured(const wchar_t* code);
const wchar_t* snapcore_i18n_resolve();
const wchar_t* snapcore_i18n_current();
bool           snapcore_i18n_rtl();
const wchar_t* snapcore_i18n_ui_font_face();            // null: the default font
wchar_t        snapcore_i18n_alt_key(const wchar_t* name, wchar_t fallback);
bool           snapcore_po_load(const wchar_t* path, void (*entry)(void* ctx, const wchar_t* id, const wchar_t* str), void* ctx);

}
