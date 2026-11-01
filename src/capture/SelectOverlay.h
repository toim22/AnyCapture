// Input selection (CCapturePreparer, comp_1 §4–§6): crosshair over the frozen screen
#pragma once
#include "Image.h"

namespace snap {

enum class SelectMode { Region, Window, Object, Fixed, Freehand, MultiArea, PickWindow };

struct SelectResult {
    bool ok = false;
    RECT rect{};                    // screen coordinates
    HWND window = nullptr;          // top-level window under the click
    HWND child = nullptr;           // innermost control under the click
    std::vector<POINT> path;        // freehand (screen)
    std::vector<RECT> rects;        // multiple areas (screen)
};

// behavior of the crosshair (Region mode)
struct SelectOptions {
    // the region is captured as soon as the mouse is released (QuickCrop, ShareX dossier 01 · A4); when off — or with Ctrl
    // held on release — the selection stays to be adjusted with the mouse and the keyboard (C19) and Enter captures it
    bool quickCrop = true;
    std::function<void(const std::wstring& path)> openCapture;   // H: a recent capture was chosen (the crosshair closes)
};

// Opens the selection window; 'done' is called once, on the UI thread.
void RunSelectOverlay(const Image& frozen, RECT virtualScreen, SelectMode mode, SIZE fixedSize, bool video,
                      std::function<void(const SelectResult&)> done, const SelectOptions& opt = {});

// last captured regions (screen coordinates, most recent first, up to 10): Tab / Shift+Tab in the crosshair (C18)
void RememberRegion(RECT screenRect);
std::vector<RECT> RegionHistory();

// Window utilities used by the selection and the capture
RECT WindowFrameRect(HWND h);        // DWMWA_EXTENDED_FRAME_BOUNDS (without the invisible shadow)
std::wstring WindowTitle(HWND h);
std::wstring WindowProcessName(HWND h);

} // namespace snap
