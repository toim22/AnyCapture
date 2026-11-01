// Screen recording — equivalent of TSCRec3 + AVFileWriter + AVStreamEncoder (ds_arch §12, comp_3)
//   video: BitBlt of the area → RGB32 samples → IMFSinkWriter (H.264, MP4), optional hardware encoding
//   audio: WASAPI (microphone + system loopback) → mixer with separate mute → AAC
//   competitor feature request: cursor highlight, click ripples and keystrokes drawn in the video (C07), crash-safe
//   recording into a fragmented MP4 remuxed at the end (C14), zoom following the pointer (C15), audio only (C32)
#pragma once
#include "Image.h"

namespace snap {

struct RecordSettings {
    RECT rect{};                 // screen area
    bool cursor = true;
    bool microphone = true;
    bool systemAudio = true;
    bool micMuted = false, sysMuted = false;
    bool autoGain = true;        // automatic microphone gain (otherwise only the manual volume)
    int micVolume = 100, sysVolume = 100;   // % (0–200)
    int fps = 30;
    int quality = 2;             // 0..3
    bool downscale1080 = true;
    bool hardware = true;
    std::wstring path;
    // drawn in the video (C07)
    bool highlightCursor = false;
    COLORREF highlightColor = RGB(255, 214, 0);
    int highlightSize = 44, highlightOpacity = 35;    // px (at the screen size), %
    bool clickEffects = false;
    COLORREF leftClickColor = RGB(229, 50, 45), rightClickColor = RGB(0, 120, 215);
    int keyMode = 0;             // keys on screen: 0 off, 1 shortcuts only (Ctrl/Alt/Win + key, F-keys), 2 every key
    bool autoZoom = false;       // zooms in on each click for a moment (C15)
    int zoomPercent = 200;
    bool crashSafe = true;       // fragmented MP4 while recording: a crash leaves a recoverable file (C14)
    bool audioOnly = false;      // C32: no video; .m4a (AAC) or .mp3 by the path's extension
};

class Recorder {
public:
    enum State { Idle, Recording, Paused, Stopping };   // mirrors the VideoRecording states (comp_3 §4.1)
    bool Start(const RecordSettings& s, HWND notify, UINT notifyMsg);
    void Pause();
    void Resume();
    bool Stop();                 // finalizes the MP4
    void Cancel();               // stops and deletes
    State state() const { return state_; }
    double Elapsed() const;      // seconds recorded (without pauses)
    void SetMicMuted(bool m) { micMuted_ = m; }
    void SetSysMuted(bool m) { sysMuted_ = m; }
    bool micMuted() const { return micMuted_; }
    bool sysMuted() const { return sysMuted_; }
    bool hasMic() const { return hasMic_; }
    bool hasSys() const { return hasSys_; }
    // levels for the meters, after the volume (and the automatic gain) are applied
    float micLevel() const { return std::min(1.0f, micLevel_ * micGainNow_ * micVol_ / 100.0f); }
    float sysLevel() const { return std::min(1.0f, sysLevel_ * sysVol_ / 100.0f); }
    // volume of each source in % (0–200) and automatic microphone gain: they also change during the recording
    void SetMicVolume(int v) { micVol_ = std::clamp(v, 0, 200); }
    void SetSysVolume(int v) { sysVol_ = std::clamp(v, 0, 200); }
    void SetAutoGain(bool on) { autoGain_ = on; }
    int micVolume() const { return micVol_; }
    int sysVolume() const { return sysVol_; }
    bool autoGain() const { return autoGain_; }
    // before the recording: opens the sources only for the meters (nothing is written); Start() closes it
    void StartMonitor(bool mic, bool sys);
    void StopMonitor();
    bool monitoring() const { return monitoring_; }
    const std::wstring& path() const { return settings_.path; }
    Image TakeSnapshot();        // image of the area during the recording
    Image firstFrame;
    std::wstring lastError;
    ~Recorder();
    // live from the HUD (C07, C15)
    void SetOverlay(bool highlight, bool clicks, int keyMode) { highlight_ = highlight; clicks_ = clicks; keyMode_ = keyMode; }
    bool highlightOn() const { return highlight_; }
    bool clicksOn() const { return clicks_; }
    int keyMode() const { return keyMode_; }
    void ToggleZoom() { zoomOn_ = !zoomOn_; }
    bool zoomed() const { return zoomOn_; }
    // crash recovery (C14): recordings interrupted by a crash are left in RecoveryDir() with a ".target" file naming the
    // video they were meant to become; RecoverInterrupted() finishes them (at startup) and returns the recovered videos
    static std::wstring RecoveryDir();
    static std::vector<std::wstring> RecoverInterrupted();
    static bool RemuxToMp4(const std::wstring& in, const std::wstring& out, std::wstring& err);

    // notifications sent to notifyMsg (wParam)
    enum Note { NoteDiskLow = 1, NoteDiskFull = 2, NoteError = 3 };
private:
    struct AudioSource;
    void VideoThread();
    void InputThread();          // low-level mouse/keyboard hooks: clicks and keys for the overlays (C07, C15)
    void AudioMixThread();
    LONGLONG Now100ns() const;   // recording clock (excludes pauses)
    bool WriteSample(DWORD stream, const void* data, DWORD len, LONGLONG time, LONGLONG dur);

    RecordSettings settings_;
    HWND notify_ = nullptr; UINT notifyMsg_ = 0;
    std::atomic<State> state_{ Idle };
    std::atomic<bool> running_{ false };
    std::atomic<bool> micMuted_{ false }, sysMuted_{ false };
    std::atomic<float> micLevel_{ 0 }, sysLevel_{ 0 };
    std::atomic<int> micVol_{ 100 }, sysVol_{ 100 };
    std::atomic<bool> autoGain_{ true };
    std::atomic<float> micGainNow_{ 1 };          // current automatic gain (for the meter)
    std::atomic<bool> monitoring_{ false };
    std::thread monitorThread_;
    bool hasMic_ = false, hasSys_ = false;
    void* writer_ = nullptr;     // IMFSinkWriter*
    DWORD videoStream_ = 0, audioStream_ = 0;
    bool audioEnabled_ = false;
    std::mutex writerMutex_;
    std::thread videoThread_, audioThread_;
    std::shared_ptr<AudioSource> mic_, sys_;
    int outW_ = 0, outH_ = 0;
    LARGE_INTEGER qpcFreq_{}, qpcStart_{};
    mutable std::mutex clockMutex_;
    LONGLONG pausedTotal_ = 0, pauseStart_ = 0;
    std::atomic<bool> highlight_{ false }, clicks_{ false }, zoomOn_{ false };
    std::atomic<int> keyMode_{ 0 };
    std::wstring tempPath_;      // fragmented recording being written (crash-safe mode), "" = straight to 'path'
    // input events seen by the hooks, consumed by the video thread (quick taps are not lost between two frames)
public:
    struct InputEvent { bool key; UINT code; POINT pt; bool ctrl, alt, shift, win; ULONGLONG tick; };
private:
    std::mutex inputMutex_;
    std::vector<InputEvent> input_;
    std::thread inputThread_;
    DWORD inputThreadId_ = 0;
    friend LRESULT CALLBACK RecorderKeyHook(int, WPARAM, LPARAM);
    friend LRESULT CALLBACK RecorderMouseHook(int, WPARAM, LPARAM);
};

} // namespace snap
