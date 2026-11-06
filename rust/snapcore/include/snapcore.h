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

// ---- image files (Image.cpp): the Windows codecs, and libwebp for WebP. Formats as ImageFormat in Image.h.
// Errors: decoding 1 = failed, 2 = Windows has no decoder for the file, 3 = not a WebP libwebp reads, 4 = libwebp failed
// while decoding, 5 = the WebP file could not be read whole; encoding 1 = failed, 2 = the AVIF codec hint, 3 = libwebp
// could not encode.
struct SnapcoreImage;                                  // a decoded image: copy it out, then snapcore_image_free
struct SnapcoreImageInfo { uint32_t w, h; bool hasMeta, gps; double lat, lon; float dpi; };
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
    int32_t format, jpegQuality, avifQuality, webpQuality;
    bool webpLossless, stripMetadata;
    const wchar_t* version;                            // "Snapnab <version>" goes into the metadata (not WebP's)
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

// ---- the capture library (Library.cpp); dir: the library folder (dirLen units), null for the program's (LibraryDir)
bool snapcore_library_dir(SnapcoreWStr* out);
typedef void (*SnapcoreLibraryItemFn)(void* ctx, const wchar_t* path, size_t len, uint64_t modified, uint64_t size, bool video);
void snapcore_library_list(const wchar_t* dir, size_t dirLen, size_t max, SnapcoreLibraryItemFn item, void* ctx);
                                                       // the .snap and .mp4 files, the most recent first
bool snapcore_library_thumb_path(const wchar_t* dir, size_t dirLen, const wchar_t* item, size_t itemLen, SnapcoreWStr* out);
bool snapcore_library_write_thumbnail(const wchar_t* dir, size_t dirLen, const wchar_t* item, size_t itemLen,
                                      const SnapcoreImageIn* thumb, const wchar_t* version);   // thumb already scaled
struct SnapcoreNamedImage { const char* name; size_t len; SnapcoreImageIn image; };   // name: StreamName, UTF-8
struct SnapcoreLibraryAdd {
    const wchar_t* dir; size_t dirLen;
    SnapcoreImageIn base, thumb;                       // an empty thumb is not written
    const wchar_t *mode, *windowTitle, *application;
    SnapcoreXmlBuilder* objects;                       // the <Objects> element (consumed), null for a plain image
    const SnapcoreNamedImage* embedded; size_t embeddedCount;
    const wchar_t* version;
};
bool snapcore_library_add(const SnapcoreLibraryAdd* req, SnapcoreWStr* out);   // the new .snap's path
bool snapcore_library_new_video_path(const wchar_t* dir, size_t dirLen, SnapcoreWStr* out);
bool snapcore_library_audio_dir(SnapcoreWStr* out);
bool snapcore_library_new_audio_path(bool mp3, SnapcoreWStr* out);
bool snapcore_library_delete(const wchar_t* dir, size_t dirLen, const wchar_t* item, size_t itemLen, void* owner);
int32_t snapcore_library_cleanup(const wchar_t* dir, size_t dirLen, int32_t days, bool keepNamed, bool keepTagged, bool recycle);
int32_t snapcore_library_cleanup_now();
bool snapcore_library_contains(const wchar_t* dir, size_t dirLen, const wchar_t* path, size_t len);
bool snapcore_library_name(const wchar_t* dir, size_t dirLen, const wchar_t* item, size_t itemLen, SnapcoreWStr* out);
bool snapcore_library_set_name(const wchar_t* dir, size_t dirLen, const wchar_t* item, size_t itemLen,
                               const wchar_t* name, size_t nameLen);
bool snapcore_next_auto_name(const wchar_t* folder, size_t folderLen, const wchar_t* prefix, size_t prefixLen,
                             const wchar_t* ext, size_t extLen, int32_t digits, SnapcoreWStr* out);

