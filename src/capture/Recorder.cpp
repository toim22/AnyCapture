#include "Recorder.h"
#include "Library.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <deque>
#include <future>
#include <cmath>
#include <gdiplus.h>

using Microsoft::WRL::ComPtr;

namespace snap {

static constexpr int kRate = 48000;     // mixer/AAC rate
static constexpr LONGLONG kHns = 10000000;

// ---------------------------------------------------------------- WASAPI audio source
struct Recorder::AudioSource {
    bool loopback = false;
    std::atomic<bool> run{ false };
    std::atomic<float>* level = nullptr;
    std::thread th;
    std::mutex m;
    std::deque<float> buf;               // interleaved stereo at 48 kHz
    std::wstring deviceName;
    // linear resampling
    double pos = 0; float prevL = 0, prevR = 0;
    std::promise<bool> ready_;

    bool Start(bool lb, std::atomic<float>* lvl) {
        loopback = lb; level = lvl; run = true;
        auto fut = ready_.get_future();
        th = std::thread([this] { Loop(ready_); });
        bool ok = fut.get();
        if (!ok && th.joinable()) th.join();
        return ok;
    }
    void Stop() { run = false; if (th.joinable()) th.join(); }

    size_t Available() { std::lock_guard<std::mutex> lk(m); return buf.size() / 2; }

    void Loop(std::promise<bool>& ready) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool signalled = false;
        auto fail = [&](const wchar_t* what, HRESULT hr) {
            Log(L"Audio (%s): %s failed 0x%08X", loopback ? L"system" : L"microphone", what, hr);
            if (!signalled) { ready.set_value(false); signalled = true; }
        };
        do {
            ComPtr<IMMDeviceEnumerator> en;
            HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
            if (FAILED(hr)) { fail(L"MMDeviceEnumerator", hr); break; }
            ComPtr<IMMDevice> dev;
            hr = en->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &dev);
            if (FAILED(hr)) { fail(L"GetDefaultAudioEndpoint", hr); break; }
            ComPtr<IAudioClient> client;
            hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.GetAddressOf());
            if (FAILED(hr)) { fail(L"Activate", hr); break; }
            WAVEFORMATEX* wf = nullptr;
            hr = client->GetMixFormat(&wf);
            if (FAILED(hr)) { fail(L"GetMixFormat", hr); break; }
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0, 2000000, 0, wf, nullptr);
            if (FAILED(hr)) {
                // some drivers reject their own mix format: try stereo float 48 kHz with WASAPI conversion
                Log(L"Audio (%s): mix format rejected (0x%08X), trying automatic conversion", loopback ? L"system" : L"microphone", hr);
                client.Reset();
                if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.GetAddressOf()))) { CoTaskMemFree(wf); fail(L"Activate", hr); break; }
                WAVEFORMATEX* std = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
                *std = {};
                std->wFormatTag = WAVE_FORMAT_IEEE_FLOAT; std->nChannels = 2; std->nSamplesPerSec = 48000; std->wBitsPerSample = 32;
                std->nBlockAlign = 8; std->nAvgBytesPerSec = 48000 * 8;
                hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0) |
                                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 2000000, 0, std, nullptr);
                CoTaskMemFree(wf);
                wf = std;
                if (FAILED(hr)) { CoTaskMemFree(wf); fail(L"Initialize", hr); break; }
            }
            ComPtr<IAudioCaptureClient> cap;
            hr = client->GetService(IID_PPV_ARGS(&cap));
            if (FAILED(hr)) { CoTaskMemFree(wf); fail(L"GetService", hr); break; }
            bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
                isFloat = ((WAVEFORMATEXTENSIBLE*)wf)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            int ch = wf->nChannels, bits = wf->wBitsPerSample, rate = wf->nSamplesPerSec, blk = wf->nBlockAlign;
            Log(L"Audio (%s): %d Hz, %d channels, %d bits%s", loopback ? L"system" : L"microphone", rate, ch, bits, isFloat ? L" float" : L"");
            CoTaskMemFree(wf);
            hr = client->Start();
            if (FAILED(hr)) { fail(L"Start", hr); break; }
            ready.set_value(true); signalled = true;

            std::vector<float> st;   // stereo frames at the original rate
            while (run) {
                Sleep(10);
                UINT32 pkt = 0;
                while (SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt) {
                    BYTE* data; UINT32 frames; DWORD flags;
                    if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                    st.resize((size_t)frames * 2);
                    float peak = 0;
                    for (UINT32 f = 0; f < frames; f++) {
                        float s[2] = { 0, 0 };
                        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                            for (int c = 0; c < std::min(ch, 2); c++) {
                                const BYTE* p = data + (size_t)f * blk + (size_t)c * (bits / 8);
                                float v = 0;
                                if (isFloat && bits == 32) v = *(const float*)p;
                                else if (bits == 16) v = *(const int16_t*)p / 32768.0f;
                                else if (bits == 24) v = ((int)((p[0] << 8) | (p[1] << 16) | (p[2] << 24)) >> 8) / 8388608.0f;
                                else if (bits == 32) v = *(const int32_t*)p / 2147483648.0f;
                                s[c] = v;
                            }
                            if (ch == 1) s[1] = s[0];
                        }
                        st[(size_t)f * 2] = s[0]; st[(size_t)f * 2 + 1] = s[1];
                        peak = std::max(peak, std::max(std::fabs(s[0]), std::fabs(s[1])));
                    }
                    cap->ReleaseBuffer(frames);
                    if (level) level->store(std::max(peak, level->load() * 0.85f));
                    Push(st, frames, rate);
                }
                if (level) level->store(level->load() * 0.9f);
            }
            client->Stop();
        } while (false);
        if (!signalled) ready.set_value(false);
        CoUninitialize();
    }

    void Push(const std::vector<float>& s, UINT32 n, int rate) {
        std::lock_guard<std::mutex> lk(m);
        if (rate == kRate) { buf.insert(buf.end(), s.begin(), s.begin() + (size_t)n * 2); }
        else {
            double ratio = (double)rate / kRate;
            while (pos <= (double)n - 1) {
                int i = (int)std::floor(pos);
                double fr = pos - i;
                float aL = i < 0 ? prevL : s[(size_t)i * 2], aR = i < 0 ? prevR : s[(size_t)i * 2 + 1];
                float bL = s[(size_t)(i + 1) * 2], bR = s[(size_t)(i + 1) * 2 + 1];
                buf.push_back((float)(aL + (bL - aL) * fr));
                buf.push_back((float)(aR + (bR - aR) * fr));
                pos += ratio;
            }
            pos -= n;
            if (n) { prevL = s[(size_t)(n - 1) * 2]; prevR = s[(size_t)(n - 1) * 2 + 1]; }
        }
        size_t maxFrames = kRate * 5;                 // never buffers more than 5 s
        if (buf.size() > maxFrames * 2) buf.erase(buf.begin(), buf.begin() + (buf.size() - maxFrames * 2));
    }

    // takes up to n frames; returns how many came
    size_t Pop(float* out, size_t n) {
        std::lock_guard<std::mutex> lk(m);
        size_t avail = buf.size() / 2;
        // drift: if the buffer grew too much (> n + 200 ms), drops the oldest excess
        if (avail > n + kRate / 5) {
            size_t drop = avail - n - kRate / 20;
            buf.erase(buf.begin(), buf.begin() + drop * 2);
            avail -= drop;
        }
        size_t take = std::min(n, avail);
        for (size_t i = 0; i < take * 2; i++) out[i] = buf[i];
        buf.erase(buf.begin(), buf.begin() + take * 2);
        return take;
    }
    void Clear() { std::lock_guard<std::mutex> lk(m); buf.clear(); }
};

