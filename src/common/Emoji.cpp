// Emoji drawn in color: Direct2D with the Segoe UI Emoji color font, in snapcore (rust/snapcore/src/stamps.rs)
#include "Emoji.h"
#include "SnapcoreBridge.h"

namespace snap {

Image RenderEmoji(const std::wstring& text, int px) { return TakeImage(snapcore_stamp_image(text.c_str(), px, 1)); }

} // namespace snap
