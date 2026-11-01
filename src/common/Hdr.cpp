#include "Hdr.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace snap::hdr {

float HalfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 31, man = h & 1023;
    float v;
    if (exp == 0) v = std::ldexp((float)man, -24);                       // subnormal
    else if (exp == 31) v = man ? 0.0f : 65504.0f;                        // NaN → 0, infinity → max
    else v = std::ldexp((float)(man | 1024), (int)exp - 25);
    return sign ? -v : v;
}

uint8_t ToneMap(float v, float sdrWhiteNits) {
    if (!(v > 0)) return 0;
    float x = v * 80.0f / std::max(sdrWhiteNits, 1.0f);                  // 1.0 = SDR white
    const float knee = 0.85f;                                             // highlights above the knee are compressed
    if (x > knee) x = knee + (1 - knee) * (1 - std::exp(-(x - knee) / (1 - knee)));
    float s = x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1 / 2.4f) - 0.055f;
    return (uint8_t)std::clamp((int)std::lround(s * 255), 0, 255);
}

bool Enabled() { return RegGetDword(L"HdrCorrection", 1) != 0; }

namespace {
struct Output {
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput6> out;
    DXGI_OUTPUT_DESC1 desc{};
    bool hdr() const { return desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020; }
};

std::vector<Output> Outputs() {
    std::vector<Output> v;
    ComPtr<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return v;
    ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
        ComPtr<IDXGIOutput> o;
        for (UINT j = 0; a->EnumOutputs(j, &o) != DXGI_ERROR_NOT_FOUND; j++) {
            Output x; x.adapter = a;
            if (SUCCEEDED(o.As(&x.out)) && SUCCEEDED(x.out->GetDesc1(&x.desc)) && x.desc.AttachedToDesktop) v.push_back(x);
            o.Reset();
        }
        a.Reset();
    }
    return v;
}

// "SDR content brightness" of the monitor (Settings › Display › HDR), in nits; 80 if unknown
float SdrWhiteNits(const wchar_t* gdiName) {
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return 80;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return 80;
    for (UINT32 i = 0; i < np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof src;
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || wcscmp(src.viewGdiDeviceName, gdiName) != 0) continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL wl{};
        wl.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        wl.header.size = sizeof wl;
        wl.header.adapterId = paths[i].targetInfo.adapterId;
        wl.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&wl.header) == ERROR_SUCCESS && wl.SDRWhiteLevel) return wl.SDRWhiteLevel / 1000.0f * 80.0f;
    }
    return 80;
}

// 65 536 entries: half-float bit pattern → 8-bit sRGB (fast per-pixel conversion, also for video)
std::vector<uint8_t> MakeLut(float white) {
    std::vector<uint8_t> lut(65536);
    for (uint32_t h = 0; h < 65536; h++) lut[h] = ToneMap(HalfToFloat((uint16_t)h), white);
    return lut;
}

// one duplicated output: the device, the duplication and a staging texture for reading
struct Dup {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGIOutputDuplication> dup;
    ComPtr<ID3D11Texture2D> staging;
    RECT bounds{};
    std::vector<uint8_t> lut;
    bool Open(const Output& o) {
        if (o.desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && o.desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) return false;
        D3D_FEATURE_LEVEL fl;
        if (FAILED(D3D11CreateDevice(o.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, &dev, &fl, &ctx))) return false;
        ComPtr<IDXGIOutput5> o5;
        if (FAILED(o.out.As(&o5))) return false;
        DXGI_FORMAT fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (FAILED(o5->DuplicateOutput1(dev.Get(), 0, 1, &fmt, &dup))) return false;
        bounds = o.desc.DesktopCoordinates;
        lut = MakeLut(SdrWhiteNits(o.desc.DeviceName));
        return true;
    }
    // copies the current desktop image to the staging texture; true when there is an image to read
    bool Acquire(int waitMs) {
        DXGI_OUTDUPL_FRAME_INFO fi{};
        ComPtr<IDXGIResource> res;
        HRESULT hr = dup->AcquireNextFrame(waitMs, &fi, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return staging != nullptr;    // nothing new: the previous copy is still valid
        if (FAILED(hr)) return false;
        ComPtr<ID3D11Texture2D> tex;
        bool ok = SUCCEEDED(res.As(&tex));
        if (ok && !staging) {
            D3D11_TEXTURE2D_DESC td; tex->GetDesc(&td);
            td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
            td.MipLevels = 1; td.ArraySize = 1;
            ok = SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &staging));
        }
        if (ok) ctx->CopyResource(staging.Get(), tex.Get());
        dup->ReleaseFrame();
        return ok;
    }
};
}