// ---------------------------------------------------------------- Recorder
Recorder::~Recorder() { if (running_) Cancel(); StopMonitor(); }

LONGLONG Recorder::Now100ns() const {
    std::lock_guard<std::mutex> lk(clockMutex_);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    LONGLONG t = (now.QuadPart - qpcStart_.QuadPart) * kHns / qpcFreq_.QuadPart;
    LONGLONG paused = pausedTotal_;
    if (state_ == Paused) paused += t - pauseStart_;
    return std::max<LONGLONG>(0, t - paused);
}

double Recorder::Elapsed() const { return state_ == Idle ? 0 : Now100ns() / (double)kHns; }

static HRESULT CreateWriter(const RecordSettings& s, const std::wstring& path, bool fragmented, int w, int h, bool hw, bool audio,
                            ComPtr<IMFSinkWriter>& out, DWORD& vStream, DWORD& aStream) {
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 3);
    attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, hw ? TRUE : FALSE);
    const bool mp3 = s.audioOnly && FileExt(path) == L".mp3";
    attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, mp3 ? MFTranscodeContainerType_MP3 : fragmented ? MFTranscodeContainerType_FMPEG4 : MFTranscodeContainerType_MPEG4);
    ComPtr<IMFSinkWriter> wr;
    HRESULT hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attr.Get(), &wr);
    if (FAILED(hr)) return hr;
    if (s.audioOnly) {                          // audio only (C32): AAC in .m4a or MP3
        ComPtr<IMFMediaType> aout, ain;
        MFCreateMediaType(&aout);
        aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        aout->SetGUID(MF_MT_SUBTYPE, mp3 ? MFAudioFormat_MP3 : MFAudioFormat_AAC);
        aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, mp3 ? 24000 : 24000);
        if (!mp3) { aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16); aout->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0); }
        if (FAILED(hr = wr->AddStream(aout.Get(), &aStream))) return hr;
        MFCreateMediaType(&ain);
        ain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        ain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        ain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        ain->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        ain->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        ain->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        ain->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * 4);
        if (FAILED(hr = wr->SetInputMediaType(aStream, ain.Get(), nullptr))) return hr;
        if (FAILED(hr = wr->BeginWriting())) return hr;
        out = wr;
        return S_OK;
    }

    static const double bppTable[4] = { 0.04, 0.07, 0.10, 0.16 };
    UINT32 bitrate = (UINT32)std::clamp(bppTable[std::clamp(s.quality, 0, 3)] * w * h * s.fps, 400000.0, 40000000.0);
    ComPtr<IMFMediaType> vout, vin;
    MFCreateMediaType(&vout);
    vout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vout->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    vout->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    vout->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vout.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(vout.Get(), MF_MT_FRAME_RATE, s.fps, 1);
    MFSetAttributeRatio(vout.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr = wr->AddStream(vout.Get(), &vStream))) return hr;
    MFCreateMediaType(&vin);
    vin->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vin->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    vin->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vin.Get(), MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(vin.Get(), MF_MT_FRAME_RATE, s.fps, 1);
    MFSetAttributeRatio(vin.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr = wr->SetInputMediaType(vStream, vin.Get(), nullptr))) return hr;

    if (audio) {
        ComPtr<IMFMediaType> aout, ain;
        MFCreateMediaType(&aout);
        aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        aout->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
        if (FAILED(hr = wr->AddStream(aout.Get(), &aStream))) return hr;
        MFCreateMediaType(&ain);
        ain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        ain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        ain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        ain->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        ain->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        ain->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        ain->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * 4);
        if (FAILED(hr = wr->SetInputMediaType(aStream, ain.Get(), nullptr))) return hr;
    }
    if (FAILED(hr = wr->BeginWriting())) return hr;
    out = wr;
    return S_OK;
}

