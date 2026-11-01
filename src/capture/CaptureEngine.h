// CCaptureEngine — capture state machine (ds_pseudocode §3, comp_1 §4–§7)
#pragma once
#include "Presets.h"
#include "Image.h"
#include "Ipc.h"
#include "Library.h"
#include "Recorder.h"
#include "RecorderHud.h"
#include "SelectOverlay.h"
#include "Panoramic.h"
#include "Webcam.h"
#include "Links.h"
#include "CleanDesktop.h"

namespace snap {

class CaptureEngine {
public:
    enum State { Idle, Activated, InputSelecting, InputSelected, Capturing, HaveInput, Outputting, Suspended };

    void Init(HWND frame, ipc::Notify* ipc);
    bool Activate(const Preset& preset);          // entry point (hotkey, button, tray, command line)
    State state() const { return state_; }
    bool busy() const { return state_ != Idle; }

    // video (VideoStartPauseResumeKey / VideoStopKey hotkeys)
    void VideoStartPauseResume();
    void VideoStop();
    void VideoCancel();
    void VideoToggleZoom();                 // C15 (hotkey during the recording)
    void VideoOverlayMenu(POINT screen);    // C07: cursor highlight, clicks, keys
    void OnRecorderNote(WPARAM note);

    // common output (also used by the clipboard capture and the clipboard monitor)
    // layers stored in the library instead of the plain image: windows as objects and/or captured links
    struct Layers { Image base; XmlNode objects{ "Objects" }; std::map<std::string, Image> embedded; };
    void Output(const Image& img, const CaptureInfo& info, const Layers* layers = nullptr);
    void DeliverToEditor(const std::wstring& path);

    std::function<void(const std::wstring& title, const std::wstring& text)> notify;   // tray balloon
    std::function<void(bool busy)> onBusyChanged;
    std::function<void(const Preset&)> onPresetVolumes;   // volume panel of the HUD changed (the preset remembers it)
    // outputs handled by the capture window (pins, QR results, overlay, Office, upload)
    std::function<void(const Image&, const std::wstring& title, const POINT* at)> onPin;
    std::function<void(const Image&)> onReadQr;
    std::function<void(const Image&, const std::wstring& libPath, bool video, const std::wstring& title)> onOverlay;
    std::function<void(const Image&, int target)> onOffice;
    std::function<void(const Image&, const std::wstring& libPath, const std::wstring& title)> onUpload;
    Recorder recorder;

private:
    void SetState(State s);
    void BeginInput();
    void OnInputSelected(const SelectResult& r);
    void InputCaptured(Image img, const CaptureInfo& info, std::vector<CapturedLink> links = {}, POINT origin = {});
    bool CaptureWindowsAsObjects(RECT area, const Image& screen, Layers& out);
    void InputCanceled();
    void StartVideoHud(RECT area);
    void StartRecording();
    void FinishVideo(bool ok);
    void ShowDelay(int seconds, std::function<void()> then);
    void GrabText(const Image& img);
    void StopWebcam();
    Image FreezeScreen(RECT vs, bool withCursor);
    Image ScrollCapture(HWND top, HWND child, POINT at);
    Image ExtendedWindowCapture(HWND top);
    bool CaptureMenus(Image& out);

    HWND frame_ = nullptr;
    ipc::Notify* ipc_ = nullptr;
    State state_ = Idle;
    ULONGLONG stateSince_ = 0;    // when the state last changed
    bool UiAlive();
    Preset preset_;
    Image frozen_;
    RECT vs_{};
    RecorderHud hud_;
    PanoramicSession pano_;
    WebcamOverlay webcam_;
    RECT videoArea_{};
    HWND delayWnd_ = nullptr;
    CleanDesktop clean_;          // icons hidden / notifications off during the capture (C16)
    POINT origin_{};              // screen position of the last captured area (pins open where it was)
    bool haveOrigin_ = false;
    // captured cursor kept apart, to become an editable object in the Editor (C25, option CursorAsObject)
    Image cursorImg_;
    POINT cursorPos_{};           // screen position of the cursor image's top left corner
};

// Windows 11 rounded corners of a captured window made transparent (C06); off for maximized windows and for windows
// that ask for square corners (DWMWA_WINDOW_CORNER_PREFERENCE)
void RoundWindowCorners(Image& img, HWND h);

} // namespace snap
