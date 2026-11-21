[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$BootstrapPython = 'python',
    [string]$CacheDir,
    [switch]$Offline
)

$ErrorActionPreference = 'Stop'
if (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    throw 'OutputDir must be an absolute path.'
}
$builder = Join-Path $PSScriptRoot '../ocr/build_runtime.py'
$builderArgs = @('-I', '-B', $builder, '--output-dir', $OutputDir)
if ($CacheDir) { $builderArgs += @('--cache-dir', $CacheDir) }
if ($Offline) { $builderArgs += '--offline' }
& $BootstrapPython @builderArgs
if ($LASTEXITCODE -ne 0) { throw "OCR runtime preparation failed (exit $LASTEXITCODE)." }