// level meters before the recording: the sources capture and the buffers are emptied (nothing is written); the
// automatic gain is estimated from the peak, as the mixer does
void Recorder::StartMonitor(bool mic, bool sys) {
    if (state_ != Idle || monitoring_) return;
    if (mic) { mic_ = std::make_shared<AudioSource>(); hasMic_ = mic_->Start(false, &micLevel_); if (!hasMic_) mic_.reset(); }
    if (sys) { sys_ = std::make_shared<AudioSource>(); hasSys_ = sys_->Start(true, &sysLevel_); if (!hasSys_) sys_.reset(); }
    if (!mic_ && !sys_) return;
    monitoring_ = true;
    monitorThread_ = std::thread([this] {
        float env = 0;
        while (monitoring_) {
            Sleep(50);
            if (mic_) mic_->Clear();
            if (sys_) sys_->Clear();
            env = std::max((float)micLevel_, env * 0.97f);
            micGainNow_ = autoGain_ ? std::clamp(0.5f / std::max(env, 1e-4f), 1.0f, 8.0f) : 1.0f;
        }
    });
}

void Recorder::StopMonitor() {
    if (!monitoring_ && !monitorThread_.joinable()) return;
    monitoring_ = false;
    if (monitorThread_.joinable()) monitorThread_.join();
    if (mic_) mic_->Stop();
    if (sys_) sys_->Stop();
    mic_.reset(); sys_.reset();
    hasMic_ = hasSys_ = false;
    micLevel_ = 0; sysLevel_ = 0; micGainNow_ = 1;
}

// ---------------------------------------------------------------- input hooks (C07): every click and key press, even quick ones
static Recorder* g_hookRec = nullptr;

LRESULT CALLBACK RecorderKeyHook(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && g_hookRec && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) {
        auto* k = (KBDLLHOOKSTRUCT*)l;
        Recorder::InputEvent e{ true, k->vkCode, {}, GetAsyncKeyState(VK_CONTROL) < 0, GetAsyncKeyState(VK_MENU) < 0 || (k->flags & LLKHF_ALTDOWN),
                                GetAsyncKeyState(VK_SHIFT) < 0, GetAsyncKeyState(VK_LWIN) < 0 || GetAsyncKeyState(VK_RWIN) < 0, GetTickCount64() };
        std::lock_guard<std::mutex> lk(g_hookRec->inputMutex_);
        if (g_hookRec->input_.size() < 256) g_hookRec->input_.push_back(e);
    }
    return CallNextHookEx(nullptr, code, w, l);
}

LRESULT CALLBACK RecorderMouseHook(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && g_hookRec && (w == WM_LBUTTONDOWN || w == WM_RBUTTONDOWN)) {
        auto* m = (MSLLHOOKSTRUCT*)l;
        Recorder::InputEvent e{ false, (UINT)(w == WM_RBUTTONDOWN ? VK_RBUTTON : VK_LBUTTON), m->pt, false, false, false, false, GetTickCount64() };
        std::lock_guard<std::mutex> lk(g_hookRec->inputMutex_);
        if (g_hookRec->input_.size() < 256) g_hookRec->input_.push_back(e);
    }
    return CallNextHookEx(nullptr, code, w, l);
}

void Recorder::InputThread() {
    inputThreadId_ = GetCurrentThreadId();
    HHOOK kh = SetWindowsHookExW(WH_KEYBOARD_LL, RecorderKeyHook, GetModuleHandleW(nullptr), 0);
    HHOOK mh = SetWindowsHookExW(WH_MOUSE_LL, RecorderMouseHook, GetModuleHandleW(nullptr), 0);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {}
    if (kh) UnhookWindowsHookEx(kh);
    if (mh) UnhookWindowsHookEx(mh);
}

