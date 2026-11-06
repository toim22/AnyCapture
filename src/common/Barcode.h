// QR code and barcode reading (C04; ShareX dossier 01 · F10: ZXing reader with tryHarder + tryRotate + tryInvert and
// a multi-result loop). Read by snapcore with rxing, a Rust port of ZXing (Apache-2.0). Available as a capture output ("Read QR code"), in the Editor
// and on pinned images; the results window copies the content and opens links.
#pragma once
#include "Image.h"

namespace snap::barcode {

struct Result {
    std::wstring text, format;       // content and symbology ("QRCode", "EAN-13"…)
    RECT box{};                      // where it is in the image
    bool url = false;                // http(s) link: can be opened
};

std::vector<Result> Read(const Image& img);
// results dialog: copy each content / open each link / copy all; a message when nothing was found
void ShowResults(HWND owner, const std::vector<Result>& results);

} // namespace snap::barcode