// ---- pixel work: effects (Effects.cpp), Beautify, scaling (Image.cpp), computer vision (Cv.cpp) and PDF (Pdf.cpp).
// A new image comes back as a SnapcoreImage (copy it out, then snapcore_image_free; null: failed); the in-place calls
// change the caller's w × h pixels. Colors are 0xAARRGGBB unless the field says COLORREF.
struct SnapcoreRect { int32_t left, top, right, bottom; };
struct SnapcoreFx {
    int32_t op;
    int32_t i[4];
    double d;
    uint32_t c[2];
    SnapcoreRect rect;
    const wchar_t* text;
    size_t textLen;
};
// snapcore_fx ops: 0 Rotate90 (i0 clockwise), 1 Rotate180, 2 FlipH, 3 FlipV, 4 RotateAngle (d, c0), 5 CanvasResize
// (i0 w, i1 h, i2 anchor, c0), 6 AutoTrim (i0), 7 Scale (i0 %), 8 Border (i0, c0), 9 Shadow (i0 offset, i1 blur,
// i2 opacity), 10 EdgeEffect (i0 style, i1 size, i2 sides, i3 shadow), 11 Perspective (i0, i1, i2), 12 PageCurl (i0, i1),
// 13 Spotlight (rect, i0 dim, i1 blur, i2 zoom, i3 shadow), 14 Caption (text, i0 top, i1 px, c0 fg, c1 bg),
// 15 ScaleImage (i0 w, i1 h, i2 high quality), 16 MakeThumbnail (i0, i1)
SnapcoreImage* snapcore_fx(const SnapcoreImageIn* img, const SnapcoreFx* a);
// snapcore_fx_in_place ops: 0 Grayscale, 1 Invert, 2 Sepia, 3 Monochrome (i0), 4 BrightnessContrast (i0, i1),
// 5 HueSaturation (i0, i1), 6 Gamma (d), 7 ReplaceColor (c0, c1, i0), 8 BoxBlur (rect, i0), 9 Pixelate (rect, i0),
// 10 Emboss (i0), 11 Sharpen (i0), 12 Watermark (text, i0 opacity, i1 tiled, i2 emboss)
void snapcore_fx_in_place(uint32_t* px, int32_t w, int32_t h, const SnapcoreFx* a);
bool snapcore_fx_expand_tokens(const wchar_t* text, size_t len, SnapcoreWStr* out);
struct SnapcoreBeautify {                              // BeautifyOptions; colors are COLORREFs
    int32_t background;
    uint32_t color1, color2;
    int32_t angle;
    const wchar_t* imagePath;
    size_t imagePathLen;
    int32_t padding, radius, shadow, shadowSize, align, aspect;
    bool autoBalance;
};
SnapcoreImage* snapcore_beautify(const SnapcoreImageIn* img, const SnapcoreBeautify* o, int32_t* origin);   // origin: x, y
bool snapcore_wallpaper_path(SnapcoreWStr* out);
struct SnapcoreCaptureEffects {                        // fx::CaptureEffects, field by field; colors are COLORREFs
    bool border; int32_t borderWidth; uint32_t borderColor;
    bool filter; int32_t filterKind;
    bool shadow; int32_t shadowOffset, shadowBlur, shadowOpacity;
    bool caption; const wchar_t* captionText; size_t captionTextLen; bool captionTop;
    bool replaceColor; uint32_t replaceFrom, replaceTo; int32_t replaceTolerance;
    bool scale; int32_t scalePercent;
    bool edge; int32_t edgeStyle, edgeSize, edgeSides;
    bool trim; int32_t trimTolerance;
    bool adjust; int32_t brightness, contrast, hue, saturation;
    bool rotate; int32_t rotateKind;
    bool watermark; const wchar_t* watermarkText; size_t watermarkTextLen; int32_t watermarkOpacity; bool watermarkTiled;
    bool resolution; int32_t dpi;
    bool beautify; SnapcoreBeautify beautifyOpt;
};
SnapcoreImage* snapcore_capture_effects(const SnapcoreImageIn* img, const SnapcoreCaptureEffects* e);
// computer vision: masks are w × h bytes (0 or 255) the caller allocates
bool snapcore_cv_wand(const SnapcoreImageIn* img, int32_t x, int32_t y, float tol, bool global, bool doWand, uint8_t* mask);
void snapcore_cv_shape_mask(int32_t kind, int32_t w, int32_t h, SnapcoreRect r, const int32_t* pts, size_t n,
                            uint8_t* mask);        // 0 rectangle, 1 ellipse (r), 2 polygon (n x, y pairs)