bool Recorder::Start(const RecordSettings& s, HWND notify, UINT notifyMsg) {
    if (state_ != Idle) return false;
    StopMonitor();
    settings_ = s;
    micVol_ = std::clamp(s.micVolume, 0, 200); sysVol_ = std::clamp(s.sysVolume, 0, 200); autoGain_ = s.autoGain;
    micGainNow_ = 1;
    notify_ = notify; notifyMsg_ = notifyMsg;
    micMuted_ = s.micMuted; sysMuted_ = s.sysMuted;
    lastError.clear();
    firstFrame = Image();

    // output size: even, ≤ 1080 lines (optional) and within the H.264 encoder limit (4096×2304)
    int w = s.rect.right - s.rect.left, h = s.rect.bottom - s.rect.top;
    if (s.audioOnly) { w = h = 32; }
    if (w < 16 || h < 16) { lastError = Tr(L"Área de gravação muito pequena."); return false; }
    highlight_ = s.highlightCursor; clicks_ = s.clickEffects; keyMode_ = std::clamp(s.keyMode, 0, 2); zoomOn_ = false;
    double sc = 1.0;
    if (s.downscale1080 && h > 1080) sc = 1080.0 / h;
    sc = std::min({ sc, 4096.0 / w, 2304.0 / h });
    outW_ = std::max(32, ((int)(w * sc)) & ~1);
    outH_ = std::max(32, ((int)(h * sc)) & ~1);

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) { lastError = Tr(L"Media Foundation indisponível."); return false; }

    // audio sources
    if (s.microphone) { mic_ = std::make_shared<AudioSource>(); hasMic_ = mic_->Start(false, &micLevel_); if (!hasMic_) mic_.reset(); }
    if (s.systemAudio) { sys_ = std::make_shared<AudioSource>(); hasSys_ = sys_->Start(true, &sysLevel_); if (!hasSys_) sys_.reset(); }
    audioEnabled_ = hasMic_ || hasSys_;
    if (s.audioOnly && !audioEnabled_) {
        lastError = Tr(L"Nenhuma fonte de áudio disponível (ligue o microfone ou o áudio do sistema).");
        MFShutdown();
        return false;
    }

    // crash-safe (C14): a fragmented MP4 in the recovery folder, remuxed into 'path' when the recording ends
    tempPath_.clear();
    if (s.crashSafe && !s.audioOnly) {
        std::wstring dir = RecoveryDir();
        tempPath_ = dir + L"\\" + NewGuidString() + L".mp4";
        HANDLE t = CreateFileW((tempPath_ + L".target").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (t != INVALID_HANDLE_VALUE) {
            std::string u = WToUtf8(s.path);
            DWORD wr = 0; WriteFile(t, u.data(), (DWORD)u.size(), &wr, nullptr);
            CloseHandle(t);
        } else tempPath_.clear();
    }
    ComPtr<IMFSinkWriter> wr;
    auto create = [&](bool hw) {
        HRESULT r = E_FAIL;
        if (!tempPath_.empty()) {
            r = CreateWriter(settings_, tempPath_, true, outW_, outH_, hw, audioEnabled_, wr, videoStream_, audioStream_);
            if (SUCCEEDED(r)) return r;
            Log(L"Recorder: fragmented MP4 rejected (0x%08X), recording straight to MP4", r);
            wr.Reset();
            DeleteFileW(tempPath_.c_str()); DeleteFileW((tempPath_ + L".target").c_str());
            tempPath_.clear();
        }
        return CreateWriter(settings_, s.path, false, outW_, outH_, hw, audioEnabled_, wr, videoStream_, audioStream_);
    };
    HRESULT hr = create(s.hardware);
    if (FAILED(hr) && s.hardware && !s.audioOnly) {
        Log(L"Recorder: hardware encoder rejected (0x%08X), trying software", hr);
        wr.Reset();
        DeleteFileW(s.path.c_str());
        hr = create(false);
    }
    if (FAILED(hr)) {
        lastError = Format(Tr(L"Não foi possível iniciar o codificador MP4 (0x%08X)."), hr);
        Log(L"Recorder: %s", lastError.c_str());
        if (mic_) mic_->Stop(); if (sys_) sys_->Stop();
        mic_.reset(); sys_.reset();
        MFShutdown();
        return false;
    }
    writer_ = wr.Detach();
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&qpcStart_);
    pausedTotal_ = 0;
    state_ = Recording;
    running_ = true;
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);   // keeps the system from sleeping
    if (!s.audioOnly) {
        { std::lock_guard<std::mutex> lk(inputMutex_); input_.clear(); }
        g_hookRec = this;
        inputThread_ = std::thread([this] { InputThread(); });
        videoThread_ = std::thread([this] { VideoThread(); });
    }
    if (audioEnabled_) audioThread_ = std::thread([this] { AudioMixThread(); });
    Log(L"Recorder: started %dx%d → %dx%d @%d fps, audio mic=%d sys=%d, %s", w, h, outW_, outH_, s.fps, hasMic_, hasSys_, s.path.c_str());
    return true;
}

void Recorder::Pause() {
    if (state_ != Recording) return;
    std::lock_guard<std::mutex> lk(clockMutex_);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    pauseStart_ = (now.QuadPart - qpcStart_.QuadPart) * kHns / qpcFreq_.QuadPart;
    state_ = Paused;
}

void Recorder::Resume() {
    if (state_ != Paused) return;
    {
        std::lock_guard<std::mutex> lk(clockMutex_);
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        LONGLONG t = (now.QuadPart - qpcStart_.QuadPart) * kHns / qpcFreq_.QuadPart;
        pausedTotal_ += t - pauseStart_;
        state_ = Recording;
    }
    if (mic_) mic_->Clear();
    if (sys_) sys_->Clear();
}

bool Recorder::WriteSample(DWORD stream, const void* data, DWORD len, LONGLONG time, LONGLONG dur) {
    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(MFCreateMemoryBuffer(len, &buf))) return false;
    BYTE* p = nullptr;
    buf->Lock(&p, nullptr, nullptr);
    memcpy(p, data, len);
    buf->Unlock();
    buf->SetCurrentLength(len);
    ComPtr<IMFSample> smp;
    MFCreateSample(&smp);
    smp->AddBuffer(buf.Get());
    smp->SetSampleTime(time);
    smp->SetSampleDuration(dur);
    std::lock_guard<std::mutex> lk(writerMutex_);
    HRESULT hr = ((IMFSinkWriter*)writer_)->WriteSample(stream, smp.Get());
    if (FAILED(hr)) Log(L"Recorder: WriteSample(%u) 0x%08X", stream, hr);
    return SUCCEEDED(hr);
}

