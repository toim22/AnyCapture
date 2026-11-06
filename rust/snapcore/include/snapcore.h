// snapcore — Snapnab's shared Rust layer (rust/snapcore, C ABI in src/ffi.rs), linked into both programs while the C++
// code moves to Rust. Strings cross as nul-terminated UTF-16 unless a length is given; the const wchar_t* snapcore
// returns live as long as the process. Stream paths of the .snap container are UTF-8 with '/' between storages
// ("Frame0/ImageStorage/Image_0.png").
#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {

// ---- the .snap container (the tests read streams with it; SnapFile.cpp uses the .snap functions below)
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

// ---- XML (Xml.cpp). Text is UTF-8 with a length. A document read by snapcore comes back through the sink in document
// order: open(name), attr(k, v)…, text(t) if any, the children, close(). The root is sent also when the text is
// malformed (what was read up to the error), as XmlParse always did.
struct SnapcoreXmlSink {
    void (*open)(void* ctx, const char* name, size_t len);
    void (*attr)(void* ctx, const char* k, size_t klen, const char* v, size_t vlen);
    void (*text)(void* ctx, const char* t, size_t len);
    void (*close)(void* ctx);
};
bool    snapcore_xml_parse(const char* data, size_t len, const SnapcoreXmlSink* sink, void* ctx);   // false: malformed
int32_t snapcore_xml_load(const wchar_t* path, size_t len, const SnapcoreXmlSink* sink, void* ctx);
                                                       // -1: not read (nothing sent), 0: malformed, 1: read
// a document written by snapcore is fed the same way, then serialized or saved (both free the builder)
struct SnapcoreXmlBuilder;
SnapcoreXmlBuilder* snapcore_xml_builder_new();
void snapcore_xml_open(SnapcoreXmlBuilder* b, const char* name, size_t len);
void snapcore_xml_attr(SnapcoreXmlBuilder* b, const char* k, size_t klen, const char* v, size_t vlen);
void snapcore_xml_text(SnapcoreXmlBuilder* b, const char* t, size_t len);
void snapcore_xml_close(SnapcoreXmlBuilder* b);
bool snapcore_xml_serialize(SnapcoreXmlBuilder* b, bool declaration, SnapcoreBuf* out);
bool snapcore_xml_save(SnapcoreXmlBuilder* b, const wchar_t* path, size_t len);

// ---- image files through the Windows codecs (Image.cpp; WebP stays with libwebp). Formats as ImageFormat in Image.h.
// Errors: decoding 1 = failed, 2 = Windows has no decoder for the file; encoding 1 = failed, 2 = the AVIF codec hint.
struct SnapcoreImage;                                  // a decoded image: copy it out, then snapcore_image_free
struct SnapcoreImageInfo { uint32_t w, h; bool hasMeta, gps; double lat, lon; };
SnapcoreImage*  snapcore_image_decode(const uint8_t* data, size_t len, int32_t* err);
SnapcoreImage*  snapcore_image_load(const wchar_t* path, size_t len, int32_t* err);
void            snapcore_image_info(const SnapcoreImage* img, SnapcoreImageInfo* out);
const uint32_t* snapcore_image_pixels(const SnapcoreImage* img);                  // w × h, 0xAARRGGBB
const wchar_t*  snapcore_image_meta_text(const SnapcoreImage* img, uint32_t field);
                                                       // 0 date taken, 1 make, 2 model, 3 software ("" if none)
void            snapcore_image_free(SnapcoreImage* img);
// an image handed to snapcore (copied: the pointers need to live during the call only); strings may be null
struct SnapcoreImageIn {
    uint32_t w, h;
    const uint32_t* px;
    float dpi;
    bool hasMeta;                                      // the metadata of the source photo
    const wchar_t *dateTaken, *make, *model, *software;
    bool gps;
    double lat, lon;
};
struct SnapcoreEncodeRequest {
    SnapcoreImageIn image;
    int32_t format, jpegQuality, avifQuality;
    bool stripMetadata;
    const wchar_t* version;                            // "Snapnab <version>" goes into the metadata
};
bool snapcore_image_encode(const SnapcoreEncodeRequest* req, SnapcoreBuf* out, int32_t* err);

// ---- .snap documents (SnapFile.cpp): the index, each frame's document, base image and embedded images
struct SnapcoreSnapSave;                               // a .snap handed over frame by frame, then saved
SnapcoreSnapSave* snapcore_snap_new();
bool snapcore_snap_add_frame(SnapcoreSnapSave* s, const SnapcoreImageIn* base, SnapcoreXmlBuilder* doc);   // doc consumed
bool snapcore_snap_add_embedded(SnapcoreSnapSave* s, const char* name, size_t len, const SnapcoreImageIn* img);
bool snapcore_snap_save(SnapcoreSnapSave* s, const wchar_t* path, size_t len, const wchar_t* version);   // frees s
struct SnapcoreSnap;                                   // a .snap read: copy it out, then snapcore_snap_free
SnapcoreSnap*        snapcore_snap_load(const wchar_t* path, size_t len);         // null: not readable
size_t               snapcore_snap_frames(const SnapcoreSnap* h);
void                 snapcore_snap_frame_doc(const SnapcoreSnap* h, size_t i, const SnapcoreXmlSink* sink, void* ctx);
const SnapcoreImage* snapcore_snap_frame_base(const SnapcoreSnap* h, size_t i);   // not to be freed
size_t               snapcore_snap_embedded_count(const SnapcoreSnap* h, size_t i);
const SnapcoreImage* snapcore_snap_embedded(const SnapcoreSnap* h, size_t i, size_t k, const char** name, size_t* len);
void                 snapcore_snap_free(SnapcoreSnap* h);
int32_t snapcore_snap_load_document(const wchar_t* path, size_t len, const SnapcoreXmlSink* sink, void* ctx);
                                                       // the first frame's document: -1 not read, 0 malformed, 1 read

}
