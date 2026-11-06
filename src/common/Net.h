// Secrets of the optional upload (C21) and the AI assistant (C22) in the Windows Credential Manager. HTTPS, SHA-256/HMAC
// and JSON moved to Rust with the code that uses them (rust/snapcore/src/net.rs); these are the C++ entry points the
// settings dialogs still need.
#pragma once
#include "Common.h"

namespace snap::net {

// generic credential "Snapnab/<name>"
bool SaveSecret(const std::wstring& name, const std::wstring& value);
std::wstring LoadSecret(const std::wstring& name);
void DeleteSecret(const std::wstring& name);

} // namespace snap::net