SnapcoreRect snapcore_cv_mask_bounds(const uint8_t* mask, int32_t w, int32_t h);
bool snapcore_cv_mask_empty(const uint8_t* mask, size_t n);
void snapcore_cv_dilate(const uint8_t* mask, int32_t w, int32_t h, int32_t k, uint8_t* out);
// 0 Dingo7 (arg kernel; false when there is no ring), 1 diffusion (arg iterations), 2 smart fill
bool snapcore_cv_inpaint(int32_t kind, uint32_t* px, int32_t w, int32_t h, const uint8_t* mask, int32_t arg,
                         uint32_t* dominant, double* share);
float snapcore_cv_auto_tolerance(const SnapcoreImageIn* img);
float snapcore_cv_tolerance_scaling(float t, float inMin, float inMax, float outMin, float outMax);
void snapcore_cv_detect_translation(const SnapcoreImageIn* a, const SnapcoreImageIn* b, int32_t* offset, double* confidence);
// PDF: 0 saved, 1 empty image, 2 the image could not be encoded, 3 the file could not be created, 4 nor written
struct SnapcorePdfLink { float x, y, w, h; const wchar_t* url; size_t urlLen; };
int32_t snapcore_pdf_save(const wchar_t* path, size_t len, const SnapcoreImageIn* img, const SnapcorePdfLink* links, size_t n);
// SmartBreaks (parts < 0) or SmartCuts (parts >= 0) into out (capacity cap); returns how many there are
size_t snapcore_page_breaks(const SnapcoreImageIn* img, int32_t pageH, int32_t search, int32_t parts, int32_t* out, size_t cap);
bool snapcore_split_long_pages();

// ---- color texts (Color.cpp; ColorToHex / HexToColor of Util.cpp). Colors are COLORREFs.
bool           snapcore_color_text(uint32_t c, int32_t fmt, SnapcoreWStr* out);   // fmt as color::Fmt
const wchar_t* snapcore_color_format_name(int32_t fmt);                            // "" past the last
void           snapcore_color_hsl(uint32_t c, double* hsl);                        // h 0..360, s and l 0..1
double         snapcore_color_luminance(uint32_t c);
double         snapcore_color_contrast(uint32_t a, uint32_t b);
int32_t        snapcore_color_default_format();
void           snapcore_color_set_default_format(int32_t fmt);
size_t         snapcore_color_recent(uint32_t* out, size_t cap);                 // how many there are
void           snapcore_color_add_recent(uint32_t c);
uint32_t       snapcore_color_from_hex(const wchar_t* s, size_t len, uint32_t def);

// ---- HDR capture (Hdr.cpp)
struct SnapcoreHdrSession;
bool    snapcore_hdr_any_monitor();
bool    snapcore_hdr_is_area(SnapcoreRect screen);
bool    snapcore_hdr_enabled();
bool    snapcore_hdr_fix_capture(SnapcoreRect screen, uint32_t* px, int32_t w, int32_t h);   // in place
uint8_t snapcore_hdr_tone_map(float scRgb, float sdrWhiteNits);
float   snapcore_hdr_half_to_float(uint16_t h);
SnapcoreHdrSession* snapcore_hdr_begin(SnapcoreRect screen, int32_t outW, int32_t outH);   // null: not an HDR area
bool    snapcore_hdr_grab(SnapcoreHdrSession* s, uint32_t* bottomUp, size_t n);
void    snapcore_hdr_end(SnapcoreHdrSession* s);

