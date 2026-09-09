#Requires -Version 5.1
<#
.SYNOPSIS
    Uninstalls ebl (expo-local-builder) from Windows.

.DESCRIPTION
    Always removes the ebl install directory (%LOCALAPPDATA%\Programs\ebl) and its
    PATH entry - that part is never optional, it's what "uninstall" means. On top of
    that, this reads %APPDATA%\ebl\config.json to find any leftovers ebl itself put
    on this machine beyond the program files - a Native-mode install's downloaded
    JDK/Android SDK/Node toolchain (%LOCALAPPDATA%\ebl\toolchain\...), and saved
    settings/Expo token (%APPDATA%\ebl) - and, unless -Quiet was passed, shows a
    checkbox dialog so the user picks exactly which of those leftovers to remove and
    which to keep, rather than the old all-or-nothing single Y/N prompt. A component
    ebl only detected and reused (installedByEbl=false) is never offered for
    removal, since it existed before ebl and isn't ebl's to delete.

    Never touches Docker Desktop, WSL2, or %UserProfile%\.wslconfig's memory/swap
    tuning - those are either the system's own components, or shared machine-wide
    state other software may depend on by now.

.PARAMETER Quiet
    Skip the checkbox dialog entirely - removes only the install dir and PATH entry
    (the always-safe, always-happened part), leaving every optional leftover in
    place untouched. Used for a silent/unattended uninstall (the Inno Setup GUI
    uninstaller passes this automatically when run with /VERYSILENT); run this
    script directly from a terminal (or a normal, non-silent GUI uninstall) instead
    if you want the checkbox picker.
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
# Plain JSON via PowerShell's own ConvertFrom-Json - no dependency on the CLI's own
# C++ JSON parser, and no separate installer-side manifest file needed: config.json
# is already the single source of truth ebl setup --runtime native itself writes.
$config = $null
if (Test-Path $ConfigPath) {
    try {
        $config = Get-Content -Raw $ConfigPath | ConvertFrom-Json
    } catch {
        Write-Host "!! Could not read $ConfigPath ($($_.Exception.Message)) - skipping leftover-cleanup options." -ForegroundColor Yellow
    }
}

# --- Figure out what optional leftovers actually exist ------------------------------
# Each entry is something this script *could* delete beyond the install dir/PATH -
# never anything ebl only detected and reused.
$optionalItems = @()
if ($config -and $config.buildMode -eq "native" -and $config.nativeToolchain) {
    $nt = $config.nativeToolchain
    foreach ($c in @(
        @{ Key = "jdk"; Label = "JDK 21 (native build toolchain)"; Path = $nt.jdkHome; Installed = [bool]$nt.jdkInstalledByEbl }
        @{ Key = "androidsdk"; Label = "Android SDK (native build toolchain)"; Path = $nt.androidSdkRoot; Installed = [bool]$nt.androidSdkInstalledByEbl }
        @{ Key = "node"; Label = "Node.js (native build toolchain)"; Path = $nt.nodeHome; Installed = [bool]$nt.nodeInstalledByEbl }
    )) {
        if ($c.Installed -and $c.Path -and (Test-Path $c.Path)) {
            $optionalItems += $c
        }
    }
}
if (Test-Path $ConfigDir) {
    $optionalItems += @{ Key = "config"; Label = "Saved ebl settings and Expo token"; Path = $ConfigDir; Installed = $true }
}

# --- Checkbox dialog (skipped with -Quiet, or if there's nothing optional to ask about) ---
# Keys the user actually checked "remove" for. Empty (nothing removed) unless the
# dialog runs and something gets checked - the safe default is "keep".
$toRemove = @{}

