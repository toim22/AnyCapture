# Regenerates the screenshots of the user manual (help\images\*.webp) with the running build itself, through the
# command line automation (C44): each step opens a window, then AnyCapture captures it by its title (or by an area) and
# saves it as WebP. Run from the repository root after `build.bat`; follow the prompts (the dialogs are opened by hand,
# so the pictures show exactly what the user sees). Use a sample image without personal data, and keep other windows
# away from the captured ones.
#
#   powershell -ExecutionPolicy Bypass -File scripts\manual-screenshots.ps1 [-Dist dist\AnyCapture]
param([string]$Dist = "dist\AnyCapture")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$anycapture = Join-Path $root "$Dist\AnyCapture.exe"
$editor = Join-Path $root "$Dist\AnyCaptureEditor.exe"
$out = Join-Path $root "help\images"
New-Item -ItemType Directory -Force $out | Out-Null

# sample image: invented contact data, so OCR and redaction have something to find
Add-Type -AssemblyName System.Drawing
$sample = Join-Path $env:TEMP "anycapture-manual-sample.png"
$b = New-Object System.Drawing.Bitmap 900, 500
$g = [System.Drawing.Graphics]::FromImage($b)
$g.Clear([System.Drawing.Color]::White)
$g.TextRenderingHint = 'AntiAliasGridFit'
$f = New-Object System.Drawing.Font('Segoe UI', 20)
$y = 30
foreach ($line in 'Contact: jane.doe@example.com', 'Phone: +1 (555) 123-4567', 'Card: 4111 1111 1111 1111',
                  'Server 192.168.10.254 ok', 'Annual sales report 2026') {
    $g.DrawString($line, $f, [System.Drawing.Brushes]::Black, 30, $y); $y += 60
}
$g.FillRectangle([System.Drawing.Brushes]::SteelBlue, 600, 330, 220, 130)
$g.Dispose(); $b.Save($sample); $b.Dispose()

# second version for Compare images: one line changed and the box moved
$sampleB = Join-Path $env:TEMP "anycapture-manual-sample-b.png"
$b = New-Object System.Drawing.Bitmap 900, 500
$g = [System.Drawing.Graphics]::FromImage($b)
$g.Clear([System.Drawing.Color]::White)
$g.TextRenderingHint = 'AntiAliasGridFit'
$y = 30
foreach ($line in 'Contact: jane.doe@example.com', 'Phone: +1 (555) 123-4567', 'Card: 4111 1111 1111 1111',
                  'Server 192.168.10.254 offline', 'Annual sales report 2026') {
    $g.DrawString($line, $f, [System.Drawing.Brushes]::Black, 30, $y); $y += 60
}
$g.FillRectangle([System.Drawing.Brushes]::SteelBlue, 640, 300, 220, 130)
$g.Dispose(); $b.Save($sampleB); $b.Dispose()

if (-not (Get-Process AnyCapture -ErrorAction SilentlyContinue)) { Start-Process $anycapture -ArgumentList "/hide"; Start-Sleep 2 }
Start-Process $editor -ArgumentList "`"$sample`""
Start-Sleep 3

function Shot([string]$prompt, [string[]]$args2) {
    Read-Host "$prompt — press Enter when it is on the screen"
    & $anycapture @args2
    Start-Sleep 3
}

Shot "Open Image > Effects > Beautify" @("/window", "Beautify", "/output", "$out\beautify.webp")
Shot "Close it and open Tools > Redact sensitive data" @("/window", "Redact sensitive", "/output", "$out\redact.webp")
Shot "Close it, open File > Compare images with $sampleB and choose Difference" @("/window", "Compare images", "/output", "$out\compare.webp")
$r = Read-Host "Press Ctrl+Shift+P in the Editor to pin the image, then type its area as x,y,width,height (screen pixels)"
& $anycapture /region $r /output "$out\pin.webp"
Start-Sleep 3
Get-ChildItem $out | Format-Table Name, Length
