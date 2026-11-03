# STDplus — STD submodule bijwerken naar laatste upstream versie
# Voer uit vanuit de STDplus map.

param(
    [string]$ProjectDir = "C:\dev\STDplus"
)

$ErrorActionPreference = "Stop"
Set-Location $ProjectDir

Write-Host "== std submodule updaten (pe5jw/STD main) ==" -ForegroundColor Cyan
git submodule update --remote std

$hash = git -C std rev-parse --short HEAD
Write-Host "std staat nu op: $hash" -ForegroundColor Yellow

Write-Host "== commit submodule pointer ==" -ForegroundColor Cyan
git add std
git commit -m "Update STD submodule to $hash"

Write-Host ""
Write-Host "Klaar. Voer 03-build.ps1 opnieuw uit om te herbouwen." -ForegroundColor Green
