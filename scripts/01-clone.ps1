# STDplus — stap 1: clone van GitHub + submodule ophalen
# Voer uit vanuit de map waar je STDplus wilt neerzetten.
# Voorbeeld: cd C:\Projects && .\01-clone.ps1

param(
    [string]$Dest = "C:\dev\STDplus"
)

$ErrorActionPreference = "Stop"

# Zorg dat de bovenliggende map bestaat.
$parent = Split-Path $Dest -Parent
if (-not (Test-Path $parent)) {
    New-Item -ItemType Directory -Path $parent | Out-Null
}

Write-Host "== Klonen pe5jw/STDplus naar $Dest ==" -ForegroundColor Cyan
git clone https://github.com/pe5jw/STDplus.git $Dest
Set-Location $Dest

Write-Host "== STD submodule ophalen ==" -ForegroundColor Cyan
git submodule update --init --recursive

Write-Host ""
Write-Host "Klaar. Ga nu naar '$Dest' en voer 02-configure.ps1 uit." -ForegroundColor Green
