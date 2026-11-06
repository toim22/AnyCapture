// Animated GIF (V2AGNative::GifCreator / GIFMaker::Writer, ds_pseudocode §11.5)
//   bitsPerPixel 4 (16 colors, the original's default) or 8; dithering; delta frames; infinite loop
//   Written by rust/snapcore/src/gif.rs; frames of another size are scaled here (GDI+).
#pragma once
#include "Image.h"

struct SnapcoreGif;

namespace snap {

class GifWriter {
public:
    bool Open(const std::wstring& path, int width, int height, double fps, int bitsPerPixel, bool dithering,
              bool deltaFrames, const std::string& comment = "Snapnab");
    bool AddFrame(const Image& frame, int delayCentiseconds = -1);
    bool Close();
    bool failed() const { return failed_; }
    ~GifWriter() { if (gif_) Close(); }
private:
    SnapcoreGif* gif_ = nullptr;     // the writer (rust/snapcore/src/gif.rs)
    int w_ = 0, h_ = 0;
    bool failed_ = false;
};

} // namespace snap
