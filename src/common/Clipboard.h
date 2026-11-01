// Clipboard helpers beyond the image (Image.h): text, files, Base64 / data URL (owner's auto-copy, C30 Copy as…)
#pragma once
#include "Image.h"

namespace snap::clip {

bool CopyText(HWND owner, const std::wstring& text);                          // CF_UNICODETEXT
// files for pasting in Explorer, e-mail, chat (CF_HDROP + "Preferred DropEffect" = copy)
bool CopyFiles(HWND owner, const std::vector<std::wstring>& paths);
// "data:image/png;base64,…" (or WebP) and the bare Base64
std::wstring DataUrl(const Image& img, bool webp = false);
std::wstring Base64(const Image& img, bool webp = false);
// Markdown image link "![name](url)"
std::wstring MarkdownImage(const std::wstring& name, const std::wstring& url);

} // namespace snap::clip

namespace snap {
// "Copy as…" menu of a capture (C30): image, file, file path, Base64 / data URL (PNG or WebP), Markdown link when it
// was uploaded. Returns the command chosen (0 = none); 'uploadedUrl' empty hides the Markdown item.
bool CopyAsMenu(HWND owner, POINT screen, const Image& img, const std::wstring& file, const std::wstring& title,
                const std::wstring& uploadedUrl = L"");
} // namespace snap
