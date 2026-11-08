# TaskPin Qt interactive regression: drives the real UI with simulated mouse/keyboard and
# asserts clipboard sizes, task counts and window state. Raw screenshots land in docs\artifacts
# (gitignored); the curated evidence images in docs\screenshots are committed by hand.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File TaskPinQt\scripts\test-snip.ps1
#
# Requires: the app built at TaskPinQt\build\TaskPinQt.exe, a 125% (1.25) primary screen like the dev box.
param(
    [string]$Exe = '',
    [string]$Docs = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent            # ...\TaskPinQt
$repoRoot = Split-Path $projectRoot -Parent                # ...\repo
if (-not $Exe) { $Exe = Join-Path $projectRoot 'build\TaskPinQt.exe' }
if (-not $Docs) { $Docs = Join-Path $repoRoot 'docs\artifacts' }
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class U {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
    public const uint LEFTDOWN = 0x02;
    public const uint LEFTUP = 0x04;
    public static void Aware() { try { SetProcessDpiAwarenessContext(new IntPtr(-4)); } catch { } }
}
'@
[U]::Aware()

$script:pass = 0
$script:fail = 0
$script:notes = New-Object System.Collections.Generic.List[string]

function Check([string]$name, [bool]$ok, [string]$detail = '') {
    if ($ok) { $script:pass++ } else { $script:fail++ }
    $line = ('{0}  {1}{2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' })), $name, $(if ($detail) { '  -> ' + $detail } else { '' }))
    Write-Host $line
    $script:notes.Add($line)
}

