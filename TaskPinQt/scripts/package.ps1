[CmdletBinding()]
param([string]$BuildDir,
      [string]$OutputDir,
      [string]$QtRoot = $env:QT_ROOT, [string]$QtKit = $env:QT_KIT,
      [string]$Qt6Dir = $env:Qt6_DIR, [string]$CmakePrefixPath = $env:CMAKE_PREFIX_PATH,
      [string]$Compiler = $env:CXX, [string]$CMake = $env:CMAKE, [string]$Ninja = $env:NINJA)
. "$PSScriptRoot/common.ps1"
if (!$BuildDir) { $BuildDir = Join-Path $PSScriptRoot '../build' }
if (!$OutputDir) { $OutputDir = Join-Path $PSScriptRoot '../../dist' }
$toolchain = Initialize-Toolchain $QtRoot $QtKit $Qt6Dir $CmakePrefixPath $Compiler $CMake $Ninja
$root = Get-FullPath (Join-Path $PSScriptRoot '../..')
$build = Get-FullPath $BuildDir
$output = Get-FullPath $OutputDir
$versionLine = Select-String -LiteralPath (Join-Path $root 'TaskPinQt/CMakeLists.txt') -Pattern 'project\(TaskPinQt VERSION ([0-9.]+)'
$version = $versionLine.Matches[0].Groups[1].Value
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Unexpected version: $version" }
if (!(Test-Path -LiteralPath (Join-Path $build 'TaskPinQt.exe'))) { throw 'Build the Release executable first.' }
if (!(Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_BUILD_TYPE:STRING=Release$' -Quiet)) {
    throw 'Packaging requires a CMake Release build.'
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$stage = Join-Path $output ('stage-' + [guid]::NewGuid().ToString('N'))
$appDir = Join-Path $stage "TaskPin-$version"
New-Item -ItemType Directory -Path $appDir | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'TaskPinQt.exe') -Destination $appDir
$ocrRuntime=Join-Path $build 'ocr'
if (!(Test-Path -LiteralPath (Join-Path $ocrRuntime 'python.exe')) -or !(Test-Path -LiteralPath (Join-Path $ocrRuntime 'helper.py'))) {
    throw 'Build the local PaddleOCR runtime with prepare-ocr-runtime.ps1 before packaging.'
}
$ocrCheck = & (Join-Path $ocrRuntime 'python.exe') -I -B (Join-Path $ocrRuntime 'helper.py') --self-check
$ocrStatus = $ocrCheck | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or !$ocrStatus.runtimeReady -or
    @($ocrStatus.builtinProfilesVerified).Count -ne 3 -or
    'builtin-ppocrv6-tiny' -notin $ocrStatus.builtinProfilesVerified -or
    'builtin-ppocrv4-mobile' -notin $ocrStatus.builtinProfilesVerified -or
    'builtin-ppocrv5-mobile' -notin $ocrStatus.builtinProfilesVerified) {
    throw 'PaddleOCR model/runtime self-check failed before packaging.'
}
Copy-Item -LiteralPath $ocrRuntime -Destination (Join-Path $appDir 'ocr') -Recurse
Invoke-Checked (Join-Path $toolchain.Kit 'bin/windeployqt.exe') @('--qtpaths', (Join-Path $toolchain.Kit 'bin/qtpaths.exe'),
    '--compiler-runtime', '--no-translations',
    '--no-system-d3d-compiler', '--no-opengl-sw', (Join-Path $appDir 'TaskPinQt.exe'))
# TaskPin uses SQLite only; windeployqt also discovers unrelated external SQL drivers.
foreach ($driver in @('qsqlodbc.dll', 'qsqlpsql.dll')) {
    $path = Join-Path $appDir "sqldrivers/$driver"
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $appDir
Copy-Item -LiteralPath (Join-Path $root 'docs/PORTABLE-README.txt') -Destination (Join-Path $appDir 'README.txt')
New-Item -ItemType Directory -Path (Join-Path $appDir 'ocr-models') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'docs/OCR-MODELS-README.txt') -Destination (Join-Path $appDir 'ocr-models/README.txt')
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $appDir -Recurse
$forbidden = @(Get-ChildItem -LiteralPath $appDir -Recurse -File | Where-Object {
    $_.Extension -in @('.db', '.log', '.a', '.cmake', '.obj', '.o') -or $_.Name -eq 'CMakeCache.txt'
})
if ($forbidden.Count) { throw 'Unexpected development or personal files in deployment.' }
$binaryZip = Join-Path $output "TaskPin-$version-windows-x64.zip"
Compress-Archive -LiteralPath $appDir -DestinationPath $binaryZip -Force
$sourceZip = Join-Path $output "TaskPin-$version-source.zip"
Invoke-Checked 'git' @('-C', $root, 'archive', '--format=zip', "--prefix=TaskPin-$version-source/", '-o', $sourceZip, 'HEAD')
$checksums = foreach ($archive in @($binaryZip, $sourceZip)) {
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $archive
    '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), [IO.Path]::GetFileName($archive)
}
$checksums | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding Ascii
$checksums | Write-Host
Write-Host "Packaged committed source HEAD and fresh portable staging: $appDir"
# Keep staging for inspection; CI uploads only the three named final files.
