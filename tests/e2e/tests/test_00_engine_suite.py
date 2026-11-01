"""The repository's own integration tests (SnapnabTests.exe, tests/tests.cpp): phase correlation, wand, .snap round
trip, rendering, PDF, GIF, effects, IPC, MP4 recording and cutting, OCR, panoramic stitching and the translations."""
import subprocess

import pytest

from snapnab_e2e.config import ARTIFACTS, TESTS_EXE


@pytest.mark.slow
@pytest.mark.timeout(900)
def test_snapnab_tests_exe():
    assert TESTS_EXE.exists(), f"{TESTS_EXE} not found: run build.bat test"
    r = subprocess.run([str(TESTS_EXE)], capture_output=True, timeout=880, cwd=TESTS_EXE.parent)
    out = r.stdout.decode("utf-8", "replace") + r.stderr.decode("utf-8", "replace")
    (ARTIFACTS / "SnapnabTests.log").write_text(out, encoding="utf-8")
    failed = [l for l in out.splitlines() if l.strip().lower().startswith("fail")]
    assert r.returncode == 0 and not failed, f"exit {r.returncode}; failures:\n" + "\n".join(failed[:30])
