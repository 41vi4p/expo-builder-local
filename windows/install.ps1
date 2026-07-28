#Requires -Version 5.1
<#
.SYNOPSIS
    Installs ebl (expo-local-builder) on Windows.

.DESCRIPTION
    ebl talks to Docker through WSL2 - Docker Desktop for Windows already runs on a
    WSL2 backend by default, so builds are Linux either way. This script:
      1. Ensures WSL2 + a Linux distro are installed.
      2. Installs the real Linux `ebl` CLI *inside* that distro, via the same
         install.sh / APT repo every Linux user gets - no separate Windows-side
         install logic to maintain.
      3. Downloads the small native ebl.exe launcher (from this repo's GitHub
         Releases) that forwards `ebl <args>` into that WSL distro, and puts it on
         your Windows PATH.

    One-line usage:
      irm https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/windows/install.ps1 | iex
#>

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn($msg) { Write-Host "!! $msg" -ForegroundColor Yellow }

$RepoRawBase = "https://raw.githubusercontent.com/41vi4p/expo-builder-local/main"
$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\ebl"
$ExePath = Join-Path $InstallDir "ebl.exe"

# --- 1. WSL2 ------------------------------------------------------------------

Write-Step "Checking for WSL2..."
$wslInstalled = $true
try {
    wsl --status *> $null
    if ($LASTEXITCODE -ne 0) { $wslInstalled = $false }
} catch {
    $wslInstalled = $false
}

if (-not $wslInstalled) {
    Write-Step "WSL2 not found - installing it (this needs Administrator and may require a reboot)."
    $currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Warn "This step needs Administrator. Re-run this script from an elevated PowerShell, then run it again."
        exit 1
    }
    wsl --install
    Write-Warn "WSL2 was just installed. Reboot if prompted, then re-run this script to finish setup."
    exit 0
}

Write-Step "Checking for an installed Linux distro..."
# Excludes Docker Desktop's own internal WSL distros (docker-desktop,
# docker-desktop-data) - they're Docker's private plumbing, not a real userland
# with apt/etc., and can otherwise get picked as the install target since wsl -l -q
# doesn't order user distros first.
$systemDistros = @("docker-desktop", "docker-desktop-data")
$distros = (wsl -l -q) -replace "`0", "" |
    Where-Object { $_.Trim() -ne "" -and $systemDistros -notcontains $_.Trim() }
if (-not $distros -or $distros.Count -eq 0) {
    Write-Step "No distro installed - installing Ubuntu (default)."
    wsl --install -d Ubuntu
    Write-Warn "Ubuntu was just installed - it may ask you to create a Unix username/password on first launch."
    Write-Warn "Finish that setup (run 'wsl' once), then re-run this script."
    exit 0
}
$distro = ($distros | Select-Object -First 1).Trim()
Write-Host "   Using distro: $distro"

# --- 2. ebl inside WSL ----------------------------------------------------------

Write-Step "Installing ebl inside '$distro' (same install path as native Linux)..."
wsl -d $distro -- bash -c "curl -fsSL $RepoRawBase/install.sh | sh"
if ($LASTEXITCODE -ne 0) {
    Write-Warn "ebl install inside WSL failed - see the output above."
    exit 1
}

# --- 3. ebl.exe launcher on the Windows side ------------------------------------

Write-Step "Installing the ebl.exe launcher..."
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$launcherUrl = "https://github.com/41vi4p/expo-builder-local/releases/latest/download/ebl.exe"
Invoke-WebRequest -Uri $launcherUrl -OutFile $ExePath -UseBasicParsing

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ";") -notcontains $InstallDir) {
    Write-Step "Adding $InstallDir to your PATH..."
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$InstallDir", "User")
    $env:Path += ";$InstallDir"
}

Write-Host ""
Write-Host "ebl installed." -ForegroundColor Green
Write-Host "  Distro:  $distro"
Write-Host "  Launcher: $ExePath"
Write-Host ""
Write-Warn "One more manual step: open Docker Desktop -> Settings -> Resources -> WSL Integration,"
Write-Warn "and enable integration for '$distro'. ebl needs Docker Desktop's daemon reachable from there."
Write-Host ""
Write-Host "Open a NEW terminal (so PATH updates take effect), then try:"
Write-Host "  ebl setup"
Write-Host "  cd path\to\your\expo\app"
Write-Host "  ebl build ."
