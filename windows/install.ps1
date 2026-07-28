#Requires -Version 5.1
<#
.SYNOPSIS
    Installs ebl (expo-local-builder) on Windows.

.DESCRIPTION
    ebl.exe is a native Windows build of the same CLI every other platform uses
    (cli/) - it talks directly to Docker Desktop's named pipe
    (\\.\pipe\docker_engine), the same endpoint docker.exe itself uses. No WSL2, no
    separate Linux distro, no "enable WSL integration" step - just Docker Desktop,
    installed and running.

    This script:
      0. Checks that Docker Desktop is installed. It does NOT install Docker Desktop
         itself (a much heavier installer with its own license/reboot
         considerations) - Docker Desktop is a hard prerequisite you install
         yourself first, from https://www.docker.com/products/docker-desktop/.
      1. Downloads the ebl release archive (ebl.exe plus the bundled Android runner
         build context it needs to build the runner image locally if it isn't
         published yet) from this repo's GitHub Releases, and installs it under
         %LOCALAPPDATA%\Programs\ebl.
      2. Adds %LOCALAPPDATA%\Programs\ebl\bin to your Windows PATH.

    One-line usage:
      irm https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/windows/install.ps1 | iex

.PARAMETER SkipDockerCheck
    Skip the Docker Desktop presence check (e.g. if it's installed somewhere this
    script's detection doesn't recognize).

.PARAMETER LocalInstallDir
    Used by the Inno Setup GUI installer (ebl-setup.exe), which already bundles and
    extracts the release archive itself: points at that already-extracted bin\/share\
    tree instead of downloading one, so this script only does the Docker Desktop
    check and the PATH update.
#>

param(
    [switch]$SkipDockerCheck,
    [string]$LocalInstallDir
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn($msg) { Write-Host "!! $msg" -ForegroundColor Yellow }

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\ebl"
$BinDir = Join-Path $InstallDir "bin"
$ExePath = Join-Path $BinDir "ebl.exe"
$ReleaseZipUrl = "https://github.com/41vi4p/expo-builder-local/releases/latest/download/ebl-windows-amd64.zip"

# --- 0. Docker Desktop ---------------------------------------------------------
# ebl talks to Docker Desktop's own named pipe - checked first, before touching the
# install directory at all, so someone without Docker Desktop hits a clear message
# now instead of finishing setup only to find Docker unreachable at `ebl build` time.

if (-not $SkipDockerCheck) {
    Write-Step "Checking for Docker Desktop..."
    $dockerDesktopExe = Join-Path $env:ProgramFiles "Docker\Docker\Docker Desktop.exe"
    $dockerDesktopFound = Test-Path $dockerDesktopExe
    if (-not $dockerDesktopFound) {
        $dockerDesktopFound = @(
            "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*",
            "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*"
        ) | Get-ItemProperty -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -like "Docker Desktop*" } |
            Select-Object -First 1
    }
    if (-not $dockerDesktopFound) {
        Write-Warn "Docker Desktop doesn't look installed."
        Write-Warn "ebl needs it (installed and running) - install it first:"
        Write-Warn "  https://www.docker.com/products/docker-desktop/"
        Write-Warn "Then re-run this script. Already have it somewhere this check doesn't"
        Write-Warn "recognize? Re-run with -SkipDockerCheck."
        exit 1
    }
    Write-Host "   Found."
}

# --- 1. Install files ------------------------------------------------------------

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

if ($LocalInstallDir) {
    Write-Step "Installing ebl from $LocalInstallDir..."
    Copy-Item -Path (Join-Path $LocalInstallDir "bin") -Destination $InstallDir -Recurse -Force
    Copy-Item -Path (Join-Path $LocalInstallDir "share") -Destination $InstallDir -Recurse -Force
} else {
    Write-Step "Downloading ebl..."
    $tmpZip = Join-Path ([System.IO.Path]::GetTempPath()) "ebl-windows-amd64.zip"
    Invoke-WebRequest -Uri $ReleaseZipUrl -OutFile $tmpZip -UseBasicParsing
    $tmpExtract = Join-Path ([System.IO.Path]::GetTempPath()) "ebl-windows-amd64-extract"
    if (Test-Path $tmpExtract) { Remove-Item -Recurse -Force $tmpExtract }
    Expand-Archive -Path $tmpZip -DestinationPath $tmpExtract -Force
    Copy-Item -Path (Join-Path $tmpExtract "bin") -Destination $InstallDir -Recurse -Force
    Copy-Item -Path (Join-Path $tmpExtract "share") -Destination $InstallDir -Recurse -Force
    Remove-Item -Force $tmpZip
    Remove-Item -Recurse -Force $tmpExtract
}

# --- 2. PATH -----------------------------------------------------------------------

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ";") -notcontains $BinDir) {
    Write-Step "Adding $BinDir to your PATH..."
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$BinDir", "User")
    $env:Path += ";$BinDir"
}

Write-Host ""
Write-Host "ebl installed." -ForegroundColor Green
Write-Host "  Location: $ExePath"
Write-Host ""
Write-Host "Open a NEW terminal (so PATH updates take effect), then try:"
Write-Host "  ebl setup"
Write-Host "  cd path\to\your\expo\app"
Write-Host "  ebl build ."
