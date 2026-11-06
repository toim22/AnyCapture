// The secrets in the Windows Credential Manager: the work is done by snapcore (rust/snapcore/src/net.rs).
#include "Net.h"
#include "SnapcoreBridge.h"

namespace snap::net {

bool SaveSecret(const std::wstring& name, const std::wstring& value) { return snapcore_secret_save(name.c_str(), value.c_str()); }

std::wstring LoadSecret(const std::wstring& name) {
    SnapcoreWStr s{};
    return snapcore_secret_load(name.c_str(), &s) ? TakeWStr(s) : std::wstring();
}

void DeleteSecret(const std::wstring& name) { snapcore_secret_delete(name.c_str()); }

} // namespace snap::net
