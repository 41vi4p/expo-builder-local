; Inno Setup script for the ebl (expo-local-builder) Windows installer.
;
; This installer is deliberately thin: it bundles ebl.exe plus the same
; install.ps1/uninstall.ps1 already used for the one-line `irm | iex` install, and
; just runs them. All the real logic (WSL2 check/install, installing the actual
; Linux `ebl` inside WSL via the existing APT repo, adding ebl.exe to PATH) lives in
; those two scripts in exactly one place - the installer doesn't reimplement any of
; it, so the two install paths can never drift apart.
;
; Build with: iscc ebl.iss  (from a Windows machine/CI runner with Inno Setup 6
; installed - https://jrsoftware.org/isinfo.php). Expects
; ..\launcher\build\Release\ebl.exe to already be built (see ../launcher/CMakeLists.txt).

#define MyAppName "ebl (expo-local-builder)"
#define MyAppVersion "0.8.2"
#define MyAppPublisher "41vi4p"
#define MyAppURL "https://github.com/41vi4p/expo-builder-local"
#define MyAppExeName "ebl.exe"

[Setup]
AppId={{20DF2A08-D7F1-43EB-92F9-39D298B7A69A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={localappdata}\Programs\ebl
DisableProgramGroupPage=yes
; Per-user install, matching install.ps1 (%LOCALAPPDATA%\Programs\ebl) - no UAC
; prompt for the installer itself. install.ps1 separately requests elevation only
; if WSL2 itself still needs to be installed.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
OutputDir=dist
OutputBaseFilename=ebl-setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\launcher\build\Release\ebl.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\install.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\uninstall.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Run]
; -File runs install.ps1 out of the just-installed {app} dir (bundled, not
; downloaded again) - it still needs network access itself, to install the real
; ebl inside WSL and to fetch the launcher update check, if any.
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install.ps1"""; \
    Flags: waituntilterminated; \
    StatusMsg: "Setting up WSL2 and installing ebl - this can take a few minutes..."

[UninstallRun]
; -KeepWslPackage: the GUI uninstaller just removes the Windows-side launcher:
; asking an interactive y/n question via Read-Host mid-uninstall (like the
; standalone uninstall.ps1 does) doesn't work well from Inno Setup's own uninstall
; flow. Remove ebl from WSL by hand (or via uninstall.ps1 -RemoveFromWsl directly)
; if you want that too.
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\uninstall.ps1"" -KeepWslPackage"; \
    Flags: waituntilterminated runhidden
