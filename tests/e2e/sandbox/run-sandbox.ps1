# Runs the E2E suite inside Windows Sandbox, a throwaway Windows desktop of its own: the tests get their own mouse,
# keyboard, clipboard and screen, so this computer stays free to use while they run.
# Usage: .\sandbox\run-sandbox.ps1 [pytest arguments]     e.g. .\sandbox\run-sandbox.ps1 -m smoke
# The program under test is ANYCAPTURE_DIR (default: the repository's dist\AnyCapture, the output of build.bat).
# Results: artifacts\sandbox-<date-time>\ (pytest.log, report.html, the screenshots of the failures).
$ErrorActionPreference = "Stop"

$sandboxExe = Join-Path $env:SystemRoot "System32\WindowsSandbox.exe"
if (-not (Test-Path $sandboxExe)) {
    Write-Error ("Windows Sandbox is not turned on. In an administrator PowerShell run`n" +
                 "  Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All`n" +
                 "and restart Windows.")
}
if (Get-Process WindowsSandboxRemoteSession, WindowsSandboxServer -ErrorAction SilentlyContinue) {
    Write-Error "Windows Sandbox is already running (only one can run at a time): close it first."
}

$e2e = Split-Path $PSScriptRoot -Parent
$repo = (Resolve-Path (Join-Path $e2e "..\..")).Path

# the real Python install, not the MSIX "py" launcher (see run.ps1); the sandbox runs it from a read-only mapping,
# with the packages of requirements.txt already installed in it
$py = & py -c "import sys; print(sys.base_prefix)"
if (-not (Test-Path (Join-Path $py "python.exe"))) { $py = Split-Path (Get-Command python.exe).Source }
$ErrorActionPreference = "Continue"   # PowerShell 5.1 turns a native program's stderr (a warning) into an error
& (Join-Path $py "python.exe") -W ignore -c "import pywinauto, pytest, pytest_html, pytest_timeout, PIL, psutil" 2>$null
$ErrorActionPreference = "Stop"
if ($LASTEXITCODE -ne 0) { Write-Error "Install the test packages first: $py\python.exe -m pip install -r $e2e\requirements.txt" }

$app = if ($env:ANYCAPTURE_DIR) { $env:ANYCAPTURE_DIR } else { Join-Path $repo "dist\AnyCapture" }
$app = (Resolve-Path $app).Path
if (-not (Test-Path (Join-Path $app "AnyCapture.exe"))) { Write-Error "AnyCapture.exe not found in $app (run build.bat or set ANYCAPTURE_DIR)" }
$tool = if ($env:ANYCAPTURE_TOOL_EXE) { $env:ANYCAPTURE_TOOL_EXE } else { Join-Path $repo "rust\target\release\AnyCaptureTool.exe" }
if (-not (Test-Path $tool)) { Write-Error "AnyCaptureTool.exe not found: $tool (cargo build --release -p snaptool)" }
$toolDir = Split-Path (Resolve-Path $tool).Path

$results = Join-Path $e2e ("artifacts\sandbox-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force $results | Out-Null

# The sandbox's Windows has no Visual C++ runtime, which pywin32 (win32ui, for pywinauto) loads from System32: this
# computer's copy goes in with it (inside.ps1 puts it in the sandbox's System32)
$vcrt = Join-Path $e2e "artifacts\sandbox-vcrt"
New-Item -ItemType Directory -Force $vcrt | Out-Null
foreach ($n in "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll",
               "concrt140.dll", "mfc140.dll", "mfc140u.dll") {
    $src = Join-Path $env:SystemRoot "System32\$n"
    if (-not (Test-Path $src)) { Write-Error "$src is missing: install the Visual C++ Redistributable (x64)" }
    Copy-Item $src $vcrt -Force
}
# one pytest argument per line, so quoting survives the trip into the sandbox
Set-Content -Path (Join-Path $results "args.txt") -Value $args -Encoding utf8

function Map($hostDir, $inside, $readOnly) {
    "    <MappedFolder><HostFolder>$([Security.SecurityElement]::Escape($hostDir))</HostFolder>" +
    "<SandboxFolder>$inside</SandboxFolder><ReadOnly>$(([string]$readOnly).ToLower())</ReadOnly></MappedFolder>"
}
$wsb = @"
<Configuration>
  <vGPU>Enable</vGPU>
  <MemoryInMB>8192</MemoryInMB>
  <ClipboardRedirection>Disable</ClipboardRedirection>
  <AudioInput>Disable</AudioInput>
  <VideoInput>Disable</VideoInput>
  <PrinterRedirection>Disable</PrinterRedirection>
  <MappedFolders>
$(Map $py 'C:\host\python' $true)
$(Map $e2e 'C:\host\e2e' $true)
$(Map $app 'C:\host\app' $true)
$(Map $toolDir 'C:\host\tool' $true)
$(Map $vcrt 'C:\host\vcrt' $true)
$(Map $results 'C:\results' $false)
  </MappedFolders>
  <LogonCommand>
    <Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\host\e2e\sandbox\inside.ps1</Command>
  </LogonCommand>
</Configuration>
"@
$wsbFile = Join-Path $results "AnyCapture-e2e.wsb"
Set-Content -Path $wsbFile -Value $wsb -Encoding utf8

Write-Host "Starting Windows Sandbox; results in $results"
Write-Host "You can keep using the computer. Leave the Sandbox window open (don't minimize it): it is the tests' screen."
Start-Process $sandboxExe -ArgumentList "`"$wsbFile`""

# follow pytest's output as it comes, until the sandbox writes done.txt (or is closed)
$log = Join-Path $results "pytest.log"
$done = Join-Path $results "done.txt"
$shown = 0
$started = Get-Date
while ($true) {
    Start-Sleep -Seconds 5
    if (Test-Path $log) {
        $lines = @(Get-Content $log -Encoding utf8 -ErrorAction SilentlyContinue)
        if ($lines.Count -gt $shown) { $lines[$shown..($lines.Count - 1)] | ForEach-Object { Write-Host $_ }; $shown = $lines.Count }
    }
    if (Test-Path $done) { break }
    $running = Get-Process WindowsSandboxRemoteSession, WindowsSandboxServer, WindowsSandboxClient -ErrorAction SilentlyContinue
    if (-not $running -and ((Get-Date) - $started).TotalSeconds -gt 120) { Write-Error "Windows Sandbox closed before the tests finished." }
}
$code = [int](Get-Content $done -Encoding utf8 | Select-Object -First 1)

# close the sandbox (everything inside it is thrown away; the results are already in $results)
Get-Process WindowsSandboxRemoteSession, WindowsSandboxClient, WindowsSandbox -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Write-Host "Exit code $code. Report: $results\artifacts\report.html"
exit $code
