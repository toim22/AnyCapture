// Cursor stamps (C25): arrow, hand, I-beam, busy, crosshair, move, resize and click indicators drawn as vectors at any
// size — placed from the stamp gallery ("Cursors") as editable, resizable Stamp objects ("cursor:<kind>"). Compiled in
// both programs because Render.cpp (also in the capture, for pins) draws the stamps.
#include "EditorTools.h"
#include "SnapcoreBridge.h"


namespace snap::ed {

// ================================================================ C25 cursor stamps
std::vector<std::pair<std::wstring, std::wstring>> CursorStamps() {
    return { { L"cursor:arrow", Tr(L"Seta do mouse") }, { L"cursor:arrowdark", Tr(L"Seta do mouse (escura)") },
             { L"cursor:hand", Tr(L"Mão (link)") }, { L"cursor:ibeam", Tr(L"Cursor de texto") }, { L"cursor:wait", Tr(L"Ocupado") },
             { L"cursor:cross", Tr(L"Mira") }, { L"cursor:move", Tr(L"Mover") }, { L"cursor:resize", Tr(L"Redimensionar") },
             { L"cursor:click", Tr(L"Indicador de clique") }, { L"cursor:clickright", Tr(L"Clique direito") } };
}

// drawn by snapcore (rust/snapcore/src/stamps.rs) on a 24-unit grid at the size
Image RenderCursorStamp(const std::wstring& id, int px) { return TakeImage(snapcore_stamp_image(id.c_str(), px, 2)); }

} // namespace snap::ed
