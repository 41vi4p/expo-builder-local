#Requires -Version 5.1
<#
.SYNOPSIS
    Installs ebl (expo-local-builder) on Windows.

.DESCRIPTION
    ebl.exe is a native Windows build of the same CLI every other platform uses
    (cli/). Two build engines are available on Windows, chosen via -Mode (default
    Native — see ../CLAUDE.md's native-engine section, UNVERIFIED ON REAL WINDOWS
    HARDWARE):

      Native (default): installs the Android SDK, JDK 17, and Node.js directly on
        this machine, isolated under %LOCALAPPDATA%\ebl — no Docker Desktop or
        WSL2 required. Detects and reuses an existing JDK/SDK/Node install first
        rather than downloading its own copy where possible.
      Docker: talks directly to Docker Desktop's named pipe (\\.\pipe\docker_engine,
        the same endpoint docker.exe itself uses) and runs builds in a disposable
        Linux container — the same engine Linux/macOS use. Requires Docker Desktop
        (and usually WSL2) installed separately.

    This script:
      0. (Docker mode only) Checks that Docker Desktop is installed. It does NOT
         install Docker Desktop itself (a much heavier installer with its own
         license/reboot considerations) - Docker Desktop is a hard prerequisite you
         install yourself first, from https://www.docker.com/products/docker-desktop/.
      1. (Docker mode only) If Docker Desktop's WSL2 backend is present, tunes its
         memory/swap limits in %UserProfile%\.wslconfig from the host's actual
         installed RAM (see PARAMETER SkipWslConfig below) - ebl.exe itself talks to
         Docker Desktop directly over its named pipe and never touches WSL2, but the
         *build containers* run inside that WSL2 VM, and WSL2's own default cap (50%
         of host RAM, swap wherever the system drive happens to have room) is
         routinely too little for a real Android build: a cold, multi-ABI native
         compile (react-native-worklets/react-native-screens across
         arm64-v8a/armeabi-v7a/x86/x86_64) plus Gradle/Kotlin daemons can exhaust
         it, which crashes Docker Desktop's backend (the Engine API starts
         returning 500s) rather than just slowing the build down.
      2. Downloads the ebl release archive (ebl.exe plus the bundled Android runner
         build context/native-build signing scripts it needs) from this repo's
         GitHub Releases, and installs it under %LOCALAPPDATA%\Programs\ebl.
      3. Adds %LOCALAPPDATA%\Programs\ebl\bin to your Windows PATH.
      4. Runs `ebl setup --runtime <Mode>` — in Native mode this is where the actual
         JDK/Android SDK/Node downloads happen (noticeably longer than a Docker-mode
         install, which is just a Docker Desktop reachability check + image pulls).

    One-line usage (installs in Native mode by default):
      irm https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/windows/install.ps1 | iex

.PARAMETER Mode
    "Native" (default) or "Docker" - which build engine to set up. See DESCRIPTION.

.PARAMETER SkipDockerCheck
    Docker mode only. Skip the Docker Desktop presence check (e.g. if it's
    installed somewhere this script's detection doesn't recognize).

.PARAMETER SkipWslConfig
    Docker mode only. Skip WSL2 memory/swap auto-tuning entirely - leave
    %UserProfile%\.wslconfig (if any) untouched.

