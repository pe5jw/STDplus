# STDplus — stap 2: CMake configure
# Voer uit vanuit de STDplus map (waar CMakeLists.txt staat).
# Vereisten: Visual Studio 2022, Qt 6.x (MSVC 64-bit), CMake 3.21+

param(
    [string]$ProjectDir = "C:\dev\STDplus",
    [string]$QtRoot     = "C:\Qt",     # bovenste Qt map
    [string]$BuildDir   = "build"
)

$ErrorActionPreference = "Stop"

# Zoek de nieuwste Qt6 MSVC64 installatie onder $QtRoot
$qtBase = Get-ChildItem "$QtRoot" -Directory `
    | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } `
    | Sort-Object Name -Descending `
    | Select-Object -First 1

if (-not $qtBase) {
    Write-Error "Geen Qt versie gevonden onder $QtRoot"
}

$qtDir = Join-Path $qtBase.FullName "msvc2022_64"
if (-not (Test-Path "$qtDir\lib\cmake\Qt6\Qt6Config.cmake")) {
    Write-Error "Qt6 MSVC64 niet gevonden in $qtDir"
}

Set-Location $ProjectDir

Write-Host "== Qt gevonden: $qtDir ==" -ForegroundColor Cyan
Write-Host "== CMake configure in $ProjectDir ==" -ForegroundColor Cyan

cmake -B $BuildDir `
      -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="$qtDir"

Write-Host ""
Write-Host "Configure klaar. Voer nu 03-build.ps1 uit." -ForegroundColor Green