void Recorder::VideoThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    timeBeginPeriod(1);
    const RECT r = settings_.rect;
    const int sw = r.right - r.left, sh = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = outW_; bi.bmiHeader.biHeight = outH_;     // bottom-up = MF default RGB32
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, dib);
    const bool scaled = outW_ != sw || outH_ != sh;
    SetStretchBltMode(mem, HALFTONE); SetBrushOrgEx(mem, 0, 0, nullptr);
    const double fx = (double)outW_ / sw, fy = (double)outH_ / sh;
    const LONGLONG frameDur = kHns / settings_.fps;
    const DWORD bytes = (DWORD)outW_ * outH_ * 4;
    LONGLONG lastTs = -1;
    ULONGLONG lastDiskCheck = 0;
    bool warnedDisk = false;
    // overlays (C07) and zoom (C15)
    struct Ripple { POINT p; ULONGLONG t0; bool right; };
    std::vector<Ripple> ripples;
    bool wasL = false, wasR = false;
    bool keyDown[256] = {};
    std::wstring keyText;
    ULONGLONG keyTime = 0;
    double zoomNow = 1.0, zcx = (r.left + r.right) / 2.0, zcy = (r.top + r.bottom) / 2.0;
    ULONGLONG autoZoomUntil = 0;
    auto keyName = [](UINT vk) -> std::wstring {
        switch (vk) {
        case VK_RETURN: return L"Enter"; case VK_ESCAPE: return L"Esc"; case VK_SPACE: return Tr(L"Espaço"); case VK_TAB: return L"Tab";
        case VK_BACK: return L"⌫"; case VK_DELETE: return L"Del"; case VK_LEFT: return L"←"; case VK_RIGHT: return L"→";
        case VK_UP: return L"↑"; case VK_DOWN: return L"↓"; case VK_HOME: return L"Home"; case VK_END: return L"End";
        case VK_PRIOR: return L"PgUp"; case VK_NEXT: return L"PgDn"; case VK_INSERT: return L"Ins"; case VK_SNAPSHOT: return L"PrtSc";
        }
        if (vk >= VK_F1 && vk <= VK_F24) return Format(L"F%u", vk - VK_F1 + 1);
        if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::wstring(1, (wchar_t)vk);
        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        wchar_t b[64] = L"";
        if (sc && GetKeyNameTextW((LONG)(sc << 16), b, 64) > 0) return b;
        return Format(L"#%u", vk);
    };

    while (running_) {
        if (state_ == Paused) { Sleep(10); continue; }
        LONGLONG t = Now100ns();
        ULONGLONG tick = GetTickCount64();
        CURSORINFO ci{ sizeof(ci) };
        bool haveCursor = GetCursorInfo(&ci) != 0;
        // clicks and keys seen by the hooks since the last frame (C07, and C15 auto zoom)
        std::vector<InputEvent> events;
        { std::lock_guard<std::mutex> lk(inputMutex_); events.swap(input_); }
        for (auto& e : events) {
            if (e.key) continue;
            // (the hook's time may be a little after this frame's clock: the age must not go below zero)
            if (clicks_) ripples.push_back({ e.pt, std::min(e.tick, tick), e.code == VK_RBUTTON });
            if (settings_.autoZoom) autoZoomUntil = e.tick + 2500;
        }
        (void)wasL; (void)wasR;
        // zoom (C15): eases toward the target and follows the pointer
        double target = (zoomOn_ || tick < autoZoomUntil) ? std::clamp(settings_.zoomPercent, 120, 400) / 100.0 : 1.0;
        zoomNow += (target - zoomNow) * 0.18;
        if (std::fabs(target - zoomNow) < 0.004) zoomNow = target;
        if (haveCursor) { zcx += (ci.ptScreenPos.x - zcx) * 0.2; zcy += (ci.ptScreenPos.y - zcy) * 0.2; }
        double srcW = sw / zoomNow, srcH = sh / zoomNow;
        double srcL = std::clamp(zcx - srcW / 2, (double)r.left, r.right - srcW), srcT = std::clamp(zcy - srcH / 2, (double)r.top, r.bottom - srcH);
        if (zoomNow == 1.0) { srcL = r.left; srcT = r.top; }
        const double kx = outW_ / srcW, ky = outH_ / srcH;          // screen → output
        auto toOut = [&](POINT p) { return Gdiplus::PointF((float)((p.x - srcL) * kx), (float)((p.y - srcT) * ky)); };
        if (zoomNow != 1.0) StretchBlt(mem, 0, 0, outW_, outH_, screen, (int)std::lround(srcL), (int)std::lround(srcT), (int)std::lround(srcW), (int)std::lround(srcH), SRCCOPY | CAPTUREBLT);
        else if (scaled) StretchBlt(mem, 0, 0, outW_, outH_, screen, r.left, r.top, sw, sh, SRCCOPY | CAPTUREBLT);
        else BitBlt(mem, 0, 0, outW_, outH_, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
        const bool overlays = highlight_ || !ripples.empty() || keyMode_ > 0;
        std::unique_ptr<Gdiplus::Graphics> gg;
        if (overlays) { gg = std::make_unique<Gdiplus::Graphics>(mem); gg->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); }
        const float px = (float)std::min(kx, ky);                  // screen pixel → output pixels
        if (highlight_ && haveCursor && (ci.flags & CURSOR_SHOWING)) {          // halo under the cursor (C07)
            Gdiplus::PointF c = toOut(ci.ptScreenPos);
            float rad = settings_.highlightSize / 2.0f * px;
            COLORREF hc = settings_.highlightColor;
            Gdiplus::SolidBrush b(Gdiplus::Color((BYTE)(std::clamp(settings_.highlightOpacity, 5, 100) * 255 / 100), GetRValue(hc), GetGValue(hc), GetBValue(hc)));
            gg->FillEllipse(&b, c.X - rad, c.Y - rad, 2 * rad, 2 * rad);
        }
        if (settings_.cursor && haveCursor && (ci.flags & CURSOR_SHOWING)) {
            ICONINFO ii{};
            if (GetIconInfo(ci.hCursor, &ii)) {
                if (gg) gg->Flush(Gdiplus::FlushIntentionSync);
                // the cursor follows the scale of the picture (smaller when downscaled, bigger when zoomed in)
                Gdiplus::PointF c = toOut(ci.ptScreenPos);
                bool custom = std::fabs(px - 1.0f) > 0.01f;
                int cs = (int)std::lround(GetSystemMetrics(SM_CXCURSOR) * px);
                int cx = (int)std::lround(c.X - ii.xHotspot * px), cy = (int)std::lround(c.Y - ii.yHotspot * px);
                DrawIconEx(mem, cx, cy, ci.hCursor, custom ? cs : 0, custom ? cs : 0, 0, nullptr, DI_NORMAL);
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
            }
        }
        // click ripples (C07): an expanding ring over a fading disc for 700 ms, red for the left button and blue for the
        // right one (the first version, thin and short, went unnoticed)
        if (!ripples.empty()) {
            const float life = 700.0f;
            ripples.erase(std::remove_if(ripples.begin(), ripples.end(), [&](const Ripple& rp) { return tick - rp.t0 > life; }), ripples.end());
            for (auto& rp : ripples) {
                float prog = (tick - rp.t0) / life, ease = 1 - (1 - prog) * (1 - prog);
                COLORREF cc = rp.right ? settings_.rightClickColor : settings_.leftClickColor;
                Gdiplus::PointF c = toOut(rp.p);
                float rad = (10 + 38 * ease) * px;
                Gdiplus::SolidBrush disc(Gdiplus::Color((BYTE)(90 * (1 - prog)), GetRValue(cc), GetGValue(cc), GetBValue(cc)));
                gg->FillEllipse(&disc, c.X - rad, c.Y - rad, 2 * rad, 2 * rad);
                Gdiplus::Pen pen(Gdiplus::Color((BYTE)(240 * (1 - prog)), GetRValue(cc), GetGValue(cc), GetBValue(cc)), std::max(3.0f, 5.0f * px));
                gg->DrawEllipse(&pen, c.X - rad, c.Y - rad, 2 * rad, 2 * rad);
                Gdiplus::SolidBrush dot(Gdiplus::Color((BYTE)(200 * (1 - prog)), GetRValue(cc), GetGValue(cc), GetBValue(cc)));
                gg->FillEllipse(&dot, c.X - 6 * px, c.Y - 6 * px, 12 * px, 12 * px);
            }
        }
        // keys on screen (C07): shortcuts only by default — typed text (passwords) stays out unless "every key"
        if (keyMode_ > 0) {
            for (auto& e : events) {
                if (!e.key) continue;
                UINT vk = e.code;
                bool ctrl = e.ctrl, alt = e.alt, shift = e.shift, win = e.win;
                if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || (vk >= VK_LSHIFT && vk <= VK_RMENU) ||
                    vk == VK_CAPITAL || vk == VK_NUMLOCK || vk >= 256) continue;
                bool down = true;
                if (down && !keyDown[vk]) {
                    bool fkey = vk >= VK_F1 && vk <= VK_F24;
                    bool shortcut = ctrl || alt || win || fkey;
                    if (keyMode_ == 2 || shortcut) {
                        std::wstring combo = (ctrl ? L"Ctrl + " : L"") + std::wstring(alt ? L"Alt + " : L"") + (win ? L"Win + " : L"") +
                                             ((shift && shortcut) ? L"Shift + " : L"") + keyName(vk);
                        bool typing = keyMode_ == 2 && !shortcut && combo.size() == 1;
                        if (typing && tick - keyTime < 1500 && keyText.size() < 28 && !keyText.empty() && keyText.find(L'+') == std::wstring::npos)
                            keyText += shift ? combo : std::wstring(1, (wchar_t)towlower(combo[0]));
                        else keyText = typing && !shift ? std::wstring(1, (wchar_t)towlower(combo[0])) : combo;
                        keyTime = tick;
                    }
                }
                (void)down;
            }
            if (!keyText.empty() && tick - keyTime < 1800) {
                float fade = tick - keyTime > 1400 ? 1 - (tick - keyTime - 1400) / 400.0f : 1;
                float em = std::max(14.0f, outH_ / 20.0f);
                Gdiplus::FontFamily ff(L"Segoe UI");
                Gdiplus::Font font(&ff, em, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::RectF box;
                gg->MeasureString(keyText.c_str(), -1, &font, Gdiplus::PointF(0, 0), &box);
                float bw = box.Width + em * 1.2f, bh = box.Height + em * 0.5f;
                Gdiplus::RectF bg((outW_ - bw) / 2, outH_ - bh - outH_ * 0.06f, bw, bh);
                Gdiplus::GraphicsPath path;
                float rr = bh * 0.3f;
                path.AddArc(bg.X, bg.Y, 2 * rr, 2 * rr, 180, 90); path.AddArc(bg.GetRight() - 2 * rr, bg.Y, 2 * rr, 2 * rr, 270, 90);
                path.AddArc(bg.GetRight() - 2 * rr, bg.GetBottom() - 2 * rr, 2 * rr, 2 * rr, 0, 90); path.AddArc(bg.X, bg.GetBottom() - 2 * rr, 2 * rr, 2 * rr, 90, 90);
                path.CloseFigure();
                Gdiplus::SolidBrush back(Gdiplus::Color((BYTE)(190 * fade), 20, 22, 26));
                gg->FillPath(&back, &path);
                Gdiplus::SolidBrush fore(Gdiplus::Color((BYTE)(255 * fade), 255, 255, 255));
                Gdiplus::StringFormat sf; sf.SetAlignment(Gdiplus::StringAlignmentCenter); sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                gg->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
                gg->DrawString(keyText.c_str(), -1, &font, bg, &sf, &fore);
            }
        }
        gg.reset();
        GdiFlush();
        // the BitBlt alpha channel is undefined: the encoder ignores it, but the thumbnail needs it opaque
        if (firstFrame.empty()) {
            Image f(outW_, outH_);
            for (int y = 0; y < outH_; y++) memcpy(f.row(y), (uint32_t*)bits + (size_t)(outH_ - 1 - y) * outW_, outW_ * 4);
            f.MakeOpaque();
            firstFrame = std::move(f);
        }
        LONGLONG ts = std::max(t, lastTs + 1);
        if (!WriteSample(videoStream_, bits, bytes, ts, frameDur)) {
            lastError = Tr(L"Falha ao gravar quadro de vídeo.");
            PostMessageW(notify_, notifyMsg_, NoteError, 0);
            break;
        }
        lastTs = ts;
        // disk space limits (warn / stop)
        ULONGLONG nowTick = GetTickCount64();
        if (nowTick - lastDiskCheck > 2000) {
            lastDiskCheck = nowTick;
            ULARGE_INTEGER freeBytes{};
            if (GetDiskFreeSpaceExW(DirOf(settings_.path).c_str(), &freeBytes, nullptr, nullptr)) {
                if (freeBytes.QuadPart < 200ull * 1024 * 1024) { PostMessageW(notify_, notifyMsg_, NoteDiskFull, 0); }
                else if (!warnedDisk && freeBytes.QuadPart < 1024ull * 1024 * 1024) { warnedDisk = true; PostMessageW(notify_, notifyMsg_, NoteDiskLow, 0); }
            }
        }
        // waits for the next frame on the time grid
        LONGLONG next = (t / frameDur + 1) * frameDur;
        for (;;) {
            if (!running_ || state_ == Paused) break;
            LONGLONG now = Now100ns();
            if (now >= next) break;
            LONGLONG ms = (next - now) / 10000;
            Sleep((DWORD)std::max<LONGLONG>(1, std::min<LONGLONG>(ms, 50)));
        }
    }
    SelectObject(mem, old);
    DeleteObject(dib); DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    timeEndPeriod(1);
    CoUninitialize();
}

