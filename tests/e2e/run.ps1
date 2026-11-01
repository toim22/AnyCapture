# Runs the E2E suite with the real python.exe: the "py" launcher of the Python Install Manager is an MSIX package and the
# programs it starts would write to a virtualized %LOCALAPPDATA% and HKCU (see conftest.py).
# Usage: .\run.ps1 [pytest arguments]     e.g. .\run.ps1 -m smoke    .\run.ps1 tests\test_03_editor.py -k crop
$ErrorActionPreference = "Stop"
$py = & py -c "import sys; print(sys.base_prefix + '\\python.exe')"
if (-not (Test-Path $py)) { $py = (Get-Command python.exe).Source }
Push-Location $PSScriptRoot
try {
    & $py -W ignore::FutureWarning -m pytest @args
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
