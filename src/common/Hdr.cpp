// HDR capture: the work is done by snapcore (rust/snapcore/src/hdr.rs); these are the C++ entry points.
#include "Hdr.h"
#include "SnapcoreBridge.h"

namespace snap::hdr {

namespace {
SnapcoreRect R(RECT r) { return { r.left, r.top, r.right, r.bottom }; }
}

float HalfToFloat(uint16_t h) { return snapcore_hdr_half_to_float(h); }
uint8_t ToneMap(float v, float sdrWhiteNits) { return snapcore_hdr_tone_map(v, sdrWhiteNits); }
bool Enabled() { return snapcore_hdr_enabled(); }
bool AnyHdrMonitor() { return snapcore_hdr_any_monitor(); }
bool IsHdrArea(RECT screen) { return snapcore_hdr_is_area(R(screen)); }

bool FixCapture(RECT screen, Image& img) {
    return !img.empty() && snapcore_hdr_fix_capture(R(screen), img.px.data(), img.w, img.h);
}

// ---------------------------------------------------------------- recordings
struct Session::Impl {
    SnapcoreHdrSession* s = nullptr;
    size_t n = 0;
};

Session::~Session() { End(); }

bool Session::Begin(RECT screen, int outW, int outH) {
    End();
    SnapcoreHdrSession* s = snapcore_hdr_begin(R(screen), outW, outH);
    if (!s) return false;
    impl_ = new Impl{ s, (size_t)std::max(0, outW) * std::max(0, outH) };
    return true;
}

bool Session::Grab(uint32_t* bottomUp) { return impl_ && snapcore_hdr_grab(impl_->s, bottomUp, impl_->n); }

void Session::End() {
    if (!impl_) return;
    snapcore_hdr_end(impl_->s);
    delete impl_;
    impl_ = nullptr;
}

} // namespace snap::hdr