.PARAMETER ForceWslConfig
    Docker mode only. Re-run the WSL2 memory/swap auto-tuning even if
    %UserProfile%\.wslconfig already sets a [wsl2] memory limit. Without this, an
    existing memory= setting is treated as deliberate (yours or a previous ebl
    install's) and left alone.

.PARAMETER SkipRuntimeSetup
    Skip the automatic `ebl setup --runtime <Mode>` step at the end - just install
    the binary/PATH and stop there. Mainly for testing this script itself.

.PARAMETER LocalInstallDir
    Used by the Inno Setup GUI installer (ebl-setup.exe), which already bundles and
    extracts the release archive itself: points at that already-extracted bin\/share\
    tree instead of downloading one, so this script only does the Docker Desktop
    check, WSL2 tuning, and the PATH update.
#>

param(
    [ValidateSet("Native", "Docker")]
    [string]$Mode = "Native",
    [switch]$SkipDockerCheck,
    [switch]$SkipWslConfig,
    [switch]$ForceWslConfig,
    [switch]$SkipRuntimeSetup,
    [string]$LocalInstallDir
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn($msg) { Write-Host "!! $msg" -ForegroundColor Yellow }

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\ebl"
$BinDir = Join-Path $InstallDir "bin"
$ExePath = Join-Path $BinDir "ebl.exe"
$ReleaseZipUrl = "https://github.com/41vi4p/expo-builder-local/releases/latest/download/ebl-windows-amd64.zip"

Write-Step "Installing in $Mode mode..."

# --- 0. Docker Desktop ---------------------------------------------------------
# Docker mode only - native mode never touches Docker Desktop/WSL2 at all. ebl talks
# to Docker Desktop's own named pipe - checked first, before touching the install
# directory at all, so someone without Docker Desktop hits a clear message now
# instead of finishing setup only to find Docker unreachable at `ebl build` time.

if ($Mode -eq "Docker" -and -not $SkipDockerCheck) {
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

# --- 1. WSL2 memory/swap ---------------------------------------------------------
# Docker Desktop's default backend is a WSL2 VM; a build container filling it up
# during a heavy native compile crashes the Engine API rather than just being slow
# (reproduced firsthand on an 8-16GB laptop - see docs/CHANGELOG.md). Sized once
# here from actually-installed RAM rather than left at WSL2's 50%-of-host default.

if ($Mode -eq "Docker" -and -not $SkipWslConfig -and -not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    Write-Warn "WSL2 doesn't appear to be installed."
    Write-Warn "Docker Desktop's default backend needs it. If Docker Desktop is already"
    Write-Warn "working for you, it's using a different backend and you can ignore this -"
    Write-Warn "otherwise, in an elevated PowerShell or Command Prompt, run:"
    Write-Warn "  wsl --install"
    Write-Warn "then RESTART your computer (required), install Docker Desktop if you haven't"
    Write-Warn "already (https://www.docker.com/products/docker-desktop/), and re-run this"
    Write-Warn "installer (or just ebl setup) to auto-tune its memory/swap limits."
}

if ($Mode -eq "Docker" -and -not $SkipWslConfig -and (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    try {
        Write-Step "Tuning WSL2 memory/swap for this machine..."

        $wslConfigPath = Join-Path $env:USERPROFILE ".wslconfig"
        $existingLines = if (Test-Path $wslConfigPath) { Get-Content $wslConfigPath } else { @() }

        # Parse into an ordered [section -> [key -> value]] map so any unrelated
        # sections/keys already there (a user's own [experimental] block,
        # `processors=`, etc.) survive untouched.
        $sections = [ordered]@{}
        $currentSection = $null
        foreach ($line in $existingLines) {
            $trimmed = $line.Trim()
            if ($trimmed -match '^\[(.+)\]$') {
                $currentSection = $matches[1]
                if (-not $sections.Contains($currentSection)) { $sections[$currentSection] = [ordered]@{} }
            } elseif ($currentSection -and $trimmed -match '^([^=]+)=(.*)$') {
                $sections[$currentSection][$matches[1].Trim()] = $matches[2].Trim()
            }
        }

        $alreadyTuned = $sections.Contains("wsl2") -and $sections["wsl2"].Contains("memory") -and $sections["wsl2"]["memory"]
        if ($alreadyTuned -and -not $ForceWslConfig) {
            Write-Host "   $wslConfigPath already sets a wsl2 memory limit - leaving it alone (re-run with -ForceWslConfig to override)."
        } else {
            # Reserve at least 3GB (or 25% of RAM, whichever is bigger) for Windows
            # itself; give the rest to the VM. Swap is sized generously (2x memory,
            # capped at 32GB) since it's cheap, sparse disk space rather than RAM.
            $totalGB = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
            $hostReserveGB = [math]::Max(3, [math]::Round($totalGB * 0.25))
            $memoryGB = [math]::Max(4, $totalGB - $hostReserveGB)
            $swapGB = [math]::Max(8, [math]::Min(32, $memoryGB * 2))

            if (-not $sections.Contains("wsl2")) { $sections["wsl2"] = [ordered]@{} }
            $sections["wsl2"]["memory"] = "${memoryGB}GB"
            $sections["wsl2"]["swap"] = "${swapGB}GB"

            # If the system drive doesn't have comfortable room for that much swap
            # plus a safety margin, park the swap file on whichever other fixed
            # drive has the most free space instead - the build filling the system
            # drive with its own giant swap.vhdx is exactly the kind of
            # self-inflicted disk-pressure failure this step exists to avoid.
            $sysDrive = $env:SystemDrive.TrimEnd(':')
            $sysFreeGB = (Get-PSDrive $sysDrive).Free / 1GB
            if ($sysFreeGB -lt ($swapGB + 20)) {
                $altDrive = Get-CimInstance Win32_LogicalDisk -Filter "DriveType=3" |
                    Where-Object { $_.DeviceID -ne "$sysDrive`:" -and $_.FreeSpace -gt (($swapGB + 10) * 1GB) } |
                    Sort-Object FreeSpace -Descending |
                    Select-Object -First 1
                if ($altDrive) {
                    $swapDir = Join-Path $altDrive.DeviceID "ebl-data"
                    New-Item -ItemType Directory -Force -Path $swapDir | Out-Null
                    $sections["wsl2"]["swapfile"] = Join-Path $swapDir "wsl-swap.vhdx"
                    Write-Host "   System drive is low on space ($([math]::Round($sysFreeGB,1))GB free) - placing WSL2 swap on $($altDrive.DeviceID) instead."
                } else {
                    Write-Warn "System drive is low on space and no roomier fixed drive was found - WSL2 swap will stay there, which may itself run low during a build."
                }
            }

            $newLines = [System.Collections.Generic.List[string]]::new()
            foreach ($sectionName in $sections.Keys) {
                $newLines.Add("[$sectionName]")
                foreach ($key in $sections[$sectionName].Keys) {
                    $newLines.Add("$key=$($sections[$sectionName][$key])")
                }
                $newLines.Add("")
            }
            Set-Content -Path $wslConfigPath -Value $newLines -Encoding UTF8

            Write-Host "   Detected ${totalGB}GB RAM -> WSL2 memory=${memoryGB}GB, swap=${swapGB}GB ($wslConfigPath)."
            Write-Host "   Takes effect next time Docker Desktop restarts (or after 'wsl --shutdown')."
        }
    } catch {
        Write-Warn "Could not auto-tune WSL2 memory/swap ($($_.Exception.Message)) - Docker Desktop will use its own defaults."
    }
}

# --- 2. Install files ------------------------------------------------------------

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

if ($LocalInstallDir -and ([System.IO.Path]::GetFullPath($LocalInstallDir).TrimEnd('\') -ieq [System.IO.Path]::GetFullPath($InstallDir).TrimEnd('\'))) {
    # The Inno Setup GUI installer always calls this with -LocalInstallDir "{app}",
    # which DisableDirPage/DefaultDirName pin to this exact same $InstallDir - its
    # own [Files] section already placed bin\/share\ there directly, so there's
    # nothing to copy. Copying "{app}\bin" onto itself here used to throw (self-copy
    # is a terminating error under $ErrorActionPreference = "Stop"), silently killing
    # the whole script before it ever reached PATH setup or runtime setup below -
    # which is exactly what made a real install look like it "did almost nothing".
    Write-Step "Files already in place at $InstallDir (Inno Setup's own [Files] section put them there)."
} elseif ($LocalInstallDir) {
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

# --- 3. PATH -----------------------------------------------------------------------

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ";") -notcontains $BinDir) {
    Write-Step "Adding $BinDir to your PATH..."
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$BinDir", "User")
    $env:Path += ";$BinDir"
}

Write-Host ""
Write-Host "ebl installed." -ForegroundColor Green
Write-Host "  Location: $ExePath"

# --- 4. Runtime setup --------------------------------------------------------------
# Runs right here (using the exe's real path directly, not relying on the PATH
# update above having taken effect in this same process) rather than leaving it as
# a manual follow-up step - in Native mode especially, this is where the actual
# JDK/Android SDK/Node downloads happen, so the install isn't silently "incomplete"
# until someone remembers to run `ebl setup`.

if (-not $SkipRuntimeSetup) {
    Write-Host ""
    if ($Mode -eq "Native") {
        Write-Step "Setting up the native build engine - this downloads the Android SDK/JDK/Node and can take a while..."
    } else {
        Write-Step "Setting up Docker mode (checking Docker Desktop, pulling images)..."
    }
    & $ExePath setup --runtime $Mode.ToLower()
    if ($LASTEXITCODE -ne 0) {
        Write-Warn "ebl setup --runtime $($Mode.ToLower()) didn't finish cleanly (exit $LASTEXITCODE) - re-run it yourself later: ebl setup --runtime $($Mode.ToLower())"
    }
}

Write-Host ""
Write-Host "PATH updated, but a shell that was already open won't see it - that includes an"
Write-Host "already-open terminal tab in VS Code/Cursor/etc., since it inherits the editor's"
Write-Host "own already-running process. Open a NEW terminal window from the Start Menu/"
Write-Host "taskbar (or fully quit and relaunch the editor), then try:"
Write-Host "  cd path\to\your\expo\app"
Write-Host "  ebl build ."
