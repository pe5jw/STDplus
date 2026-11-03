# STDplus — stap 3: bouwen
# Voer uit vanuit de STDplus map.

param(
    [string]$ProjectDir = "C:\dev\STDplus",
    [string]$BuildDir   = "build",
    [ValidateSet("Debug","Release","RelWithDebInfo")]
    [string]$Config   = "Release",
    [int]   $Jobs     = 0          # 0 = alle cores
)

$ErrorActionPreference = "Stop"
Set-Location $ProjectDir

if ($Jobs -eq 0) {
    $Jobs = (Get-CimInstance Win32_Processor).NumberOfLogicalProcessors
}

Write-Host "== Build STDplus ($Config, $Jobs cores) ==" -ForegroundColor Cyan
cmake --build $BuildDir --config $Config --parallel $Jobs

$exe = Join-Path $BuildDir "$Config\stdplus.exe"
if (Test-Path $exe) {
    Write-Host ""
    Write-Host "Gebouwd: $exe" -ForegroundColor Green
} else {
    Write-Warning "stdplus.exe niet gevonden op verwachte locatie: $exe"
}
