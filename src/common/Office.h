// Send a capture to Word, PowerPoint, Excel or OneNote (C20) through COM automation (late binding, IDispatch) — ShareX 21
// has no Office output (dossier 01 · C20 absent); the reference has Word/PowerPoint/Excel output plug-ins (comp_6).
//   Word: picture at the cursor of the active document (a new document when none is open)
//   PowerPoint: picture centered on the current slide, or on a new blank slide (OfficeNewSlide)
//   Excel: picture at the active cell of the active sheet
//   OneNote: picture at the end of the page open in OneNote (desktop OneNote 2016/365)
#pragma once
#include "Common.h"

namespace snap::office {

enum Target { Word = 0, PowerPoint, Excel, OneNote, TargetCount };
const wchar_t* TargetName(Target t);
bool Available(Target t);                    // the program is installed (its COM class is registered)
// 'imagePath' = PNG of the capture; w/h in pixels. 'err' explains a failure (program not installed, no page open…)
bool Send(Target t, const std::wstring& imagePath, int w, int h, std::wstring& err);

} // namespace snap::office
