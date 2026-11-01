#include "Sounds.h"
#include <mmsystem.h>
#include <cmath>

namespace snap::sounds {

const wchar_t* KindName(Kind k) {
    switch (k) {
    case Off: return Tr(L"Desligado");
    case Shutter: return Tr(L"Obturador de câmera");
    case Click: return Tr(L"Clique");
    case Chime: return Tr(L"Toque suave");
    case Custom: return Tr(L"Arquivo WAV…");
    default: return L"";
    }
}

namespace {
constexpr int kRate = 44100;

// a mono 16-bit WAV in memory, filled by the generator (t in seconds → sample -1..1)
std::vector<BYTE> MakeWav(double seconds, const std::function<double(double, int)>& gen) {
    int n = (int)(seconds * kRate);
    std::vector<BYTE> w(44 + n * 2);
    auto put32 = [&](size_t at, uint32_t v) { memcpy(&w[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { memcpy(&w[at], &v, 2); };
    memcpy(&w[0], "RIFF", 4); put32(4, (uint32_t)w.size() - 8); memcpy(&w[8], "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, 1); put32(24, kRate); put32(28, kRate * 2); put16(32, 2); put16(34, 16);
    memcpy(&w[36], "data", 4); put32(40, n * 2);
    for (int i = 0; i < n; i++) {
        double v = std::clamp(gen((double)i / kRate, i), -1.0, 1.0);
        put16(44 + i * 2, (uint16_t)(int16_t)std::lround(v * 30000));
    }
    return w;
}

uint32_t g_seed = 12345;
double Noise() { g_seed = g_seed * 1664525u + 1013904223u; return ((g_seed >> 8) & 0xFFFF) / 32768.0 - 1.0; }

const std::vector<BYTE>& Sample(Kind k) {
    static std::vector<BYTE> cache[Count];
    if (!cache[k].empty()) return cache[k];
    const double pi = 3.14159265358979;
    switch (k) {
    case Shutter: {    // the curtain opening and closing: two crisp clicks 55 ms apart, like the phone and camera ones
        // each: a metallic ring (4.2 and 2.6 kHz, a few ms), a burst of noise band-passed around 3.5 kHz and a short body
        // at 190 Hz; the first version had a 180 Hz tone under plain noise and sounded like a buzz
        double w0 = 2 * pi * 3500 / kRate, alpha = std::sin(w0) / (2 * 0.8), a0 = 1 + alpha;
        double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2 * std::cos(w0) / a0, a2 = (1 - alpha) / a0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        g_seed = 12345;
        cache[k] = MakeWav(0.17, [&](double t, int) {
            double x = Noise(), bp = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;      // band-pass biquad (RBJ)
            x2 = x1; x1 = x; y2 = y1; y1 = bp;
            double v = 0;
            const double at[2] = { 0.0, 0.055 }, amp[2] = { 1.0, 0.72 };
            for (int e = 0; e < 2; e++) {
                double d = t - at[e];
                if (d < 0) continue;
                double ring = std::sin(2 * pi * 4200 * d) * std::exp(-d * 900) * 0.55 + std::sin(2 * pi * 2600 * d) * std::exp(-d * 600) * 0.35;
                double body = std::sin(2 * pi * 190 * d) * std::exp(-d * 140) * 0.22;
                v += amp[e] * (ring + bp * std::exp(-d * 150) * 1.1 + body);
            }
            return v * 0.42;                       // the peaks of ring + noise + body reach about 2: kept below clipping
        });
        break;
    }
    case Click:        // a short dry tick
        cache[k] = MakeWav(0.05, [&](double t, int) { return std::sin(2 * pi * 2200 * t) * std::exp(-t * 160) * 0.8 + Noise() * std::exp(-t * 300) * 0.3; });
        break;
    case Chime:        // two soft notes
        cache[k] = MakeWav(0.45, [&](double t, int) {
            double a = std::sin(2 * pi * 880 * t) * std::exp(-t * 9);
            double b = t > 0.09 ? std::sin(2 * pi * 1318.5 * (t - 0.09)) * std::exp(-(t - 0.09) * 8) : 0;
            return (a + b) * 0.35;
        });
        break;
    default: break;
    }
    return cache[k];
}
}

void Play(Kind k, const std::wstring& customFile) {
    if (k == Off || k >= Count) return;
    if (k == Custom) {
        if (!customFile.empty() && FileExists(customFile))
            PlaySoundW(customFile.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        return;
    }
    const auto& w = Sample(k);           // static buffer: stays valid while it plays (SND_ASYNC)
    if (!w.empty()) PlaySoundW((LPCWSTR)w.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void PlayCaptureSound() {
    Kind k = (Kind)std::clamp<DWORD>(RegGetDword(L"CaptureSound", Off), 0, Count - 1);
    if (k != Off) Play(k, RegGetString(L"CaptureSoundFile", L""));
}

} // namespace snap::sounds
