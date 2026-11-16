# Runs inside Windows Sandbox at logon (started by run-sandbox.ps1): copies the suite and the program to the sandbox's
# own disk, runs pytest on its desktop and leaves the results in C:\results, which is a folder of the host.
$ErrorActionPreference = "Continue"
$results = "C:\results"
$log = Join-Path $results "pytest.log"
$code = 1
try {
    "Sandbox: preparing" | Out-File $log -Encoding utf8
    # the suite writes artifacts next to itself and the program must behave as on a normal disk, so both are copied
    # out of the read-only mappings
    robocopy C:\host\e2e C:\work\tests\e2e /E /XD artifacts __pycache__ /NFL /NDL /NJH /NJS /NP | Out-Null
    robocopy C:\host\app C:\AnyCapture /E /NFL /NDL /NJH /NJS /NP | Out-Null
    # the Visual C++ runtime pywin32 needs (the sandbox's Windows has none); only what is missing
    Get-ChildItem C:\host\vcrt -Filter *.dll | Where-Object { -not (Test-Path "C:\Windows\System32\$($_.Name)") } |
        Copy-Item -Destination C:\Windows\System32

    # A fresh Windows opens the Snipping Tool with Print Screen (Settings > Accessibility > Keyboard), which dims the
    # screen over every capture the tests take with that key: off, as on a computer where AnyCapture owns the key.
    # Explorer reads it when it starts.
    Set-ItemProperty "HKCU:\Control Panel\Keyboard" -Name PrintScreenKeyForSnippingEnabled -Value 0 -Type DWord
    # and the notification pop-ups ("Capture added to the library") stay on screen in a sandbox nobody touches,
    # over the Editor's Recent captures; no test reads them
    New-Item "HKCU:\Software\Microsoft\Windows\CurrentVersion\PushNotifications" -Force | Out-Null
    Set-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\PushNotifications" -Name ToastEnabled -Value 0 -Type DWord
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 5
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe; Start-Sleep -Seconds 5 }

    $env:ANYCAPTURE_DIR = "C:\AnyCapture"
    $env:ANYCAPTURE_TOOL_EXE = "C:\host\tool\AnyCaptureTool.exe"
    $env:PYTHONDONTWRITEBYTECODE = "1"
    $pyArgs = @(Get-Content (Join-Path $results "args.txt") -Encoding utf8 | Where-Object { $_ -ne "" })

    Set-Location C:\work\tests\e2e
    # cmd keeps pytest's output as plain text (PowerShell 5.1 would wrap each stderr line in an error record)
    $quoted = ($pyArgs | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join " "
    cmd /c "C:\host\python\python.exe -W ignore::FutureWarning -m pytest -p no:faulthandler $quoted >> `"$log`" 2>&1"
    $code = $LASTEXITCODE
} catch {
    "Sandbox: $_" | Out-File $log -Append -Encoding utf8
} finally {
    robocopy C:\work\tests\e2e\artifacts (Join-Path $results "artifacts") /E /NFL /NDL /NJH /NJS /NP | Out-Null
    # the program's own logs: the evidence when a test failed
    robocopy (Join-Path $env:LOCALAPPDATA "AnyCapture\Logs") (Join-Path $results "logs") /E /NFL /NDL /NJH /NJS /NP | Out-Null
    Set-Content -Path (Join-Path $results "done.txt") -Value $code -Encoding ascii
}
