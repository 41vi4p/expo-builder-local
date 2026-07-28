#Requires -Version 5.1
<#
.SYNOPSIS
    Uninstalls ebl (expo-local-builder) from Windows.

.DESCRIPTION
    Removes the ebl.exe launcher and its PATH entry. Leaves WSL2 and Docker Desktop
    alone (they're your system's own components, not ebl's). Optionally also removes
    the real `ebl` package from inside WSL - asks first, since that distro may be
    used for other things.

.PARAMETER RemoveFromWsl
    Skip the interactive prompt and also run `apt remove ebl` inside WSL.

.PARAMETER KeepWslPackage
    Skip the interactive prompt and leave the `ebl` package inside WSL untouched.
#>

param(
    [switch]$RemoveFromWsl,
    [switch]$KeepWslPackage
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\ebl"

Write-Step "Removing the ebl.exe launcher..."
if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
    Write-Host "   Removed $InstallDir"
} else {
    Write-Host "   Already gone."
}

Write-Step "Removing it from your PATH..."
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$parts = ($userPath -split ";") | Where-Object { $_ -ne "" -and $_ -ne $InstallDir }
[Environment]::SetEnvironmentVariable("Path", ($parts -join ";"), "User")

if (-not $KeepWslPackage) {
    $doRemove = $RemoveFromWsl
    if (-not $RemoveFromWsl) {
        $answer = Read-Host "Also remove the ebl package from inside WSL? [y/N]"
        $doRemove = $answer -match "^[Yy]"
    }
    if ($doRemove) {
        $systemDistros = @("docker-desktop", "docker-desktop-data")
        $distros = (wsl -l -q) -replace "`0", "" |
            Where-Object { $_.Trim() -ne "" -and $systemDistros -notcontains $_.Trim() }
        $distro = ($distros | Select-Object -First 1).Trim()
        if ($distro) {
            Write-Step "Removing ebl from '$distro'..."
            wsl -d $distro -- sudo apt remove -y ebl
        }
    }
}

Write-Host ""
Write-Host "ebl uninstalled." -ForegroundColor Green
Write-Host "WSL2 and Docker Desktop were left as-is - remove those yourself if you no longer need them."
