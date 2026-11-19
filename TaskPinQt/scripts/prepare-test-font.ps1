[CmdletBinding()]
param([string]$OutputDir, [string]$FontPath)
. "$PSScriptRoot/common.ps1"
if (!$OutputDir) { $OutputDir = Join-Path $PSScriptRoot '../../docs/artifacts/fonts' }
$output = Get-FullPath $OutputDir
New-Item -ItemType Directory -Force -Path $output | Out-Null
# Noto CJK Sans 2.004, SIL Open Font License. Test-only; never packaged.
$base = 'https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78'
$font = Join-Path $output 'NotoSansCJKsc-Regular.otf'
if ($FontPath) {
    Copy-Item -LiteralPath (Get-FullPath $FontPath) -Destination $font -Force
} else {
    Invoke-WebRequest -UseBasicParsing -Uri "$base/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf" -OutFile $font
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../tests/Noto-OFL.txt') -Destination (Join-Path $output 'Noto-LICENSE.txt') -Force
$blob = & git hash-object -- $font
if ($LASTEXITCODE -ne 0 -or $blob -ne 'dc15562470b4f842321894787a0d066879ccff8b') {
    throw 'Downloaded Noto CJK font does not match the pinned upstream Git blob.'
}
Write-Host "Verified Chinese test font: $font"
