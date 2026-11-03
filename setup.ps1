# STDplus — eenmalige setup op een nieuwe pc
# Voer uit vanuit de map waar dit script staat (C:\dev\STDplus)
# Vereisten: Git, CMake 3.21+, Visual Studio 2022, Qt 6.x MSVC64
#
# Gebruik:
#   cd C:\dev\STDplus
#   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
#   .\setup.ps1
#
# Optioneel andere Qt root:
#   .\setup.ps1 -QtRoot "D:\Qt"

param(
    [string]$QtRoot = "C:\Qt"
)

$ErrorActionPreference = "Stop"
$ProjectDir = $PSScriptRoot

Set-Location $ProjectDir
Write-Host "== STDplus setup in $ProjectDir ==" -ForegroundColor Cyan

# --- 1. STD broncode ---
Write-Host ""
Write-Host "[1/4] STD broncode ophalen..." -ForegroundColor Yellow
$stdDir = Join-Path $ProjectDir "std"
if (Test-Path (Join-Path $stdDir "CMakeLists.txt")) {
    Write-Host "   std/ al aanwezig, overslaan."
} elseif (Test-Path (Join-Path $ProjectDir ".git")) {
    git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "git submodule mislukt" }
} else {
    git clone https://github.com/pe5jw/STD.git $stdDir
    if ($LASTEXITCODE -ne 0) { throw "git clone van STD mislukt" }
}

# --- 2. MSHV broncode (FT8/FT4/JT65/Q65 decoder) ---
# MSHV staat BINNEN STDplus: C:\dev\STDplus\MSHV
# (build_ft8dec.cmd: %~dp0\..\..\..\ vanuit std\src\ft8\ = STDplus\)
Write-Host ""
Write-Host "[2/4] MSHV broncode ophalen (FT8 decoder)..." -ForegroundColor Yellow
$mshvDir = Join-Path $ProjectDir "MSHV"

if (Test-Path (Join-Path $mshvDir "src\HvDecoderMs\decoderft8.cpp")) {
    Write-Host "   MSHV al aanwezig in $mshvDir, overslaan."
} else {
    Write-Host "   Klonen naar $mshvDir ..."
    git clone https://github.com/LZ2HV/MSHV.git $mshvDir
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $mshvDir "src\HvDecoderMs\decoderft8.cpp"))) {
        Write-Warning "MSHV clone mislukt — FT8 decoder wordt overgeslagen."
        Write-Warning "Handmatig: pak MSHV uit naar $mshvDir"
    }
}

# --- 3. CMake configure ---
Write-Host ""
Write-Host "[3/4] CMake configure..." -ForegroundColor Yellow
$qtBase = Get-ChildItem $QtRoot -Directory -ErrorAction SilentlyContinue `
    | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } `
    | Sort-Object Name -Descending `
    | Select-Object -First 1
if (-not $qtBase) { throw "Geen Qt gevonden onder $QtRoot" }
$qtDir = Join-Path $qtBase.FullName "msvc2022_64"
if (-not (Test-Path "$qtDir\lib\cmake\Qt6\Qt6Config.cmake")) {
    throw "Qt6 MSVC64 niet gevonden in $qtDir"
}
Write-Host "   Qt: $qtDir"
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="$qtDir"
if ($LASTEXITCODE -ne 0) { throw "cmake configure mislukt" }

# --- 4. Build ---
Write-Host ""
Write-Host "[4/4] Build (Release)..." -ForegroundColor Yellow
$cores = (Get-CimInstance Win32_Processor).NumberOfLogicalProcessors
cmake --build build --config Release --parallel $cores
if ($LASTEXITCODE -ne 0) { throw "build mislukt" }

$exe = Join-Path $ProjectDir "build\Release\stdplus.exe"
Write-Host ""
if (Test-Path $exe) {
    Write-Host "== Klaar! ==" -ForegroundColor Green
    Write-Host "   $exe"
} else {
    Write-Warning "stdplus.exe niet gevonden op: $exe"
}
