// HDR screenshots with the right colors (C02). On HDR (Advanced Color) monitors a GDI copy of the screen comes out
// washed out or too bright; there the screen is read with DXGI Output Duplication in FP16 (scRGB) and tone-mapped to
// SDR with the monitor's "SDR content brightness" (SDR white level), so the result looks like what the user sees.
// ShareX 21 has no HDR path (dossier 01 · A1/A9: GDI BitBlt only) — this is Snapnab's own design.
// Setting HdrCorrection (on by default); monitors in SDR are untouched.
#pragma once
#include "Image.h"

namespace snap::hdr {

bool AnyHdrMonitor();                    // some monitor is in HDR mode now
bool IsHdrArea(RECT screen);             // the area touches an HDR monitor
bool Enabled();                          // HdrCorrection setting
// replaces the parts of 'img' (a GDI copy of 'screen') that lie on HDR monitors with tone-mapped pixels;
// returns false when nothing was replaced (no HDR monitor, duplication unavailable)
bool FixCapture(RECT screen, Image& img);

// tone mapping of one scRGB value (1.0 = 80 nits) to an 8-bit sRGB channel, for a given SDR white level in nits
uint8_t ToneMap(float scRgb, float sdrWhiteNits);
float HalfToFloat(uint16_t h);

// continuous capture for recordings: one HDR monitor area, frames scaled to the output size (bottom-up BGRA rows)
class Session {
public:
    ~Session();
    bool Begin(RECT screen, int outW, int outH);   // false: area not on (only) an HDR monitor or no duplication
    bool Grab(uint32_t* bottomUp);                  // the latest desktop frame; false on a lost duplication
    void End();
    bool active() const { return impl_ != nullptr; }
private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace snap::hdr
