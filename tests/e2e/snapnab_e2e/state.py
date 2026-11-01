"""Backup and restore of the user's Snapnab data around the test session.

The programs find their folders through SHGetKnownFolderPath, which ignores LOCALAPPDATA, so the suite cannot point
them at a sandbox: it copies %LOCALAPPDATA%\\Snapnab and exports HKCU\\Software\\Snapnab before the first test and puts
both back at the end. The backup stays in artifacts/backup-* until the restore succeeds.
"""
import shutil
import subprocess
import time
from pathlib import Path

from .config import ARTIFACTS, DATA_DIR, PICTURES_DIR, REG_KEY


def _reg(*args):
    return subprocess.run(["reg", *args], capture_output=True, text=True)


class UserState:
    def __init__(self):
        self.dir = ARTIFACTS / time.strftime("backup-%Y%m%d-%H%M%S")
        self.had_data = DATA_DIR.exists()
        self.had_reg = _reg("query", REG_KEY).returncode == 0
        self.pictures_before = set(PICTURES_DIR.rglob("*")) if PICTURES_DIR.exists() else set()

    def backup(self):
        self.dir.mkdir(parents=True, exist_ok=True)
        if self.had_data:
            shutil.copytree(DATA_DIR, self.dir / "Snapnab", ignore=shutil.ignore_patterns("*.log"))
        if self.had_reg:
            r = _reg("export", REG_KEY, str(self.dir / "registry.reg"), "/y")
            assert r.returncode == 0, f"reg export failed: {r.stderr}"
        (self.dir / "README.txt").write_text(
            "Backup of the Snapnab data made before the E2E tests.\n"
            "If the automatic restore fails: close Snapnab, copy the Snapnab folder to %LOCALAPPDATA%\n"
            "and import registry.reg (reg import registry.reg).\n", encoding="utf-8")

    def restore(self):
        # data folder: everything the tests wrote goes away, the logs are kept (they are the test evidence)
        if DATA_DIR.exists():
            for p in DATA_DIR.iterdir():
                if p.name == "Logs":
                    continue
                _remove(p)
        if self.had_data:
            for p in (self.dir / "Snapnab").iterdir():
                if p.name == "Logs":
                    continue
                dst = DATA_DIR / p.name
                if p.is_dir():
                    shutil.copytree(p, dst, dirs_exist_ok=True)
                else:
                    shutil.copy2(p, dst)
        _reg("delete", REG_KEY, "/f")
        if self.had_reg:
            r = _reg("import", str(self.dir / "registry.reg"))
            assert r.returncode == 0, f"reg import failed: {r.stderr}"
        # files saved by captures in Pictures\Snapnab during the run
        if PICTURES_DIR.exists():
            for p in sorted(PICTURES_DIR.rglob("*"), reverse=True):
                if p not in self.pictures_before:
                    _remove(p)
            if not self.pictures_before and not any(PICTURES_DIR.iterdir()):
                PICTURES_DIR.rmdir()
        # the backup is deleted only when the data folder matches it file by file
        if self.had_data:
            saved, now = _listing(self.dir / "Snapnab"), _listing(DATA_DIR)
            if saved != now:
                raise AssertionError(f"restore mismatch, backup kept in {self.dir}: "
                                     f"missing {sorted(saved - now)[:10]}, extra {sorted(now - saved)[:10]}")
        shutil.rmtree(self.dir, ignore_errors=True)


def _listing(root: Path):
    return {(str(p.relative_to(root)), p.stat().st_size) for p in root.rglob("*")
            if p.is_file() and "Logs" not in p.relative_to(root).parts}


def _remove(p: Path):
    for _ in range(10):
        try:
            if p.is_dir():
                shutil.rmtree(p)
            elif p.exists():
                p.unlink()
            return
        except PermissionError:
            time.sleep(0.5)   # a program that just closed may still hold the file
