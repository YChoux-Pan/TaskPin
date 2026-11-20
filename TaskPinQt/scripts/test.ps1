[CmdletBinding()]
param([string]$BuildDir,
      [string]$ReportDir,
      [string]$CMake = $env:CMAKE)
. "$PSScriptRoot/common.ps1"
if (!$BuildDir) { $BuildDir = Join-Path $PSScriptRoot '../build' }
if (!$ReportDir) { $ReportDir = Join-Path $PSScriptRoot '../../docs/artifacts/automated' }
$build = Get-FullPath $BuildDir
$reports = Get-FullPath $ReportDir
New-Item -ItemType Directory -Force -Path $reports | Out-Null
$cmakeExe = Find-Tool $CMake 'cmake.exe'
$ctest = Join-Path (Split-Path $cmakeExe -Parent) 'ctest.exe'
$oldData = $env:TASKPIN_DATA
$oldPlatform = $env:QT_QPA_PLATFORM
$oldAutomation = $env:TASKPIN_AUTOMATION
try {
    $env:TASKPIN_DATA = Join-Path $reports ('data-' + [guid]::NewGuid().ToString('N'))
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:TASKPIN_AUTOMATION = '1'
    New-Item -ItemType Directory -Path $env:TASKPIN_DATA | Out-Null
    try {
        Invoke-Checked $ctest @('--test-dir', $build, '--build-config', 'Release', '--output-on-failure', '--no-tests=error',
            '--verbose', '--output-log', (Join-Path $reports 'ctest.log'), '--output-junit', (Join-Path $reports 'ctest.xml'))
    } finally {
        foreach ($name in @('TaskPinScreenshotTests', 'TaskPinIntegrationTests', 'TaskPinExperienceTests')) {
            foreach ($extension in @('txt', 'xml')) {
                $path = Join-Path $build "$name.$extension"
                if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $reports -Force }
            }
        }
    }
    $selftestDir = Join-Path $env:TASKPIN_DATA 'selftest'
    $process = Start-Process -FilePath (Join-Path $build 'TaskPinQt.exe') -WorkingDirectory $build -WindowStyle Hidden `
        -ArgumentList @('--selftest', ('"' + $selftestDir + '"')) -Wait -PassThru `
        -RedirectStandardOutput (Join-Path $reports 'selftest.stdout.txt') `
        -RedirectStandardError (Join-Path $reports 'selftest.stderr.txt')
    if ($process.ExitCode -ne 0) { throw "Selftest failed with exit code $($process.ExitCode). See $reports" }
    $report = Get-ChildItem -LiteralPath $selftestDir -Filter '*.txt' | Select-Object -First 1
    if (!$report) { throw 'Selftest did not create its report.' }
    Copy-Item -LiteralPath $report.FullName -Destination (Join-Path $reports 'selftest.txt') -Force
    Get-Content -LiteralPath $report.FullName | Select-Object -Last 1 | Write-Host
    Write-Host "Automated tests passed. Reports: $reports"
} finally {
    $env:TASKPIN_DATA = $oldData
    $env:QT_QPA_PLATFORM = $oldPlatform
    $env:TASKPIN_AUTOMATION = $oldAutomation
}