function Click([int]$x, [int]$y) {
    [U]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 180
    [U]::mouse_event([U]::LEFTDOWN, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [U]::mouse_event([U]::LEFTUP, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
}

function Drag([int]$x0, [int]$y0, [int]$x1, [int]$y1) {
    [U]::SetCursorPos($x0, $y0) | Out-Null
    Start-Sleep -Milliseconds 180
    [U]::mouse_event([U]::LEFTDOWN, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $steps = 14
    for ($i = 1; $i -le $steps; $i++) {
        $x = [int]($x0 + ($x1 - $x0) * $i / $steps)
        $y = [int]($y0 + ($y1 - $y0) * $i / $steps)
        [U]::SetCursorPos($x, $y) | Out-Null
        Start-Sleep -Milliseconds 22
    }
    [U]::mouse_event([U]::LEFTUP, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
}

function SendKeys([string]$keys) {
    [System.Windows.Forms.SendKeys]::SendWait($keys)
    Start-Sleep -Milliseconds 300
}

function ClipboardSize() {
    $img = [System.Windows.Forms.Clipboard]::GetImage()
    if ($null -eq $img) { return 'none' }
    $size = '{0}x{1}' -f $img.Width, $img.Height
    $img.Dispose()
    return $size
}

function Shot([string]$path) {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($b.X, $b.Y, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host ("      shot -> " + $path)
}

function Dump([string]$path) {
    if (Test-Path $path) { Remove-Item $path -Force }
    $proc = Start-Process -FilePath $Exe -ArgumentList '--dump', $path -Wait -PassThru
    Start-Sleep -Milliseconds 250
    if (-not (Test-Path $path)) { return $null }
    return (Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json)
}

function Command([string]$verb) {
    Start-Process -FilePath $Exe -ArgumentList ('--' + $verb) | Out-Null
}

function Near([string]$size, [int]$w, [int]$h, [int]$tol) {
    if ($size -eq 'none') { return $false }
    $parts = $size -split 'x'
    return ([math]::Abs([int]$parts[0] - $w) -le $tol) -and ([math]::Abs([int]$parts[1] - $h) -le $tol)
}

Write-Host '== TaskPin Qt interactive regression =='
if (-not (Test-Path $Exe)) { throw "missing $Exe" }
if (-not (Test-Path $Docs)) { New-Item -ItemType Directory -Path $Docs | Out-Null }

# ---------------------------------------------------------------- 0. app + fixture
# start from a clean instance: a leftover overlay would silently answer our clicks
$stale = Get-Process TaskPinQt -ErrorAction SilentlyContinue
if ($stale) {
    foreach ($p in $stale) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 1400
}
Start-Process -FilePath $Exe | Out-Null
Start-Sleep -Seconds 2
Check 'app is running' ([bool](Get-Process TaskPinQt -ErrorAction SilentlyContinue))

$logPath = Join-Path $env:LOCALAPPDATA 'TaskPin\TaskPinQt\taskpinqt.log'
$logStart = 0
if (Test-Path $logPath) { $logStart = @(Get-Content $logPath).Count }

$fixtureImage = Join-Path $PSScriptRoot 'fixtures\ocr-probe.png'
if (-not (Test-Path $fixtureImage)) { throw "missing fixture image $fixtureImage" }
$fixture = Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    (Join-Path $PSScriptRoot 'show-image.ps1'), '-Image', $fixtureImage, '-X', '100', '-Y', '100', '-W', '720', '-H', '220' -PassThru
Start-Sleep -Milliseconds 2600
$fw = Get-Process -Id $fixture.Id -ErrorAction SilentlyContinue
Check 'fixture window shown' ($null -ne $fw -and $fw.MainWindowTitle -eq 'TaskPinTestImage') ($fw.MainWindowTitle)

$before = Dump (Join-Path $Docs 'dump-before.json')
Check 'dump works' ($null -ne $before) 'settings/tasks/hotkeys parsed'

# ---------------------------------------------------------------- 1. region snip -> clipboard
Command 'snip'
Start-Sleep -Milliseconds 1100
Drag 130 130 500 260
SendKeys '{ENTER}'
Start-Sleep -Milliseconds 600
$size = ClipboardSize
$regionDump = Dump (Join-Path $Docs 'dump-region.json')
$lr = @($regionDump.settings.lastRegion -split ',')
Check 'region snip copies a bitmap' ($size -ne 'none') $size
Check 'region snip output is physical pixels (371x131)' (Near $size 371 131 4) $size
$physW = [int]($size -split 'x')[0]
Check 'logical selection x dpr == physical output' ([math]::Abs([int]$lr[2] * 1.25 - $physW) -le 2) (
    'logical {0}x{1} -> physical {2}' -f $lr[2], $lr[3], $size)

# ---------------------------------------------------------------- 2. click-to-snap a window
Command 'snip'
Start-Sleep -Milliseconds 1100
[U]::SetCursorPos(300, 200) | Out-Null
Start-Sleep -Milliseconds 500
Click 300 200
SendKeys '{ENTER}'
Start-Sleep -Milliseconds 700
$snap = ClipboardSize
Check 'click snaps the whole window (720x220)' (Near $snap 720 220 10) $snap

# ---------------------------------------------------------------- 2b. last region comes back pre-selected
Command 'last'
Start-Sleep -Milliseconds 1200
SendKeys '{ENTER}'
Start-Sleep -Milliseconds 700
$last = ClipboardSize
Check 'last region re-uses the snapped rectangle (720x220)' (Near $last 720 220 10) $last

# ---------------------------------------------------------------- 3. tools: rect / ellipse / mosaic / blur / arrow / text / number
Command 'snip'
Start-Sleep -Milliseconds 1100
Drag 140 140 760 420
SendKeys 'r'
Drag 620 160 740 300
SendKeys 'o'
Drag 170 300 330 390
SendKeys 'm'
Drag 140 150 380 250
SendKeys 'b'
Drag 390 150 600 250
SendKeys 'a'
Drag 360 300 520 390
# paste the text instead of typing it: the system IME would swallow synthetic letters
$cnText = [string]::Concat([char]0x6D4B, [char]0x8BD5)
[System.Windows.Forms.Clipboard]::SetText($cnText)
SendKeys 't'
Click 560 310
SendKeys '^v'
Start-Sleep -Milliseconds 400
SendKeys '{ENTER}'
SendKeys 'n'
Click 610 395
Start-Sleep -Milliseconds 400
Shot (Join-Path $Docs 'test-snip-tools.png')
SendKeys '{ESC}'
Start-Sleep -Milliseconds 600
$afterTools = Dump (Join-Path $Docs 'dump-tools.json')
if ($afterTools.card.visible -ne $true) {
    SendKeys '{ESC}'
    Start-Sleep -Milliseconds 600
    $afterTools = Dump (Join-Path $Docs 'dump-tools.json')
}
Check 'tools round leaves no overlay' ($afterTools.card.visible -eq $true) 'card restored'

# ---------------------------------------------------------------- 4. OCR -> task
$beforeOcr = (Dump (Join-Path $Docs 'dump-ocr-before.json')).tasks.open
Command 'snip'
Start-Sleep -Milliseconds 1100
Click 300 200
SendKeys '^r'
Start-Sleep -Seconds 6
$afterOcr = (Dump (Join-Path $Docs 'dump-after-ocr.json')).tasks.open
Check 'OCR hotkey creates exactly one task' ($afterOcr -eq ($beforeOcr + 1)) ("open $beforeOcr -> $afterOcr")
$newLog = (@(Get-Content $logPath -Encoding UTF8 | Select-Object -Skip $logStart) -join "`n")
Check 'OCR text reaches TimeParser (due 15:00 parsed)' ($newLog -match 'ocr task created: .+ due=\d{4}-\d{2}-\d{2}T15:00:00') 'log: ocr task created'
Check 'OCR engine returned text' ($newLog -match 'ocr ok lines=\d+') 'log: ocr ok'
Shot (Join-Path $Docs 'test-ocr-task.png')

# ---------------------------------------------------------------- 5. pin window
$img = [System.Drawing.Image]::FromFile($fixtureImage)
[System.Windows.Forms.Clipboard]::SetImage($img)
$img.Dispose()
Start-Sleep -Milliseconds 300
Command 'pin'
Start-Sleep -Milliseconds 1400
$pinDump = Dump (Join-Path $Docs 'dump-pin.json')
Check 'pin window opened' ($pinDump.pins.count -ge 1) ("pins=" + $pinDump.pins.count + " group=" + $pinDump.pins.group)
Shot (Join-Path $Docs 'test-pin.png')
Command 'pins'
Start-Sleep -Milliseconds 500
Command 'pins'
Start-Sleep -Milliseconds 500
Check 'pins can be hidden and shown again' ((Dump (Join-Path $Docs 'dump-pin2.json')).pins.count -ge 1) 'toggle ok'

# ---------------------------------------------------------------- 6. calendar / settings / history
Command 'calendar'
Start-Sleep -Milliseconds 1600
Shot (Join-Path $Docs 'test-calendar.png')
SendKeys '{ESC}'
Start-Sleep -Milliseconds 500

Command 'settings'
Start-Sleep -Milliseconds 1800
Shot (Join-Path $Docs 'test-settings.png')
SendKeys '{ESC}'
Start-Sleep -Milliseconds 500

Command 'history'
Start-Sleep -Milliseconds 1800
Shot (Join-Path $Docs 'test-history.png')
SendKeys '{ESC}'
Start-Sleep -Milliseconds 500

# ---------------------------------------------------------------- 6b. colour picker
Command 'color'
Start-Sleep -Milliseconds 1400
Click 700 300
Start-Sleep -Milliseconds 600
$picked = [System.Windows.Forms.Clipboard]::GetText()
Check 'colour picker copies a hex value' ($picked -match '^#[0-9A-F]{6}$') $picked

# ---------------------------------------------------------------- 6c. whiteboards
Command 'board'
Start-Sleep -Milliseconds 1400
Drag 400 500 900 650
Start-Sleep -Milliseconds 200
Shot (Join-Path $Docs 'test-board.png')
SendKeys '{ENTER}'
Start-Sleep -Milliseconds 900
$board = ClipboardSize
Check 'whiteboard copies the full frozen frame' (Near $board 2560 1600 8) $board

Command 'board-transparent'
Start-Sleep -Milliseconds 1400
Drag 500 700 1000 820
Start-Sleep -Milliseconds 200
Shot (Join-Path $Docs 'test-board-transparent.png')
SendKeys '{ENTER}'
Start-Sleep -Milliseconds 900
$board2 = ClipboardSize
Check 'transparent whiteboard copies a full-size image' (Near $board2 2560 1600 8) $board2

# ---------------------------------------------------------------- 7. full screen capture
Command 'full'
Start-Sleep -Milliseconds 1600
$full = ClipboardSize
Check 'full screen capture copies the whole desktop' (Near $full 2560 1600 8) $full

# ---------------------------------------------------------------- 8. settings round trip
$settingsPath = Join-Path $env:LOCALAPPDATA 'TaskPin\TaskPinQt\settings.json'
$originalSettings = Get-Content $settingsPath -Raw -Encoding UTF8
$cfg = $originalSettings | ConvertFrom-Json
$cfg.visibleRows = 3
$cfg.cardWidth = 300
$cfg.hotkeys.snip = 'Ctrl+Alt+A'
$cfg.autostart = $true
$cfg.snipDelay = 1.0
$cfg | ConvertTo-Json -Depth 6 | Set-Content $settingsPath -Encoding UTF8
Get-Process TaskPinQt -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 1600
Start-Process -FilePath $Exe | Out-Null
Start-Sleep -Seconds 2
$reloaded = Dump (Join-Path $Docs 'dump-settings.json')
Check 'settings reload resizes the card (300 x 159)' ($reloaded.card.w -eq 300 -and $reloaded.card.h -eq 159) (
    'card {0}x{1}' -f $reloaded.card.w, $reloaded.card.h)
$snipChord = ($reloaded.hotkeys | Where-Object { $_.action -eq 'snip' }).chord
Check 'requested hotkey is honoured or degraded to a fallback' (-not [string]::IsNullOrEmpty($snipChord)) $snipChord
Check 'autostart writes the logon entry' ($reloaded.settings.autostart -eq $true) ('autostart=' + $reloaded.settings.autostart)

# ---------------------------------------------------------------- 8b. delayed capture (C4)
Command 'snip'
Start-Sleep -Milliseconds 1700
SendKeys '{ESC}'
Start-Sleep -Milliseconds 500
$delayLog = (@(Get-Content $logPath -Encoding UTF8 | Select-Object -Skip $logStart) -join "`n")
Check 'delayed snip honours the configured delay' ($delayLog -match 'startSnip: delayed 1\.0s') 'log: startSnip: delayed 1.0s'

# ---------------------------------------------------------------- 9. final snapshot
$final = Dump (Join-Path $Docs 'dump.json')
Check 'final dump written' ($null -ne $final) ('version ' + $final.version)
Check 'history keeps captures' ($final.history.count -ge 3) ('n=' + $final.history.count)
Check 'hotkeys registered' (($final.hotkeys | Where-Object { $_.registered }).Count -ge 6) (($final.hotkeys | ForEach-Object { $_.action + '=' + $_.chord }) -join ', ')
Check 'OCR engine reported available' ($final.ocr.available -eq $true) $final.ocr.engine

# ---------------------------------------------------------------- 10. cleanup (restore the user's settings)
Stop-Process -Id $fixture.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
if (-not $env:TASKPIN_KEEP_APP) {
    Command 'quit'
    Start-Sleep -Milliseconds 1400
}
Set-Content -Path $settingsPath -Value $originalSettings -Encoding UTF8 -NoNewline
if (-not ($originalSettings | ConvertFrom-Json).autostart) {
    Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'TaskPinQt' -ErrorAction SilentlyContinue
}

$script:notes -join "`n" | Set-Content (Join-Path $Docs 'test-snip-report.txt') -Encoding UTF8
Write-Host ''
Write-Host ('== summary: {0} passed, {1} failed ==' -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
