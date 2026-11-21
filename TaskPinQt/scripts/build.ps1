[CmdletBinding()]
param(
    [string]$QtRoot = $env:QT_ROOT, [string]$QtKit = $env:QT_KIT,
    [string]$Qt6Dir = $env:Qt6_DIR, [string]$CmakePrefixPath = $env:CMAKE_PREFIX_PATH,
    [string]$Compiler = $env:CXX, [string]$CMake = $env:CMAKE, [string]$Ninja = $env:NINJA,
    [string]$BuildDir, [int]$Parallel = 2,
    [string]$BootstrapPython = $env:TASKPIN_BOOTSTRAP_PYTHON,
    [switch]$SkipOcrRuntime
)
. "$PSScriptRoot/common.ps1"
if (!$BuildDir) { $BuildDir = Join-Path $PSScriptRoot '../build' }
$toolchain = Initialize-Toolchain $QtRoot $QtKit $Qt6Dir $CmakePrefixPath $Compiler $CMake $Ninja
$source = Get-FullPath (Join-Path $PSScriptRoot '..')
$build = Get-FullPath $BuildDir
$prefix = if ($CmakePrefixPath) { $CmakePrefixPath } else { $toolchain.Kit }
Invoke-Checked $toolchain.CMake @('-S', (Convert-CMakePath $source), '-B', (Convert-CMakePath $build),
    '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON',
    "-DCMAKE_MAKE_PROGRAM=$(Convert-CMakePath $toolchain.Ninja)",
    "-DCMAKE_CXX_COMPILER=$(Convert-CMakePath $toolchain.Compiler)",
    "-DQt6_DIR=$(Convert-CMakePath (Join-Path $toolchain.Kit 'lib/cmake/Qt6'))",
    "-DCMAKE_PREFIX_PATH=$(Convert-CMakePath $prefix)")
Invoke-Checked $toolchain.CMake @('--build', (Convert-CMakePath $build), '--config', 'Release', '--parallel', "$Parallel")
if (!$SkipOcrRuntime) {
    $ocrArgs=@{OutputDir=(Join-Path $build 'ocr')}
    if ($BootstrapPython) {$ocrArgs.BootstrapPython=$BootstrapPython}
    & "$PSScriptRoot/prepare-ocr-runtime.ps1" @ocrArgs
    if ($LASTEXITCODE -ne 0) {throw 'PaddleOCR runtime preparation failed.'}
}
Write-Host "Release build ready: $build/TaskPinQt.exe"
