// Dragging files out to other programs (Explorer, e-mail, chat, Office): an IDataObject with CF_HDROP and the shell
// ID list, a drag image and "Preferred DropEffect" = copy. Used by the Editor's tray and library, the quick-access
// overlay (C08) and pinned images (C01) — the overlay's CF_HDROP drag of ShareX dossier 01 · F08.
#pragma once
#include "Image.h"

namespace snap {

// drags 'files' from 'source' (modal until the drop); 'thumb' (optional) follows the cursor. Move is only allowed
// when the files are temporary copies. Returns the drop effect (0 = canceled).
DWORD DragFilesOut(HWND source, const std::vector<std::wstring>& files, const Image* thumb, bool allowMove);

// a temporary copy of an image for dragging or copying as a file: %TEMP%\Snapnab\<name>.png (C29: metadata stripped
// when StripMetadata or 'strip' says so)
std::wstring TempImageFile(const Image& img, const std::wstring& name, bool strip = false);

} // namespace snap
