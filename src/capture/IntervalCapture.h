// Automatic interval capture (C24; ShareX dossier 01 · A6 AutoCaptureForm: repeat 1–86 400 s, region or full screen,
// duration, tray icon). Captures the full screen, the monitor, a region or a window every N seconds, for a set time
// or until stopped, into a folder with automatic names (<Prefix><number>). Settings remembered in the registry
// (Interval*).
#pragma once
#include "Image.h"

namespace snap {

class IntervalCapture {
public:
    std::function<void(bool running, int count)> onStatus;     // tray indicator
    std::function<void(const std::wstring&, const std::wstring&)> notify;
    bool running() const { return running_; }
    int count() const { return count_; }
    bool Setup(HWND owner);          // asks the settings (and the region/window) and starts; false if canceled
    void Start();                    // with the remembered settings (command line)
    void Stop();
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Tick();
    void Begin();
    HWND wnd_ = nullptr;
    bool running_ = false;
    int count_ = 0;
    ULONGLONG endAt_ = 0;
    int area_ = 0;                   // 0 full screen, 1 monitor, 2 region, 3 window
    RECT region_{};
    HWND window_ = nullptr;
    int seconds_ = 10, minutes_ = 0, format_ = 0;
    bool cursor_ = false;
    std::wstring folder_, prefix_;
};

} // namespace snap