// ---- OCR and faces (Ocr.cpp). Results: 0 done, 1 empty image, 2 no OCR language installed, 3 OCR failed, 4 no face
// detection on this Windows, 5 face detection failed, 6 a Windows error whose text is in *msg.
int32_t snapcore_ocr_text(const SnapcoreImageIn* img, SnapcoreWStr* text, SnapcoreWStr* msg);
typedef void (*SnapcoreOcrSink)(void* ctx, const wchar_t* text, size_t len, SnapcoreRect r, bool word);   // a line, then its words
int32_t snapcore_ocr_lines(const SnapcoreImageIn* img, SnapcoreOcrSink sink, void* ctx, SnapcoreWStr* msg);
typedef void (*SnapcoreFaceSink)(void* ctx, SnapcoreRect r);
int32_t snapcore_detect_faces(const SnapcoreImageIn* img, SnapcoreFaceSink sink, void* ctx, SnapcoreWStr* msg);

// ---- QR codes and barcodes (Barcode.cpp): text in UTF-8, the symbology as ZXing-C++ named it ("QRCode", "EAN-13"…)
typedef void (*SnapcoreCodeSink)(void* ctx, const char* text, size_t len, const char* format, SnapcoreRect r, bool url);
size_t snapcore_barcode_read(const SnapcoreImageIn* img, SnapcoreCodeSink sink, void* ctx);

// ---- uploads (Upload.cpp): Uploaders.xml as the C++ Settings has it; strings nul-terminated (null = "")
struct SnapcoreUploadSettings {
    int32_t dest;
    const wchar_t* s3Endpoint; const wchar_t* s3Region; const wchar_t* s3Bucket; const wchar_t* s3Prefix;
    const wchar_t* s3AccessKey; const wchar_t* s3PublicUrl;
    bool s3PathStyle; bool s3PublicRead;
    const wchar_t* dropboxAppKey; const wchar_t* googleClientId; const wchar_t* msClientId; const wchar_t* folder;
    bool publicLink;
    const wchar_t* host; const wchar_t* user; const wchar_t* keyFile; const wchar_t* httpBase;
    int32_t port;
    const wchar_t* customJson;
    bool copyLink;
};
SnapcoreUploadSettings* snapcore_upload_settings_load();           // free with snapcore_upload_settings_free
void snapcore_upload_settings_free(SnapcoreUploadSettings* s);
bool snapcore_upload_settings_save(const SnapcoreUploadSettings* s);
bool snapcore_upload_configured(const SnapcoreUploadSettings* s);
struct SnapcoreUploadResult { bool ok; SnapcoreWStr url, deleteUrl, thumbnailUrl, error; };   // each string: snapcore_wstr_free
bool snapcore_upload_file(const SnapcoreUploadSettings* s, const wchar_t* path, size_t pathLen, const wchar_t* remote, size_t remoteLen,
                          SnapcoreUploadResult* out);               // blocking: call from a worker thread
bool snapcore_upload_custom_valid(const char* utf8, size_t len);    // a .sxcu: JSON with a RequestURL
bool snapcore_upload_url_get(const wchar_t* item, size_t len, SnapcoreWStr* out);              // Library\Uploads.xml
void snapcore_upload_url_set(const wchar_t* item, size_t len, const wchar_t* url, size_t urlLen);
// sign-in (OAuth with PKCE): the page to open (or null and the error), the loopback poll, the code exchanged for the
// refresh token (Dropbox: the code the user pasted; false with the error, "" when there is nothing to say)
struct SnapcoreSignIn;
SnapcoreSignIn* snapcore_signin_begin(const SnapcoreUploadSettings* s, SnapcoreWStr* urlOrError);
bool snapcore_signin_poll(SnapcoreSignIn* si);
bool snapcore_signin_finish(SnapcoreSignIn* si, const wchar_t* code, size_t len, SnapcoreWStr* error);
void snapcore_signin_free(SnapcoreSignIn* si);

