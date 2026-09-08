#Requires -Version 5.1
<#
.SYNOPSIS
    Uninstalls ebl (expo-local-builder) from Windows.

.DESCRIPTION
    Always removes the ebl install directory (%LOCALAPPDATA%\Programs\ebl) and its
    PATH entry — same as before native mode existed. Additionally, if this was a
    Native-mode install (see ../CLAUDE.md's native-engine section), interactively
    offers to also remove the Android SDK/JDK/Node toolchain ebl downloaded for it
    (%LOCALAPPDATA%\ebl\toolchain\...) — reading %APPDATA%\ebl\config.json first to
    find out which of those components ebl actually installed itself (vs. detected
    and reused an existing one on this machine, which is never touched). Separately
    offers to remove saved ebl settings (Expo tokens, config) at %APPDATA%\ebl.

    Never touches Docker Desktop, WSL2, or %UserProfile%\.wslconfig's memory/swap
    tuning — those are either the system's own components, or shared machine-wide
    state other software may depend on by now.

.PARAMETER Quiet
    Skip every interactive prompt — removes only the install dir and PATH entry
    (the always-safe, always-happened part), same as this script's behavior before
    native mode existed. Used by the Inno Setup GUI uninstaller's [UninstallRun]
    step, which runs hidden with no console to prompt on; run this script directly
    from a terminal instead if you want the full interactive cleanup options.
#>

param(
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\ebl"
$BinDir = Join-Path $InstallDir "bin"
$ConfigDir = Join-Path $env:APPDATA "ebl"
$ConfigPath = Join-Path $ConfigDir "config.json"

# --- Read config first (before anything is deleted) --------------------------------
# Plain JSON via PowerShell's own ConvertFrom-Json — no dependency on the CLI's own
# C++ JSON parser, and no separate installer-side manifest file needed: config.json
# is already the single source of truth ebl setup --runtime native itself writes.
$config = $null
if (Test-Path $ConfigPath) {
    try {
        $config = Get-Content -Raw $ConfigPath | ConvertFrom-Json
    } catch {
        Write-Host "!! Could not read $ConfigPath ($($_.Exception.Message)) - skipping toolchain cleanup options." -ForegroundColor Yellow
    }
}

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

# --- Native toolchain cleanup (interactive, skipped entirely with -Quiet) ----------

if (-not $Quiet -and $config -and $config.buildMode -eq "native" -and $config.nativeToolchain) {
    $nt = $config.nativeToolchain
    $installedComponents = @(
        @{ Name = "JDK"; Path = $nt.jdkHome; Installed = [bool]$nt.jdkInstalledByEbl }
        @{ Name = "Android SDK"; Path = $nt.androidSdkRoot; Installed = [bool]$nt.androidSdkInstalledByEbl }
        @{ Name = "Node"; Path = $nt.nodeHome; Installed = [bool]$nt.nodeInstalledByEbl }
    ) | Where-Object { $_.Installed -and $_.Path }

    if ($installedComponents.Count -gt 0) {
        Write-Host ""
        Write-Host "ebl's native build engine installed its own copy of:"
        foreach ($c in $installedComponents) { Write-Host "  - $($c.Name): $($c.Path)" }
        $answer = Read-Host "Remove these too? (anything ebl detected and reused instead is never touched) [y/N]"
        if ($answer -match '^[Yy]') {
            foreach ($c in $installedComponents) {
                if (Test-Path $c.Path) {
                    Remove-Item -Recurse -Force $c.Path
                    Write-Host "   Removed $($c.Path)"
                }
            }
        }
    }
}

# --- Saved config/tokens cleanup (interactive, skipped entirely with -Quiet) -------

if (-not $Quiet -and (Test-Path $ConfigDir)) {
    Write-Host ""
    $answer = Read-Host "Also remove your saved ebl settings (Expo tokens, config) at $ConfigDir? [y/N]"
    if ($answer -match '^[Yy]') {
        Remove-Item -Recurse -Force $ConfigDir
        Write-Host "   Removed $ConfigDir"
    }
}

Write-Host ""
Write-Host "ebl uninstalled." -ForegroundColor Green
if (-not $config -or $config.buildMode -ne "native") {
    Write-Host "Docker Desktop was left as-is - remove it yourself if you no longer need it."
    Write-Host "(Next time, 'ebl clean --all' before uninstalling reclaims pulled images/volumes first.)"
}
