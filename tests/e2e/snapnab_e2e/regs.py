r"""Program settings in HKCU\Software\Snapnab\1 (and the Editor subkey)."""
import winreg

ROOT = r"Software\Snapnab\1"


def _key(sub, write=False):
    path = ROOT + ("\\" + sub if sub else "")
    if write:
        return winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_SET_VALUE)
    return winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_READ)


def set_dword(name, value, sub=""):
    with _key(sub, True) as k:
        winreg.SetValueEx(k, name, 0, winreg.REG_DWORD, int(value))


def set_string(name, value, sub=""):
    with _key(sub, True) as k:
        winreg.SetValueEx(k, name, 0, winreg.REG_SZ, str(value))


def get(name, sub="", default=None):
    try:
        with _key(sub) as k:
            return winreg.QueryValueEx(k, name)[0]
    except OSError:
        return default


def delete(name, sub=""):
    try:
        with _key(sub, True) as k:
            winreg.DeleteValue(k, name)
    except OSError:
        pass