// ---- secrets (Windows Credential Manager, generic credential "Snapnab/<name>"), nul-terminated names and values
bool snapcore_secret_save(const wchar_t* name, const wchar_t* value);
bool snapcore_secret_load(const wchar_t* name, SnapcoreWStr* out);   // "" if there is none
void snapcore_secret_delete(const wchar_t* name);

// ---- the AI assistant's request (Assistant.cpp), blocking: true with the answer (CR LF lines), false with the error
bool snapcore_ai_ask(int32_t provider, const wchar_t* endpoint, const wchar_t* model, const wchar_t* key, const uint8_t* png,
                     size_t pngLen, const wchar_t* question, SnapcoreWStr* out);

// ---- animated GIF (Gif.cpp): frames at the GIF's size (the caller scales them)
struct SnapcoreGif;
SnapcoreGif* snapcore_gif_open(const wchar_t* path, size_t len, int32_t w, int32_t h, double fps, int32_t bitsPerPixel,
                               bool dithering, bool deltaFrames, const char* comment, size_t commentLen);   // null: failed
bool snapcore_gif_add_frame(SnapcoreGif* g, const SnapcoreImageIn* frame, int32_t delayCentiseconds);
bool snapcore_gif_close(SnapcoreGif* g);                // frees g; false if anything could not be written
void snapcore_sort_by_byte(uint32_t* v, size_t n, uint32_t shift, bool heapOnly);   // for the tests: the palette's std::sort


// ---- the editor's objects and painters (Render.cpp → rust/snapcore/src/render.rs). An object is handed over as a
// SnapcoreObj pointing into the C++ DrawObj (valid during the call; flags as 0 / 1); GDI+ objects by their native
// pointers (GpGraphics*, GpPath*, GpPen*), drawn into with the same calls the C++ made.
struct SnapcorePointF { float x, y; };
struct SnapcoreRectF { float x, y, w, h; };
struct SnapcoreGradStop { float pos; uint32_t color; };
struct SnapcoreGradient { int32_t type; float angle; const SnapcoreGradStop* stops; size_t count; };
struct SnapcoreStyle {
    uint32_t stroke, fill, textColor;
    float width;
    int32_t opacity, shadow, shadowDir;
    uint32_t shadowColor;
    int32_t shadowOpacity, shadowBlur, shadowDist;
    const wchar_t* font;
    float fontSize;
    int32_t bold, italic, underline, alignH, alignV;
    float padding;
    int32_t arrowStart, arrowEnd;
    float startSize, endSize;
    int32_t dash, bezier, calloutShape, shaded;
    uint32_t tailColor;
    float tailWidth;
    int32_t shapeKind, stepShape, stepType, magnifyShape, magnifyScale, magnifyLink, hideKind, antialias, amount;
    float lineSpacing;
    uint32_t stepOutline;
    float stepOutlineWidth;
    int32_t areaShape;
    SnapcoreGradient grad;
    int32_t bgBlur, bgBlurAmount, loupe, loupeHandle, textOnly, snapText, sketch, sketchSeed, vertical, textOnPath;
};
struct SnapcoreObj {
    int32_t type;
    SnapcorePointF p1, p2, tail, ctrl;
    const SnapcorePointF* pts;
    size_t ptsCount;
    const wchar_t* text;
    int32_t stepValue;
    SnapcoreStyle style;
    const uint32_t* picture;                           // picW × picH, or null
    int32_t picW, picH;
    const SnapcoreObj* children;
    size_t childCount;
    const wchar_t* name;
    int32_t hidden, locked, floating;
    const SnapcoreRectF* textBoxes;
    size_t textBoxCount;
    const float* pressure;
    size_t pressureCount;
    float picDpi;                                      // the picture's resolution (0 = none)
};
// RenderObject: graphics is a GpGraphics over px (w × h), which the painter also changes directly (it flushes first)
void snapcore_render_object(void* graphics, uint32_t* px, int32_t w, int32_t h, const SnapcoreObj* o, bool hideText);
void snapcore_shape_path(void* path, int32_t kind, SnapcoreRectF r);           // added to the GpPath
void snapcore_callout_body_path(void* path, int32_t shape, SnapcoreRectF r);
void snapcore_area_path(void* path, const SnapcoreObj* o);
SnapcoreRectF snapcore_obj_rect(const SnapcoreObj* o, int32_t which);
                                                       // 0 bounds, 1 callout text area, 2 arrow label, 3 magnifier + handle
