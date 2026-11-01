// Quick access overlay after capturing (C08; ShareX dossier 01 · F08 TaskThumbnailPanel/NotificationForm): a small
// floating thumbnail in a corner of the screen with Copy, Save, Annotate (Editor), Pin, Delete and Close; the thumbnail
// can be dragged into another program (CF_HDROP); it closes by itself after OverlaySeconds (paused while the mouse is
// over it) and several captures stack. Right click: Copy as… (C30, CopyAsMenu in Clipboard.h). Settings: AfterCaptureMode (0 Editor as the
// preset says, 1 overlay instead of the Editor, 2 both), OverlaySeconds, OverlayCorner.
#pragma once
#include "Image.h"

namespace snap {

class QuickOverlay {
public:
    std::function<void(const std::wstring& libPath)> annotate;
    std::function<void(const Image& img, const std::wstring& title)> pin;
    std::function<void(const Image& img)> readQr;
    std::function<void(const std::wstring& libPath, const Image& img)> upload;   // set only when an upload destination exists
    std::function<void(const std::wstring&, const std::wstring&)> notify;
    std::function<void()> libraryChanged;

    void Show(const Image& img, const std::wstring& libPath, bool video, const std::wstring& title);
    void CloseAll();
};

} // namespace snap
