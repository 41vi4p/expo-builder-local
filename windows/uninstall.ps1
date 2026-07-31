#Requires -Version 5.1
<#
.SYNOPSIS
    Uninstalls ebl (expo-local-builder) from Windows.

.DESCRIPTION
    Removes the ebl install directory (%LOCALAPPDATA%\Programs\ebl) and its PATH
    entry. Leaves Docker Desktop alone (it's your system's own component, not
    ebl's).
#>

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\ebl"
$BinDir = Join-Path $InstallDir "bin"

Write-Step "Removing ebl..."
if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
    Write-Host "   Removed $InstallDir"
} else {
    Write-Host "   Already gone."
}

Write-Step "Removing it from your PATH..."
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$parts = ($userPath -split ";") | Where-Object { $_ -ne "" -and $_ -ne $BinDir }
[Environment]::SetEnvironmentVariable("Path", ($parts -join ";"), "User")

Write-Host ""
Write-Host "ebl uninstalled." -ForegroundColor Green
Write-Host "Docker Desktop was left as-is - remove it yourself if you no longer need it."