if (-not $Quiet -and $optionalItems.Count -gt 0) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "Uninstall ebl (expo-local-builder)"
    $form.StartPosition = "CenterScreen"
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.ClientSize = New-Object System.Drawing.Size(460, 340)
    $form.Font = New-Object System.Drawing.Font("Segoe UI", 9)

    $intro = New-Object System.Windows.Forms.Label
    $intro.Text = "ebl's program files and PATH entry will be removed. It also found the following on this machine - choose what to remove and what to keep:"
    $intro.SetBounds(12, 12, 436, 48)
    $form.Controls.Add($intro)

    $list = New-Object System.Windows.Forms.CheckedListBox
    $list.SetBounds(12, 64, 436, 180)
    $list.CheckOnClick = $true
    foreach ($item in $optionalItems) {
        [void]$list.Items.Add("$($item.Label)  -  $($item.Path)", $true)  # checked by default = remove
    }
    $form.Controls.Add($list)

    $selectAllBtn = New-Object System.Windows.Forms.Button
    $selectAllBtn.Text = "Select all"
    $selectAllBtn.SetBounds(12, 252, 100, 28)
    $selectAllBtn.Add_Click({ for ($i = 0; $i -lt $list.Items.Count; $i++) { $list.SetItemChecked($i, $true) } })
    $form.Controls.Add($selectAllBtn)

    $selectNoneBtn = New-Object System.Windows.Forms.Button
    $selectNoneBtn.Text = "Keep all"
    $selectNoneBtn.SetBounds(118, 252, 100, 28)
    $selectNoneBtn.Add_Click({ for ($i = 0; $i -lt $list.Items.Count; $i++) { $list.SetItemChecked($i, $false) } })
    $form.Controls.Add($selectNoneBtn)

    $note = New-Object System.Windows.Forms.Label
    $note.Text = "Anything ebl only detected and reused (not downloaded itself) is never shown here."
    $note.ForeColor = [System.Drawing.Color]::DimGray
    $note.SetBounds(12, 286, 436, 20)
    $form.Controls.Add($note)

    $uninstallBtn = New-Object System.Windows.Forms.Button
    $uninstallBtn.Text = "Uninstall"
    $uninstallBtn.SetBounds(268, 306, 90, 28)
    $uninstallBtn.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.Controls.Add($uninstallBtn)

    $cancelBtn = New-Object System.Windows.Forms.Button
    $cancelBtn.Text = "Cancel"
    $cancelBtn.SetBounds(364, 306, 84, 28)
    $cancelBtn.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.Controls.Add($cancelBtn)

    $form.AcceptButton = $uninstallBtn
    $form.CancelButton = $cancelBtn

    $result = $form.ShowDialog()
    if ($result -eq [System.Windows.Forms.DialogResult]::OK) {
        for ($i = 0; $i -lt $optionalItems.Count; $i++) {
            if ($list.GetItemChecked($i)) {
                $toRemove[$optionalItems[$i].Key] = $true
            }
        }
    }
    # Cancel (or closing the window) leaves $toRemove empty - every optional item is
    # kept, same as unchecking everything. Only the always-mandatory part below runs.
}

# --- Always-mandatory part: install dir + PATH entry --------------------------------

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

# --- Optional leftovers, per what was checked in the dialog (or -Quiet: none) -------

foreach ($item in $optionalItems) {
    if ($toRemove.ContainsKey($item.Key) -and (Test-Path $item.Path)) {
        Remove-Item -Recurse -Force $item.Path
        Write-Host "   Removed $($item.Path)"
    }
}

Write-Host ""
Write-Host "ebl uninstalled." -ForegroundColor Green
$keptCount = $optionalItems.Count - $toRemove.Count
if ($keptCount -gt 0) {
    Write-Host "$keptCount item(s) were left in place - re-run this script (or uninstall again) if you change your mind."
}
if (-not $config -or $config.buildMode -ne "native") {
    Write-Host "Docker Desktop was left as-is - remove it yourself if you no longer need it."
    Write-Host "(Next time, 'ebl clean --all' before uninstalling reclaims pulled images/volumes first.)"
}