bool AnyHdrMonitor() {
    for (auto& o : Outputs()) if (o.hdr()) return true;
    return false;
}

bool IsHdrArea(RECT screen) {
    for (auto& o : Outputs()) {
        RECT x;
        if (o.hdr() && IntersectRect(&x, &o.desc.DesktopCoordinates, &screen)) return true;
    }
    return false;
}

bool FixCapture(RECT screen, Image& img) {
    if (img.empty()) return false;
    bool any = false;
    for (auto& o : Outputs()) {
        RECT part;
        if (!o.hdr() || !IntersectRect(&part, &o.desc.DesktopCoordinates, &screen)) continue;
        Dup d;
        if (!d.Open(o)) { Log(L"HDR: duplication unavailable on %s", o.desc.DeviceName); continue; }
        bool got = false;
        for (int i = 0; i < 6 && !got; i++) got = d.Acquire(150) && d.staging;   // the first frame can take a moment
        if (!got) { Log(L"HDR: no frame from %s", o.desc.DeviceName); continue; }
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(d.ctx->Map(d.staging.Get(), 0, D3D11_MAP_READ, 0, &m))) continue;
        for (int y = part.top; y < part.bottom; y++) {
            const uint16_t* src = (const uint16_t*)((const BYTE*)m.pData + (size_t)(y - d.bounds.top) * m.RowPitch);
            uint32_t* dst = img.row(y - screen.top);
            for (int x = part.left; x < part.right; x++) {
                const uint16_t* p = src + (size_t)(x - d.bounds.left) * 4;
                dst[x - screen.left] = 0xFF000000u | ((uint32_t)d.lut[p[0]] << 16) | ((uint32_t)d.lut[p[1]] << 8) | d.lut[p[2]];
            }
        }
        d.ctx->Unmap(d.staging.Get(), 0);
        any = true;
        Log(L"HDR: %s tone-mapped (SDR white %.0f nits)", o.desc.DeviceName, SdrWhiteNits(o.desc.DeviceName));
    }
    return any;
}

// ---------------------------------------------------------------- recordings
struct Session::Impl {
    Dup d;
    RECT area{};
    int outW = 0, outH = 0;
    std::vector<int> xs, ys;          // source column / row of each output pixel
};

Session::~Session() { End(); }

bool Session::Begin(RECT screen, int outW, int outH) {
    End();
    for (auto& o : Outputs()) {
        RECT x;
        if (!o.hdr() || !IntersectRect(&x, &o.desc.DesktopCoordinates, &screen) || !EqualRect(&x, &screen)) continue;
        auto* im = new Impl();
        if (!im->d.Open(o)) { delete im; return false; }
        im->area = screen; im->outW = outW; im->outH = outH;
        int sw = screen.right - screen.left, sh = screen.bottom - screen.top;
        for (int i = 0; i < outW; i++) im->xs.push_back(screen.left - im->d.bounds.left + std::min(sw - 1, (int)((i + 0.5) * sw / outW)));
        for (int i = 0; i < outH; i++) im->ys.push_back(screen.top - im->d.bounds.top + std::min(sh - 1, (int)((i + 0.5) * sh / outH)));
        impl_ = im;
        Log(L"HDR: recording from %s with tone mapping", o.desc.DeviceName);
        return true;
    }
    return false;
}

bool Session::Grab(uint32_t* bottomUp) {
    if (!impl_) return false;
    auto& d = impl_->d;
    if (!d.Acquire(0) || !d.staging) return false;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(d.ctx->Map(d.staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    for (int y = 0; y < impl_->outH; y++) {
        const uint16_t* src = (const uint16_t*)((const BYTE*)m.pData + (size_t)impl_->ys[y] * m.RowPitch);
        uint32_t* dst = bottomUp + (size_t)(impl_->outH - 1 - y) * impl_->outW;
        for (int x = 0; x < impl_->outW; x++) {
            const uint16_t* p = src + (size_t)impl_->xs[x] * 4;
            dst[x] = 0xFF000000u | ((uint32_t)d.lut[p[0]] << 16) | ((uint32_t)d.lut[p[1]] << 8) | d.lut[p[2]];
        }
    }
    d.ctx->Unmap(d.staging.Get(), 0);
    return true;
}

void Session::End() { delete impl_; impl_ = nullptr; }

} // namespace snap::hdr