void Recorder::AudioMixThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    LONGLONG written = 0;            // frames already written
    std::vector<float> a, b;
    std::vector<int16_t> out;
    // automatic microphone gain: laptop microphones arrive 20–30 dB below ideal. The peak envelope
    // targets -6 dBFS with a gain of 1× to 8×; it drops immediately (no clipping) and rises slowly (no noise "pumping").
    float micEnv = 0, micGain = 1;
    while (running_) {
        Sleep(20);
        if (state_ == Paused) { if (mic_) mic_->Clear(); if (sys_) sys_->Clear(); continue; }
        LONGLONG target = Now100ns() * kRate / kHns;
        LONGLONG n = target - written;
        if (n <= 0) continue;
        if (n > kRate) n = kRate;
        a.assign((size_t)n * 2, 0.0f); b.assign((size_t)n * 2, 0.0f);
        if (mic_) mic_->Pop(a.data(), (size_t)n);
        if (sys_) sys_->Pop(b.data(), (size_t)n);
        out.resize((size_t)n * 2);
        bool mm = micMuted_, sm = sysMuted_;
        if (mic_) {
            float blockPeak = 0;
            for (float s : a) blockPeak = std::max(blockPeak, std::fabs(s));
            micEnv = std::max(blockPeak, micEnv * 0.97f);
            if (autoGain_) {
                float want = std::clamp(0.5f / std::max(micEnv, 1e-4f), 1.0f, 8.0f);
                micGain = want < micGain ? want : micGain + (want - micGain) * 0.02f;
            } else micGain = 1;                                  // manual: only the volume
            micGainNow_ = micGain;
            float k = micGain * micVol_ / 100.0f;
            for (float& s : a) s *= k;
        }
        if (sys_ && sysVol_ != 100) { float k = sysVol_ / 100.0f; for (float& s : b) s *= k; }
        for (size_t i = 0; i < out.size(); i++) {
            float v = (mm ? 0.0f : a[i]) + (sm ? 0.0f : b[i]);
            v = v / (1.0f + std::fabs(v) * 0.15f) * 1.15f;      // soft limiter: mic + system together do not clip
            out[i] = (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
        }
        LONGLONG t0 = written * kHns / kRate, t1 = (written + n) * kHns / kRate;
        WriteSample(audioStream_, out.data(), (DWORD)(out.size() * 2), t0, t1 - t0);
        written += n;
    }
    CoUninitialize();
}

