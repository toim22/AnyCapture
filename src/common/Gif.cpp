#include "Gif.h"
#include "SnapcoreBridge.h"

namespace snap {

bool GifWriter::Open(const std::wstring& path, int w, int h, double fps, int bpp, bool dither, bool delta,
                     const std::string& comment) {
    gif_ = snapcore_gif_open(path.data(), path.size(), w, h, fps, bpp, dither, delta, comment.data(), comment.size());
    if (!gif_) { failed_ = true; return false; }      // logged by snapcore
    w_ = w; h_ = h;
    return true;
}

bool GifWriter::AddFrame(const Image& frameIn, int delayCs) {
    if (!gif_ || failed_) return false;
    Image scaled;
    const Image& frame = (frameIn.w == w_ && frameIn.h == h_) ? frameIn : (scaled = ScaleImage(frameIn, w_, h_));
    SnapcoreImageIn in = ImageIn(frame);
    if (!snapcore_gif_add_frame(gif_, &in, delayCs)) failed_ = true;
    return !failed_;
}

bool GifWriter::Close() {
    if (!gif_) return false;
    if (!snapcore_gif_close(gif_)) failed_ = true;
    gif_ = nullptr;
    return !failed_;
}

} // namespace snap
