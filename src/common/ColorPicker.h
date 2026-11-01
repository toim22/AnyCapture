// Screen color picker (C10; ShareX dossier 02 · E.6 ScreenColorPickerWindow, 01 · H01 ScreenColorPicker): a full-screen
// overlay over the frozen screen with a magnifier and the color under the cursor as HEX, RGB and HSL. A click picks
// the pixel, copies it in the default format and opens a menu with the other formats and the recent colors.
#pragma once
#include "Common.h"

namespace snap {

// 'done' receives the picked color (or false when canceled), on the UI thread
void RunScreenColorPicker(std::function<void(bool ok, COLORREF c)> done);

// menu of the color formats (and recent colors) at the screen point; copies the chosen text. Returns false if closed
bool ColorFormatsMenu(HWND owner, COLORREF c, POINT screen);

} // namespace snap