Image Recorder::TakeSnapshot() {
    HDC screen = GetDC(nullptr);
    Image img = Image::FromDC(screen, settings_.rect);
    ReleaseDC(nullptr, screen);
    return img;
}

bool Recorder::Stop() {
    if (state_ == Idle) return false;
    state_ = Stopping;
    running_ = false;
    if (videoThread_.joinable()) videoThread_.join();
    if (audioThread_.joinable()) audioThread_.join();
    if (inputThread_.joinable()) {
        while (!inputThreadId_ || !PostThreadMessageW(inputThreadId_, WM_QUIT, 0, 0)) Sleep(5);
        inputThread_.join();
        inputThreadId_ = 0;
    }
    g_hookRec = nullptr;
    if (mic_) mic_->Stop();
    if (sys_) sys_->Stop();
    mic_.reset(); sys_.reset();
    bool ok = false;
    if (writer_) {
        HRESULT hr = ((IMFSinkWriter*)writer_)->Finalize();
        ok = SUCCEEDED(hr);
        if (!ok) { Log(L"Recorder: Finalize 0x%08X", hr); lastError = Format(Tr(L"Falha ao finalizar o MP4 (0x%08X)."), hr); }
        ((IMFSinkWriter*)writer_)->Release();
        writer_ = nullptr;
    }
    // crash-safe recording (C14): the fragmented file becomes a regular MP4 (no re-encoding)
    if (!tempPath_.empty()) {
        if (ok) {
            std::wstring err;
            if (!RemuxToMp4(tempPath_, settings_.path, err)) {
                Log(L"Recorder: remux failed (%s), keeping the fragmented MP4", err.c_str());
                DeleteFileW(settings_.path.c_str());
                ok = MoveFileExW(tempPath_.c_str(), settings_.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
            }
        }
        DeleteFileW(tempPath_.c_str());
        DeleteFileW((tempPath_ + L".target").c_str());
        tempPath_.clear();
    }
    MFShutdown();
    SetThreadExecutionState(ES_CONTINUOUS);
    state_ = Idle;
    micLevel_ = 0; sysLevel_ = 0;
    Log(L"Recorder: stopped (%s)", ok ? L"ok" : L"error");
    return ok;
}

void Recorder::Cancel() {
    std::wstring temp = tempPath_;
    tempPath_.clear();                   // nothing to remux: the recording is thrown away
    Stop();
    if (!temp.empty()) { DeleteFileW(temp.c_str()); DeleteFileW((temp + L".target").c_str()); }
    DeleteFileW(settings_.path.c_str());
    Log(L"Recorder: canceled");
}

// ---------------------------------------------------------------- crash recovery (C14)
std::wstring Recorder::RecoveryDir() {
    std::wstring d = AppDataDir() + L"\\Recovery";
    EnsureDir(d);
    return d;
}

// copies the streams of 'in' into a regular MP4 without re-encoding (Source Reader → Sink Writer in passthrough); a
// file cut short by a crash is copied up to where it can be read
bool Recorder::RemuxToMp4(const std::wstring& in, const std::wstring& out, std::wstring& err) {
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) { err = L"MFStartup"; return false; }
    bool ok = false;
    {
        ComPtr<IMFSourceReader> rd;
        ComPtr<IMFSinkWriter> wr;
        HRESULT hr = MFCreateSourceReaderFromURL(in.c_str(), nullptr, &rd);
        if (SUCCEEDED(hr)) {
            ComPtr<IMFAttributes> attr;
            MFCreateAttributes(&attr, 1);
            attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
            hr = MFCreateSinkWriterFromURL(out.c_str(), nullptr, attr.Get(), &wr);
        }
        std::map<DWORD, DWORD> map;                     // reader stream → writer stream
        for (DWORD i = 0; SUCCEEDED(hr); i++) {
            ComPtr<IMFMediaType> mt;
            HRESULT h2 = rd->GetNativeMediaType(i, 0, &mt);
            if (h2 == MF_E_INVALIDSTREAMNUMBER) break;
            if (FAILED(h2)) { hr = h2; break; }
            GUID major{};
            mt->GetGUID(MF_MT_MAJOR_TYPE, &major);
            if (major != MFMediaType_Video && major != MFMediaType_Audio) { rd->SetStreamSelection(i, FALSE); continue; }
            rd->SetStreamSelection(i, TRUE);
            rd->SetCurrentMediaType(i, nullptr, mt.Get());
            DWORD ws = 0;
            if (FAILED(hr = wr->AddStream(mt.Get(), &ws))) break;
            map[i] = ws;
        }
        if (SUCCEEDED(hr) && !map.empty()) hr = wr->BeginWriting();
        LONGLONG count = 0;
        while (SUCCEEDED(hr) && !map.empty()) {
            DWORD idx = 0, flags = 0;
            LONGLONG ts = 0;
            ComPtr<IMFSample> smp;
            HRESULT h2 = rd->ReadSample((DWORD)MF_SOURCE_READER_ANY_STREAM, 0, &idx, &flags, &ts, &smp);
            if (FAILED(h2) || (flags & MF_SOURCE_READERF_ERROR)) break;     // cut short: what was read is kept
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                map.erase(idx);
                continue;
            }
            if (!smp || !map.count(idx)) continue;
            if (FAILED(wr->WriteSample(map[idx], smp.Get()))) break;
            count++;
        }
        if (SUCCEEDED(hr) && count > 0) { hr = wr->Finalize(); ok = SUCCEEDED(hr); }
        if (!ok) err = Format(L"0x%08X", hr);
    }
    MFShutdown();
    if (!ok) DeleteFileW(out.c_str());
    return ok;
}

