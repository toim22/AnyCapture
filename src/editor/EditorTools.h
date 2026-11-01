// Editor tools of the competitor feature request (docs/pesquisa/competitor-feature-request-prompt.md): combine captures
// (C09), automatic redaction (C11), image comparison (C17), AI assistant (C22), cursor stamps (C25), date and time
// stamp (C26), background removal (C27), batch processing (C28), split long images (C34) and the contrast checker
// (C36). Reference: ShareX dossier 01 · F12 ImageCombiner, F13 image splitter, F14 batch (Image effects / Tools),
// 02 · E.5 auto-redaction helpers; the rest has no counterpart and follows the Editor's own patterns (RunForm, objects
// added through Canvas::PasteObjects so each result stays editable and undoable).
#pragma once
#include "Image.h"
#include "Ocr.h"

namespace snap::ed {

// ---------------------------------------------------------------- C09 combine
struct CombineOptions {
    int layout = 0;                  // 0 side by side, 1 stacked, 2 grid
    int columns = 2;                 // grid
    int gap = 16;                    // space between the images (px)
    int margin = 0;                  // space around everything (px)
    int align = 1;                   // 0 top/left, 1 center, 2 bottom/right
    bool sameSize = false;           // scales the images to the same height (side by side) / width (stacked, grid cells)
    bool transparent = false;
    COLORREF background = RGB(255, 255, 255);
    static CombineOptions Load();    // registry (Editor\Combine*)
    void Save() const;
};
Image CombineImages(const std::vector<Image>& imgs, const CombineOptions& o, std::vector<RECT>* placed = nullptr);
bool CombineDialog(HWND owner, const std::vector<Image>& imgs, CombineOptions& o);   // with a live preview

// ---------------------------------------------------------------- C11 automatic redaction
struct RedactItem { std::wstring kind, text; RECT box{}; bool on = true; };
// kind of sensitive data in a piece of text ("" = none): e-mail, phone, card (Luhn), IBAN, CPF/CNPJ, SSN, IPv4,
// URL with a token, API key / long secret. Exposed for the tests.
std::wstring SensitiveKind(const std::wstring& text);
std::vector<RedactItem> FindSensitive(const std::vector<OcrLine>& lines);
// shows the found items over the image (each can be unchecked) and the style: 0 blur, 1 pixelate, 2 black box
bool RedactDialog(HWND owner, const Image& img, std::vector<RedactItem>& items, int& style);

// ---------------------------------------------------------------- C17 comparison
// pixels that differ more than 'tolerance' (0..255 per channel) in red over a faded copy of 'a'
Image DiffImage(const Image& a, const Image& b, int tolerance, int* changedPixels = nullptr, RECT* changedBox = nullptr);
// window: overlay with a transparency slider, difference, side by side, swipe; export GIF / combined image
void ShowCompare(HWND owner, const Image& a, const Image& b, const std::wstring& nameA, const std::wstring& nameB);

// ---------------------------------------------------------------- C22 AI assistant (opt-in, bring your own key)
void ShowAssistant(HWND owner, const Image& img, std::function<void(const std::wstring&)> addAsText);
bool AssistantSettingsDialog(HWND owner);

// ---------------------------------------------------------------- C25 cursor stamps ("cursor:arrow", …)
std::vector<std::pair<std::wstring, std::wstring>> CursorStamps();     // id, name
Image RenderCursorStamp(const std::wstring& id, int px);

// ---------------------------------------------------------------- C26 date and time stamp
// tokens: yyyy yy MMMM MMM MM M dddd ddd dd d HH H hh h mm ss tt; text in '…' is kept; empty format = the user's
// short date + time (in the interface language)
std::wstring DateStampText(const std::wstring& format, const SYSTEMTIME& t);
std::wstring DateStampFormat();      // registry DateStampFormat
void SetDateStampFormat(const std::wstring& f);

// ---------------------------------------------------------------- C27 background removal (local)
// the background is what touches the image edges and looks like the border colors; the main subject stays. 'tolerance'
// 1..100, 'feather' px of soft edge
Image RemoveBackground(const Image& img, int tolerance, int feather);

// ---------------------------------------------------------------- C28 batch processing
struct BatchEffect { std::wstring name; std::function<Image(const Image&)> run; };
// 'files' empty = asks for the files; 'load' reads an image or a .snap (rendered with its objects)
void BatchDialog(HWND owner, std::vector<std::wstring> files, std::function<bool(const std::wstring&, Image&)> load,
                 const std::vector<BatchEffect>& effects);
bool PickImageFiles(HWND owner, std::vector<std::wstring>& files);      // several images (multi-select open dialog)

// ---------------------------------------------------------------- C34 split long images
// page breaks every ~pageH px moved to the nearest quiet row (no text cut), searching up to 'search' px back
std::vector<int> SmartBreaks(const Image& img, int pageH, int search = -1);       // (Pdf.cpp)
// 'parts' parts of about the same height, each cut moved to the quietest row near its ideal position
std::vector<int> SmartCuts(const Image& img, int parts);
bool SplitLongPages();               // option: print and PDF split long images across pages (SplitLongPages, on)
std::vector<Image> SplitImage(const Image& img, const std::vector<int>& cuts, bool vertical);
// asks how to split (height/width, number of parts, smart breaks) and returns the cut positions
bool SplitDialog(HWND owner, const Image& img, std::vector<int>& cuts, bool& vertical);

// ---------------------------------------------------------------- C23 draw & hold
enum StrokeShape { RecNone = 0, RecLine, RecArrow, RecRect, RecEllipse, RecTriangle };
// shape of a rough pen stroke; 'a','b' = the line's ends (tail and tip for the arrow) or the bounding box corners
int RecognizeStroke(const std::vector<Gdiplus::PointF>& pts, Gdiplus::PointF& a, Gdiplus::PointF& b);

// ---------------------------------------------------------------- C12 text under an area
// word boxes inside 'area' (at least half of each word), cut to the area
std::vector<Gdiplus::RectF> WordsInArea(const std::vector<OcrLine>& lines, Gdiplus::RectF area);
// smart highlighter: one rectangle per text line, covering the words the stroke touched (empty = no text there)
std::vector<Gdiplus::RectF> SnapToTextLines(const std::vector<OcrLine>& lines, Gdiplus::RectF area);

// ---------------------------------------------------------------- C36 contrast checker
// two swatches (each picked with the screen color picker or the color dialog), a sample and the WCAG verdicts
void ShowContrastChecker(HWND owner, COLORREF fg, COLORREF bg);
// the two main colors of an area (background = the most common, text = the most common that contrasts with it)
void DominantColors(const Image& img, COLORREF& fg, COLORREF& bg);
std::wstring ContrastReport(COLORREF fg, COLORREF bg);    // ratio and WCAG AA/AAA verdicts (also for the tests)

} // namespace snap::ed
