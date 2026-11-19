Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Get-FullPath([string]$Path) { return [IO.Path]::GetFullPath($Path) }
function Convert-CMakePath([string]$Path) { return $Path.Replace('\', '/') }
function Find-Tool([string]$Requested, [string]$Name, [string[]]$Hints = @()) {
    if ($Requested) {
        $command = Get-Command $Requested -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
        throw "Tool not found: $Requested"
    }
    foreach ($hint in $Hints) {
        if (Test-Path -LiteralPath $hint -PathType Leaf) { return (Get-FullPath $hint) }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "Cannot find $Name. Supply its script parameter or add it to PATH."
}
function Initialize-Toolchain {
    param([string]$QtRoot, [string]$QtKit, [string]$Qt6Dir, [string]$CmakePrefixPath,
          [string]$Compiler, [string]$CMake, [string]$Ninja)
    $candidates = @($QtKit)
    if ($Qt6Dir) { $candidates += Join-Path $Qt6Dir '../../..' }
    foreach ($prefix in ($CmakePrefixPath -split ';')) {
        if ($prefix) {
            $candidates += $prefix
            $candidates += Join-Path $prefix '../..'
            $candidates += Join-Path $prefix '../../..'
        }
    }
    if ($QtRoot) {
        $candidates += $QtRoot
        $candidates += @(Get-ChildItem -LiteralPath $QtRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'mingw_64' })
    }
    $qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if ($qmake) { $candidates += Split-Path (Split-Path $qmake.Source -Parent) -Parent }
    $kit = $null
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'lib/cmake/Qt6/Qt6Config.cmake'))) {
            $kit = Get-FullPath $candidate
            break
        }
    }
    if (!$kit) { throw 'Qt kit not found. Set QT_KIT, QT_ROOT, Qt6_DIR or CMAKE_PREFIX_PATH (or pass the matching parameter).' }
    $toolsRoot = if ($QtRoot) { Join-Path $QtRoot 'Tools' } else { Join-Path $kit '../../Tools' }
    $compilerHints = @(Get-ChildItem -Path (Join-Path $toolsRoot 'mingw*/bin/g++.exe') -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | ForEach-Object { $_.FullName })
    $cxx = Find-Tool $Compiler 'g++.exe' $compilerHints
    $cmakeExe = Find-Tool $CMake 'cmake.exe' @((Join-Path $toolsRoot 'CMake_64/bin/cmake.exe'))
    $ninjaExe = Find-Tool $Ninja 'ninja.exe' @((Join-Path $toolsRoot 'Ninja/ninja.exe'))
    $env:PATH = "$(Split-Path $cxx -Parent);$kit\bin;$(Split-Path $ninjaExe -Parent);$env:PATH"
    return @{ Kit = $kit; Compiler = $cxx; CMake = $cmakeExe; Ninja = $ninjaExe }
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
