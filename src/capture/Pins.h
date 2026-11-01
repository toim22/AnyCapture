// Pinned captures (C01; ShareX dossier 01 · F09 PinToScreenForm): images floating above every window, like sticky
// notes. Wheel zooms (20–500 %), Ctrl+wheel changes the opacity (10–100 %), arrows move (Shift = 10 px), middle click
// resets; lock, click-through, always on top and shadow; quick annotation with the Editor's own objects (pen,
// highlighter, arrow, rectangle, text); Ctrl+drag drags the image out to another program; Alt+click copies the color
// under the cursor. Closed pins stay in a short history (Pins folder, the last 10) and can be reopened.
#pragma once
#include "Image.h"

namespace snap {

class PinManager {
public:
    std::function<void(const std::wstring& title, const std::wstring& text)> notify;
    std::function<void(const std::wstring& snapPath)> openInEditor;      // the pin saved to the library, for the Editor
    std::function<void(const Image&)> readQr;                             // C04 on a pinned image

    // pins an image; 'at' = screen position of its top-left corner (e.g. where it was captured), else centered
    void Pin(const Image& img, const std::wstring& title = L"", const POINT* at = nullptr);
    bool PinFile(const std::wstring& path);         // image file or .snap (with its objects)
    bool PinClipboard();                            // image, or text rendered as a card, or a color (#RRGGBB, rgb(…))
    void ShowAll(bool show);
    void CloseAll();
    void ClickThroughOffAll();
    bool ReopenLast();
    size_t count() const;
    bool hidden() const { return hidden_; }
    std::vector<std::wstring> History() const;     // closed pins (newest first)
    bool ReopenHistory(size_t i);

    // image of a color (swatch with its value) and of a text (card), also used by the command line
    static Image ColorCard(COLORREF c);
    static Image TextCard(const std::wstring& text);
private:
    bool hidden_ = false;
};

} // namespace snap