std::vector<std::wstring> Recorder::RecoverInterrupted() {
    std::vector<std::wstring> done;
    std::wstring dir = RecoveryDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.mp4").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return done;
    std::vector<std::wstring> files;
    do { files.push_back(dir + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (auto& f : files) {
        std::wstring target;
        HANDLE t = CreateFileW((f + L".target").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (t != INVALID_HANDLE_VALUE) {
            char buf[2048]; DWORD rd = 0;
            if (ReadFile(t, buf, sizeof buf - 1, &rd, nullptr)) target = Utf8ToW(std::string(buf, rd));
            CloseHandle(t);
        }
        if (target.empty()) target = LibraryDir() + L"\\" + FileNameOf(f);
        // never over an existing video
        for (int n = 2; FileExists(target); n++) {
            std::wstring base = target.substr(0, target.size() - 4);
            size_t us = base.rfind(L"_r");
            target = (us != std::wstring::npos && us + 2 < base.size() && iswdigit(base[us + 2]) ? base.substr(0, us) : base) + Format(L"_r%d.mp4", n);
        }
        EnsureDir(DirOf(target));
        std::wstring err;
        LARGE_INTEGER size{};
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        if (GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &fa)) { size.HighPart = (LONG)fa.nFileSizeHigh; size.LowPart = fa.nFileSizeLow; }
        bool ok = size.QuadPart > 4096 && RemuxToMp4(f, target, err);
        if (ok) done.push_back(target);
        Log(L"Recorder: recovery of %s → %s: %s", f.c_str(), target.c_str(), ok ? L"ok" : (L"failed " + err).c_str());
        if (ok || size.QuadPart <= 4096) { DeleteFileW(f.c_str()); DeleteFileW((f + L".target").c_str()); }
    }
    return done;
}

} // namespace snap