float snapcore_text_block_height(const SnapcoreObj* o, float width);
float snapcore_stamp_aspect(const wchar_t* id);
SnapcoreImage* snapcore_stamp_image(const wchar_t* id, int32_t px, int32_t which);   // 0 stamp, 1 emoji, 2 cursor; null if empty
void* snapcore_fill_brush(uint32_t color, const SnapcoreGradient* g, SnapcoreRectF r, int32_t opacity);   // a new GpBrush
void snapcore_fill_gradient_mask(uint32_t* px, int32_t w, int32_t h, const uint8_t* mask, size_t maskLen, SnapcoreRect b,
                                 const SnapcoreGradient* g, int32_t opacity);  // mask: w × h bytes, or empty
void snapcore_draw_cap_glyph(void* graphics, int32_t cap, SnapcoreRectF r, uint32_t color, bool atStart);
void snapcore_apply_dash(void* pen, int32_t dash);
bool snapcore_step_label(int32_t value, int32_t type, SnapcoreWStr* out);
float snapcore_cap_length(float width, float size);

// ---- the objects as XML (Serialize.cpp, Document::Save / Load → rust/snapcore/src/serial.rs). Objects snapcore read come
// back as a list of SnapcoreObj, valid until snapcore_objs_free; the C++ copies them out.
struct SnapcoreObjList;
struct SnapcoreDocInfo {
    const wchar_t *captureMode, *windowTitle, *application, *created, *tags;
    int32_t nextStep;
    const float* guidesX;
    size_t guidesXCount;
    const float* guidesY;
    size_t guidesYCount;
};
size_t             snapcore_objs_count(const SnapcoreObjList* l);
const SnapcoreObj* snapcore_objs_get(const SnapcoreObjList* l, size_t i);
const wchar_t*     snapcore_objs_source(const SnapcoreObjList* l);     // clipboard: the source document; null if not objects
void               snapcore_objs_free(SnapcoreObjList* l);
bool snapcore_objects_to_clip(const SnapcoreObj* objs, size_t n, const wchar_t* source, SnapcoreBuf* out);   // UTF-8
SnapcoreObjList* snapcore_objects_from_clip(const uint8_t* text, size_t len);
bool snapcore_doc_save(const wchar_t* path, size_t len, const SnapcoreDocInfo* info, const SnapcoreImageIn* base,
                       const SnapcoreObj* objs, size_t n, const wchar_t* version);
SnapcoreObjList* snapcore_doc_load(const wchar_t* path, size_t len);   // null: not a readable .snap
bool snapcore_doc_info(const SnapcoreObjList* l, SnapcoreDocInfo* out);
SnapcoreImage* snapcore_doc_take_base(SnapcoreObjList* l);             // once
bool snapcore_hit_test(const SnapcoreObj* o, float x, float y, float tol);
bool snapcore_grad_stops_string(const SnapcoreGradient* g, SnapcoreBuf* out);
// the gradient from the FillGradient / GradientAngle / GradientStops values (UTF-8, null = missing)
bool snapcore_grad_from_attrs(const char* kind, const char* angle, const char* stops, int32_t* outKind, float* outAngle,
                              void (*stop)(void* ctx, float pos, uint32_t color), void* ctx);

}
